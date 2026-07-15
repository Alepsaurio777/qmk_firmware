#!/usr/bin/env python3
"""Cliente de telemetria del K2 HE (ver TELEMETRY.md).

Uso:
    pip install hidapi
    python telemetry_client.py           # consola: travel en vivo + ruido p-p
    python telemetry_client.py --plot    # grafica en vivo (requiere matplotlib)
    python telemetry_client.py --events  # mistype-hunt: log de eventos + fantasmas

Travel: Fn+Y (solo modo Win). Eventos: Fn+U (funciona en Gaming — el objetivo
es cazar fantasmas jugando de verdad).
"""

import argparse
import collections
import statistics
import sys
import time

import hid

VID = 0x3434  # Keychron
USAGE_PAGE = 0xFF60  # Raw HID QMK/VIA
USAGE = 0x61
MAGIC = 0xED
EVLOG_MAGIC = 0xEC
KEYS = ["W", "A", "S", "D", "SPC", "LSFT"]
EVLOG_KEYS = ["W", "A", "S", "D", "SPC", "LSFT", "LCTL"]

# Deteccion de fantasmas.
#
# La UNICA firma que un fantasma no puede falsear es el TIEMPO. Un rebote de
# resorte ocurre en el tiempo de asentamiento del stem (~2-5 ms); un re-press
# humano deliberado no baja de ~20-25 ms. Ese hueco es el detector.
#
# (Historico: antes se marcaba "MARGINAL = press con travel < 30", pensando que
# un press superficial era sospechoso. Era ERRONEO: con rapid trigger el press
# dispara en valle+sensibilidad, y con una actuacion configurada a 0.4 mm eso
# son ~24 unidades — o sea marcaba el 100% de los presses normales. Medía la
# configuracion del usuario, no fantasmas. Eliminado.)
PHANTOM_MS = 10  # release->press en < esto = imposible a proposito -> fantasma
FAST_MS = 20     # 10-20 ms: humanamente posible pero raro; se reporta como info


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


DIAG_CMD = 0xEE
DIAG_EVLOG_OFF, DIAG_EVLOG_ON = 0x00, 0x01
DIAG_TELEM_OFF, DIAG_TELEM_ON = 0x10, 0x11


def diag(dev, subcmd):
    """Arranca/para los diagnosticos por comando HID (sin depender de keycodes,
    que el keymap de VIA en EEPROM puede dejar desasignados)."""
    dev.write(bytes([0x00, DIAG_CMD, subcmd] + [0] * 29))


def parse_events(pkt):
    if len(pkt) < 3 or pkt[0] != EVLOG_MAGIC:
        return None
    n = pkt[2]
    events = []
    for i in range(n):
        off = 3 + i * 5
        if off + 5 > len(pkt):
            break
        t = pkt[off] | (pkt[off + 1] << 8)
        key_idx = pkt[off + 2]
        pressed = pkt[off + 3]
        travel = pkt[off + 4]
        events.append((t, key_idx, pressed, travel))
    return events


def run_events(dev, csv_path=None):
    diag(dev, DIAG_EVLOG_ON)
    print("Mistype-hunt ACTIVO (el logger se armo por HID; no hace falta ninguna tecla).")
    print("Funciona en Gaming: pasa el interruptor y juega. Ctrl+C para salir.")
    print(f"FANTASMA = release->press del mismo key en <{PHANTOM_MS} ms (imposible a proposito:")
    print(f"un rebote de resorte vive en 2-5 ms). {PHANTOM_MS}-{FAST_MS} ms se reporta como info.\n")
    csv_file = open(csv_path, "w", encoding="utf-8") if csv_path else None
    if csv_file:
        csv_file.write("t_ms,key,event,travel,flag\n")

    last = {}  # key_idx -> (t, pressed) del ultimo evento
    counts = {k: {"press": 0, "release": 0, "phantom": 0, "fast": 0, "trav": []} for k in EVLOG_KEYS}
    t_status = 0.0
    print("Pulsa WASD/espacio/LShift/LCtrl: el contador de abajo debe subir. Si NO sube,")
    print("el logger no esta corriendo (binario viejo?) — no es que este limpio.\n")
    try:
        while True:
            # Contador en vivo: prueba visible de que SI esta midiendo. Sin esto,
            # "no pasa nada" es indistinguible de "no funciona".
            now_s = time.time()
            if now_s - t_status > 0.4:
                t_status = now_s
                tot = sum(c["press"] + c["release"] for c in counts.values())
                ph = sum(c["phantom"] for c in counts.values())
                fa = sum(c["fast"] for c in counts.values())
                print(f"\r  eventos: {tot:6d} | FANTASMAS: {ph:4d} | rapidos(info): {fa:4d}",
                      end="", flush=True)

            pkt = dev.read(32, timeout_ms=1000)
            if not pkt:
                continue
            events = parse_events(pkt)
            if not events:
                continue
            for t, key_idx, pressed, travel in events:
                if key_idx >= len(EVLOG_KEYS):
                    continue
                name = EVLOG_KEYS[key_idx]
                flag = ""
                if pressed:
                    counts[name]["press"] += 1
                    counts[name]["trav"].append(travel)
                    if key_idx in last:
                        lt, lp = last[key_idx]
                        dt = (t - lt) & 0xFFFF
                        if lp == 0:  # el evento previo fue un release
                            if dt < PHANTOM_MS:
                                flag = f"FANTASMA ({dt}ms: imposible a proposito)"
                                counts[name]["phantom"] += 1
                            elif dt < FAST_MS:
                                flag = f"rapido ({dt}ms)"
                                counts[name]["fast"] += 1
                else:
                    counts[name]["release"] += 1
                last[key_idx] = (t, pressed)
                evt = "PRESS  " if pressed else "release"
                line = f"[{t:5d}ms] {name:4s} {evt} travel={travel:3d}"
                if flag:
                    # \n para no pisar la linea del contador en vivo
                    print(f"\n{line}   <<< {flag}")
                if csv_file:
                    csv_file.write(f"{t},{name},{'press' if pressed else 'release'},{travel},{flag}\n")
    except KeyboardInterrupt:
        diag(dev, DIAG_EVLOG_OFF)
        if csv_file:
            csv_file.close()
        print()  # cerrar la linea del contador en vivo

        total_events = sum(c["press"] + c["release"] for c in counts.values())

        # Sin datos NO es lo mismo que sin fantasmas: si no llego ningun evento,
        # no hay medicion y no se puede concluir nada.
        if total_events == 0:
            print("\n*** NO SE RECIBIO NINGUN EVENTO — SIN DATOS, NO ES 'LIMPIO' ***")
            print("El logger no envio nada. Diagnostico, en orden:")
            print("  1. Firmware: flasheaste builds/k2he-TORNEO.bin ACTUAL? El control por HID")
            print("     solo existe en el build nuevo; uno viejo ignora el comando.")
            print("  2. Launcher cerrado? Si habla por HID, los diagnosticos se auto-apagan.")
            print("  3. Pulsaste teclas de movimiento (WASD/espacio/LShift/LCtrl)? Solo esas")
            print("     se registran; el resto del teclado no genera eventos.")
            return

        print("\nResumen por tecla:")
        print(f"  {'tecla':5s} {'press':>6s} {'release':>8s} {'FANTASMA':>9s} {'rapido':>7s}   actuacion")
        for name in EVLOG_KEYS:
            c = counts[name]
            tv = c["trav"]
            act = f"travel~{int(statistics.median(tv))} ({statistics.median(tv)/60.0:.2f}mm)" if tv else "-"
            print(f"  {name:5s} {c['press']:6d} {c['release']:8d} {c['phantom']:9d} {c['fast']:7d}   {act}")
        if csv_path:
            print(f"\nSesion guardada en {csv_path}")

        total_ph = sum(c["phantom"] for c in counts.values())
        total_fa = sum(c["fast"] for c in counts.values())
        print(f"\n(La columna 'actuacion' es informativa: revela tu punto de actuacion real,")
        print(" el que tengas puesto en Launcher. No es una anomalia.)")
        if total_ph == 0:
            print(f"\nLIMPIO: {total_events} eventos, 0 fantasmas (0 re-press bajo {PHANTOM_MS} ms).")
            print("Un rebote de resorte viviria en 2-5 ms; no hay ninguno. Tus switches no")
            print(f"rebotan y la señal no genera falsos disparos. ({total_fa} eventos rapidos de")
            print(f"{PHANTOM_MS}-{FAST_MS} ms = tapeo humano rapido, normal.)")
        else:
            print(f"\n{total_ph} FANTASMAS de {total_events} eventos: re-press bajo {PHANTOM_MS} ms,")
            print("imposible a proposito. Si se concentran en LSFT/LCTL -> histeresis adaptativa")
            print("cerca del reposo (fix Wooting). Revisa el CSV para el contexto.")


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
    diag(dev, DIAG_TELEM_ON)
    print("Stream de travel ACTIVO (armado por HID; solo funciona en modo Win).")
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
        diag(dev, DIAG_TELEM_OFF)
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
    diag(dev, DIAG_TELEM_ON)
    _ani = FuncAnimation(fig, update, interval=30, blit=False, cache_frame_data=False)
    plt.show()  # bloquea hasta cerrar la ventana
    diag(dev, DIAG_TELEM_OFF)

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
    ap.add_argument("--plot", action="store_true", help="grafica de travel en vivo con matplotlib")
    ap.add_argument("--events", action="store_true", help="mistype-hunt: log de eventos + deteccion de fantasmas")
    ap.add_argument("--csv", metavar="ARCHIVO", help="guardar a CSV (con --plot o --events)")
    args = ap.parse_args()
    device = find_device()
    if args.events:
        run_events(device, csv_path=args.csv)
    elif args.plot:
        run_plot(device, csv_path=args.csv)
    else:
        run_console(device)
