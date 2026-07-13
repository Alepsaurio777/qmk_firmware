#!/usr/bin/env python3
"""Cliente de telemetria del K2 HE (ver TELEMETRY.md).

Uso:
    pip install hidapi
    python telemetry_client.py           # consola: travel en vivo + ruido p-p
    python telemetry_client.py --plot    # grafica en vivo (requiere matplotlib)

Activa el stream en el teclado con Fn+Y (modo Win) antes o despues de arrancar.
"""

import argparse
import collections
import sys
import time

import hid

VID = 0x3434  # Keychron
USAGE_PAGE = 0xFF60  # Raw HID QMK/VIA
USAGE = 0x61
MAGIC = 0xED
KEYS = ["W", "A", "S", "D", "SPC", "LSFT"]


def find_device():
    for d in hid.enumerate(VID):
        if d["usage_page"] == USAGE_PAGE and d["usage"] == USAGE:
            dev = hid.device()
            dev.open_path(d["path"])
            dev.set_nonblocking(False)
            print(f"Conectado: {d['product_string']} ({d['path'].decode()})")
            return dev
    sys.exit("No se encontro el endpoint Raw HID del K2 HE (VID 0x3434, usage 0xFF60/0x61).")


def parse(pkt):
    if len(pkt) < 6 or pkt[0] != MAGIC:
        return None
    version = pkt[1]
    n = pkt[5]
    t_ms = pkt[2] | (pkt[3] << 8)
    seq = pkt[4]
    keys = [(pkt[6 + i * 2], pkt[6 + i * 2 + 1]) for i in range(min(n, len(KEYS)))]
    scan = None
    if version >= 2 and len(pkt) >= 26:
        scan = {
            "count": pkt[18] | (pkt[19] << 8) | (pkt[20] << 16) | (pkt[21] << 24),
            "dur_us": pkt[22] | (pkt[23] << 8),
            "phase_us": pkt[24] | (pkt[25] << 8),
        }
    return t_ms, seq, keys, scan


def run_console(dev):
    window = [collections.deque(maxlen=1000) for _ in KEYS]  # ~5 s a 200 Hz
    phase_win = collections.deque(maxlen=1000)
    dur_win = collections.deque(maxlen=1000)
    last_seq = None
    last_count = None
    last_count_t = None
    scan_rate = 0
    lost = 0
    t0 = time.time()
    print("Ctrl+C para salir. travel 0-240 (240 = 4.0 mm). p-p = ruido pico-a-pico ventana 5 s.\n")
    try:
        while True:
            pkt = dev.read(32, timeout_ms=1000)
            if not pkt:
                continue
            parsed = parse(pkt)
            if not parsed:
                continue
            _, seq, keys, scan = parsed
            if last_seq is not None and (seq - last_seq) % 256 != 1:
                lost += ((seq - last_seq) % 256) - 1
            last_seq = seq
            for i, (travel, _state) in enumerate(keys):
                window[i].append(travel)
            now = time.time()
            if scan:
                phase_win.append(scan["phase_us"])
                dur_win.append(scan["dur_us"])
                if last_count is not None and now - last_count_t >= 1.0:
                    scan_rate = (scan["count"] - last_count) / (now - last_count_t)
                    last_count, last_count_t = scan["count"], now
                elif last_count is None:
                    last_count, last_count_t = scan["count"], now
            if now - t0 > 0.2:  # refresco de consola 5 Hz
                t0 = now
                cells = []
                for i, (travel, state) in enumerate(keys):
                    pp = max(window[i]) - min(window[i]) if window[i] else 0
                    mark = "*" if state else " "
                    cells.append(f"{KEYS[i]}{mark}{travel:3d} (p-p {pp:2d})")
                line = "\r" + " | ".join(cells) + f" | perdidos: {lost}"
                if dur_win:
                    line += f" | scan {dur_win[-1]:4d}us fase {phase_win[-1]:4d}us rate {scan_rate:6.0f}/s"
                print(line, end="", flush=True)
    except KeyboardInterrupt:
        print("\n\nResumen de ruido pico-a-pico (ultima ventana):")
        for i in range(len(KEYS)):
            if window[i]:
                pp = max(window[i]) - min(window[i])
                mm = pp / 60.0
                print(f"  {KEYS[i]:4s}: {pp:3d} unidades = {mm:.3f} mm -> rt minimo honesto ~{mm * 2:.2f} mm")
        if dur_win:
            print("\nFase 3 — timing del barrido (ultima ventana de ~5 s):")
            print(f"  duracion del barrido: min {min(dur_win)} us, max {max(dur_win)} us")
            print(f"  ritmo de barrido:     ~{scan_rate:.0f} barridos/s")
            print(f"  fase fin-barrido→SOF: min {min(phase_win)} us, max {max(phase_win)} us")
            print("  (fase distribuida uniforme 0-1000 = scan libre sin sincronia con USB;")
            print("   fase estable cerca de un valor = ya hay acoplamiento scan-poll)")


def run_plot(dev, csv_path=None):
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation

    hist = [collections.deque([0] * 600, maxlen=600) for _ in KEYS]  # 3 s visibles
    stats = [{"max": 0, "min": 255, "n": 0} for _ in KEYS]           # sesion completa
    csv_file = open(csv_path, "w", encoding="utf-8") if csv_path else None
    if csv_file:
        csv_file.write("t_ms,seq," + ",".join(KEYS) + "\n")

    # Sin atajos de teclado de matplotlib: probar teclas (s, w, a...) no debe
    # disparar acciones de la ventana.
    for key in list(plt.rcParams):
        if key.startswith("keymap."):
            plt.rcParams[key] = []

    fig, ax = plt.subplots()
    lines = [ax.plot([], [], label=k)[0] for k in KEYS]
    ax.set_ylim(-5, 245)
    ax.set_xlim(0, 600)
    ax.set_ylabel("travel (0-240)")
    ax.legend(loc="upper right")

    def update(_frame):
        for _ in range(40):  # drena el buffer
            pkt = dev.read(32, timeout_ms=1)
            if not pkt:
                break
            parsed = parse(pkt)
            if parsed:
                t_ms, seq, keys, _scan = parsed
                for i, (travel, _s) in enumerate(keys):
                    hist[i].append(travel)
                    st = stats[i]
                    st["max"] = max(st["max"], travel)
                    st["min"] = min(st["min"], travel)
                    st["n"] += 1
                if csv_file:
                    csv_file.write(f"{t_ms},{seq}," + ",".join(str(t) for t, _s in keys) + "\n")
        for i, line in enumerate(lines):
            line.set_data(range(len(hist[i])), list(hist[i]))
        return lines

    # blit=False: mas lento pero compatible con todos los backends (blit=True
    # rompe con 'restore_region' en algunos backends de Windows).
    _ani = FuncAnimation(fig, update, interval=30, blit=False, cache_frame_data=False)
    plt.show()  # bloquea hasta cerrar la ventana

    if csv_file:
        csv_file.close()
        print(f"Sesion guardada en {csv_path}")
    print("\nResumen de la sesion completa (desde que se abrio la grafica):")
    for i, k in enumerate(KEYS):
        st = stats[i]
        if st["n"]:
            print(f"  {k:4s}: max {st['max']:3d} ({st['max'] / 60.0:.2f} mm)  min {st['min']:3d}  muestras {st['n']}")
    print("Para la medicion C: el 'max' de W es el fondo alcanzado (ideal ~240).")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--plot", action="store_true", help="grafica en vivo con matplotlib")
    ap.add_argument("--csv", metavar="ARCHIVO", help="guardar todas las muestras a CSV (solo con --plot)")
    args = ap.parse_args()
    device = find_device()
    if args.plot:
        run_plot(device, csv_path=args.csv)
    else:
        run_console(device)
