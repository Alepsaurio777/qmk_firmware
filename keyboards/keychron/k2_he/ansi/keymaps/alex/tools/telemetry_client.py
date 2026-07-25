#!/usr/bin/env python3
"""Cliente de telemetria del K2 HE (ver TELEMETRY.md).

Uso:
    pip install hidapi
    python telemetry_client.py           # consola: travel en vivo + ruido p-p
    python telemetry_client.py --plot    # grafica en vivo (requiere matplotlib)
    python telemetry_client.py --events  # mistype-hunt: log + candidatos de rebote

El cliente arma y detiene ambos diagnosticos por Raw HID 0xEE. El stream de
travel solo funciona en Win; el logger de eventos tambien funciona en Gaming.
"""

import argparse
import collections
import statistics
import sys
import time

import hid

VID = 0x3434  # Keychron
PID = 0x0E20  # K2 HE ANSI
USAGE_PAGE = 0xFF60  # Raw HID QMK/VIA
USAGE = 0x61
MAGIC = 0xED
EVLOG_MAGIC = 0xEC
KEYS = ["W", "A", "S", "D", "SPC", "LSFT"]
EVLOG_KEYS = ["W", "A", "S", "D", "SPC", "LSFT", "LCTL"]

# Deteccion de fantasmas.
#
# Este detector busca una firma concreta: chatter/rebote release->re-press muy
# corto. No demuestra ausencia de todos los falsos inputs: un press espurio tras
# un idle largo, por ejemplo, queda fuera de esta metrica.
#
# (Historico: antes se marcaba "MARGINAL = press con travel < 30", pensando que
# un press superficial era sospechoso. Era ERRONEO: con rapid trigger el press
# dispara en valle+sensibilidad, y con una actuacion configurada a 0.4 mm eso
# son ~24 unidades — o sea marcaba el 100% de los presses normales. Medía la
# configuracion del usuario, no fantasmas. Eliminado.)
PHANTOM_MS = 10  # release->press bajo este umbral = candidato de chatter/rebote
FAST_MS = 20     # 10-20 ms: humanamente posible pero raro; se reporta como info

# Ventanas OFF (release->re-press) vs el tick de MC 1.8.9: el cliente muestrea
# el ESTADO de las teclas de movimiento 1 vez por tick (50 ms), asi que una
# ventana OFF de d ms solo es observada con probabilidad ~d/50 cuando d < 50.
# Un % alto de ventanas <50 ms en W/SPC significa w-taps sin sprint-reset y
# taps de espacio sin reset de jumpTicks — a loteria de fase. Por encima del
# cap no es un tap sino un re-engage normal y no entra al histograma.
TICK_MS = 50
OFFWIN_CAP_MS = 1000


def find_device():
    for d in hid.enumerate(VID, PID):
        if (d["vendor_id"] == VID and d["product_id"] == PID and
                d["usage_page"] == USAGE_PAGE and d["usage"] == USAGE):
            dev = hid.device()
            dev.open_path(d["path"])
            dev.set_nonblocking(False)
            path = d["path"].decode(errors="replace") if isinstance(d["path"], bytes) else d["path"]
            print(f"Conectado: {d['product_string']} ({path})")
            return dev
    sys.exit("No se encontro el K2 HE ANSI (VID:PID 3434:0E20, usage 0xFF60/0x61).")


def parse(pkt):
    if len(pkt) < 6 or pkt[0] != MAGIC:
        return None
    version = pkt[1]
    if version not in (1, 2):
        return None
    n = pkt[5]
    t_ms = pkt[2] | (pkt[3] << 8)
    seq = pkt[4]
    key_count = min(n, len(KEYS))
    if len(pkt) < 6 + key_count * 2:
        return None
    keys = [(pkt[6 + i * 2], pkt[6 + i * 2 + 1]) for i in range(key_count)]
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
DIAG_POLICY_DUMP = 0x20
EVLOG_VERSION = 3
# v3: el byte `pressed` es mascara — bit0 pulsada, bit1 flanco FISICO (crudo,
# antes de los stretches F6/F9). Con un build lab el flujo reportado no puede
# mostrar lo que el clamp se come; el fisico si.
EVLOG_PRESSED_BIT = 0x01
EVLOG_PHYSICAL_BIT = 0x02

POLICY_MAGIC = 0xEB
POLICY_VERSION = 1


def diag(dev, subcmd):
    """Arranca/para los diagnosticos por comando HID (sin depender de keycodes,
    que el keymap de VIA en EEPROM puede dejar desasignados)."""
    dev.write(bytes([0x00, DIAG_CMD, subcmd] + [0] * 29))


def stop_diag(dev, subcmd):
    """Best effort: no ocultar la excepcion original si el teclado se desconecto."""
    try:
        diag(dev, subcmd)
    except Exception:
        pass


def parse_events(pkt):
    if len(pkt) < 31 or pkt[0] != EVLOG_MAGIC or pkt[1] != EVLOG_VERSION:
        return None
    n = pkt[2]
    if n > 5 or len(pkt) < 3 + n * 5:
        return None
    events = []
    for i in range(n):
        off = 3 + i * 5
        t = pkt[off] | (pkt[off + 1] << 8)
        key_idx = pkt[off + 2]
        flags = pkt[off + 3]
        travel = pkt[off + 4]
        events.append((t, key_idx, bool(flags & EVLOG_PRESSED_BIT), travel,
                       bool(flags & EVLOG_PHYSICAL_BIT)))
    seq = pkt[28]
    dropped = pkt[29] | (pkt[30] << 8)
    return events, seq, dropped


def run_events(dev, csv_path=None):
    diag(dev, DIAG_EVLOG_ON)
    print("Mistype-hunt ACTIVO (el logger se armo por HID; no hace falta ninguna tecla).")
    print("Funciona en Gaming: pasa el interruptor y juega. Ctrl+C para salir.")
    print(f"CANDIDATO = release->press del mismo key en <{PHANTOM_MS} ms (firma de chatter/rebote).")
    print(f"No es un detector de todo falso input; {PHANTOM_MS}-{FAST_MS} ms se reporta como info.\n")
    csv_file = open(csv_path, "w", encoding="utf-8") if csv_path else None
    if csv_file:
        csv_file.write("t_ms,src,key,event,travel,flag,off_ms\n")

    # El timer del firmware es de 16 bits. El reloj monotonic del host permite
    # distinguir un intervalo corto real de uno largo que aliasa tras 65.536 s.
    #
    # Dos flujos independientes: el REPORTADO (lo que sale por USB, ya pasado por
    # F6/F9) y el FISICO (el flanco crudo del switch). En un build de torneo son
    # el mismo, porque no hay stretch; en un lab divergen, y esa divergencia es
    # justo el efecto que hay que medir. Se llevan por separado para que una sola
    # sesion de un solo drill de los dos histogramas.
    def new_counts():
        return {k: {"press": 0, "release": 0, "phantom": 0, "fast": 0, "trav": [], "offwin": []} for k in EVLOG_KEYS}

    last_by_src = {False: {}, True: {}}   # [physical] -> key_idx -> (t16, pressed, host_monotonic)
    counts_by_src = {False: new_counts(), True: new_counts()}
    t_status = 0.0
    last_packet_seq = None
    missing_packets = 0
    firmware_dropped = 0
    print("Pulsa WASD/espacio/LShift/LCtrl: el contador de abajo debe subir. Si NO sube,")
    print("el logger no esta corriendo (binario viejo?) — no es que este limpio.\n")
    try:
        while True:
            # Contador en vivo: prueba visible de que SI esta midiendo. Sin esto,
            # "no pasa nada" es indistinguible de "no funciona".
            now_s = time.time()
            if now_s - t_status > 0.4:
                t_status = now_s
                rep = counts_by_src[False]
                tot = sum(c["press"] + c["release"] for c in rep.values())
                fis = sum(c["press"] + c["release"] for c in counts_by_src[True].values())
                ph = sum(c["phantom"] for c in rep.values())
                fa = sum(c["fast"] for c in rep.values())
                print(f"\r  eventos: {tot:6d} (fisicos: {fis:6d}) | candidatos: {ph:4d} | rapidos(info): {fa:4d}",
                      end="", flush=True)

            pkt = dev.read(32, timeout_ms=1000)
            if not pkt:
                continue
            parsed = parse_events(pkt)
            if not parsed:
                continue
            events, packet_seq, firmware_dropped = parsed
            if last_packet_seq is not None:
                missing = (packet_seq - last_packet_seq - 1) & 0xFF
                if missing:
                    missing_packets += missing
                    print(f"\nAVISO: faltan {missing} paquete(s) evlog; la sesion no es concluyente.")
            last_packet_seq = packet_seq
            arrival = time.monotonic()
            for t, key_idx, pressed, travel, physical in events:
                if key_idx >= len(EVLOG_KEYS):
                    continue
                name = EVLOG_KEYS[key_idx]
                counts = counts_by_src[physical]
                last = last_by_src[physical]
                src = "fis" if physical else "rep"
                flag = ""
                off_ms = ""
                if pressed:
                    counts[name]["press"] += 1
                    counts[name]["trav"].append(travel)
                    if key_idx in last:
                        lt, lp, last_arrival = last[key_idx]
                        dt = (t - lt) & 0xFFFF
                        host_dt_ms = (arrival - last_arrival) * 1000.0
                        # Solo interpretar el t16 cuando el reloj del host
                        # confirma que no pudo ocurrir un wrap de 65.536 s.
                        if not lp and host_dt_ms < OFFWIN_CAP_MS + 1000:
                            if dt < OFFWIN_CAP_MS:
                                counts[name]["offwin"].append(dt)
                                off_ms = dt
                            if dt < PHANTOM_MS:
                                flag = f"CANDIDATO_REBOTE ({dt}ms)"
                                counts[name]["phantom"] += 1
                            elif dt < FAST_MS:
                                flag = f"rapido ({dt}ms)"
                                counts[name]["fast"] += 1
                else:
                    counts[name]["release"] += 1
                last[key_idx] = (t, pressed, arrival)
                evt = "PRESS  " if pressed else "release"
                line = f"[{t:5d}ms] {src} {name:4s} {evt} travel={travel:3d}"
                if flag:
                    # \n para no pisar la linea del contador en vivo
                    print(f"\n{line}   <<< {flag}")
                if csv_file:
                    csv_file.write(f"{t},{src},{name},{'press' if pressed else 'release'},{travel},{flag},{off_ms}\n")
    except KeyboardInterrupt:
        pass
    finally:
        stop_diag(dev, DIAG_EVLOG_OFF)
        if csv_file:
            csv_file.close()
    print()  # cerrar la linea del contador en vivo

    counts = counts_by_src[False]
    counts_fis = counts_by_src[True]
    total_events = sum(c["press"] + c["release"] for c in counts.values())
    total_fis = sum(c["press"] + c["release"] for c in counts_fis.values())
    if total_events == 0 and total_fis == 0:
        print("\n*** NO SE RECIBIO NINGUN EVENTO — SIN DATOS, NO ES 'LIMPIO' ***")
        print("Comprueba firmware actual, Launcher cerrado y que pulsaste las teclas vigiladas.")
        return

    print("\nResumen por tecla (flujo REPORTADO, lo que sale por USB):")
    print(f"  {'tecla':5s} {'press':>6s} {'release':>8s} {'candidato':>9s} {'rapido':>7s}   actuacion")
    for name in EVLOG_KEYS:
        c = counts[name]
        tv = c["trav"]
        act = f"travel~{int(statistics.median(tv))} ({statistics.median(tv)/60.0:.2f}mm)" if tv else "-"
        print(f"  {name:5s} {c['press']:6d} {c['release']:8d} {c['phantom']:9d} {c['fast']:7d}   {act}")

    def offwin_table(cnt, titulo):
        if not any(c["offwin"] for c in cnt.values()):
            return
        print(f"\nVentanas OFF — {titulo} (release→re-press, taps <{OFFWIN_CAP_MS} ms):")
        print(f"El juego muestrea el estado 1 vez por tick ({TICK_MS} ms); 'fallo^' estima taps invisibles.")
        print(f"  {'tecla':5s} {'taps':>5s} {'mediana':>8s} {'<10':>5s} {'10-24':>6s} {'25-49':>6s} {'>=50':>5s} {'%<50':>7s} {'fallo^':>7s} {'stretch':>8s}")
        for name in EVLOG_KEYS:
            ow = cnt[name]["offwin"]
            if not ow:
                continue
            b1 = sum(1 for d in ow if d < 10)
            b2 = sum(1 for d in ow if 10 <= d < 25)
            b3 = sum(1 for d in ow if 25 <= d < TICK_MS)
            b4 = sum(1 for d in ow if d >= TICK_MS)
            pct = 100.0 * (b1 + b2 + b3) / len(ow)
            miss = 100.0 * sum(1.0 - d / TICK_MS for d in ow if d < TICK_MS) / len(ow)
            stretch_n = sum(1 for d in ow if 55 <= d <= 57)
            print(f"  {name:5s} {len(ow):5d} {statistics.median(ow):7.0f}ms {b1:5d} {b2:6d} {b3:6d} {b4:5d} {pct:6.1f}% {miss:6.1f}% {stretch_n:8d}")

    offwin_table(counts, "REPORTADO (post F6/F9)")
    if total_fis:
        offwin_table(counts_fis, "FISICO (flanco crudo del switch)")
        print("\nLos dos flujos vienen de la MISMA sesion, asi que las dos tablas son")
        print("directamente comparables. El fisico dice cuantos taps hiciste de verdad;")
        print("el reportado, cuantos podia ver el juego. La diferencia es lo que compran")
        print("los stretches (y su coste en latencia).")
    else:
        print("\n(Sin eventos FISICOS: build de torneo, sin F6/F9. Aqui reportado == fisico,")
        print(" que es exactamente la linea base contra la que se compara el build lab.)")

    if csv_path:
        print(f"\nSesion guardada en {csv_path} (columna 'src': rep = reportado, fis = fisico)")

    total_ph = sum(c["phantom"] for c in counts.values())
    total_fa = sum(c["fast"] for c in counts.values())
    if firmware_dropped or missing_packets:
        print(f"\nSESION INCOMPLETA: firmware descarto {firmware_dropped} evento(s) y faltan {missing_packets} paquete(s).")
    elif total_ph == 0:
        print(f"\nSin candidatos sub-{PHANTOM_MS} ms en {total_events} eventos ({total_fa} re-press rapidos informativos).")
        print("Esto descarta chatter con esa firma; no demuestra ausencia de todo posible falso input.")
    else:
        print(f"\n{total_ph} candidato(s) sub-{PHANTOM_MS} ms de {total_events} eventos.")
        print("Revisa el CSV y repite la prueba antes de atribuirlos a rebote o ruido.")


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
        pass
    finally:
        stop_diag(dev, DIAG_TELEM_OFF)

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
    try:
        plt.show()  # bloquea hasta cerrar la ventana
    finally:
        stop_diag(dev, DIAG_TELEM_OFF)
        if csv_file:
            csv_file.close()

    if csv_file:
        print(f"Sesion guardada en {csv_path}")
    print("\nResumen de la sesion completa (desde que se abrio la grafica):")
    for i, k in enumerate(KEYS):
        st = stats[i]
        if st["n"]:
            print(f"  {k:4s}: max {st['max']:3d} ({st['max'] / 60.0:.2f} mm)  min {st['min']:3d}  muestras {st['n']}")
    print("Para la medicion C: el 'max' de W es el fondo alcanzado (ideal ~240).")


def run_policy(dev):
    """Volcado de la politica resuelta: a que posiciones fisicas aterrizaron las
    whitelists declaradas por keycode. Consulta sin estado, no arranca streams.

    Solo responde en builds con politica activa (lab). En torneo no hay nada que
    volcar y el comando cae al camino de 'desconocido'."""
    def coord(b):
        return "-" if b == 0xFF else f"({b >> 4},{b & 0x0F})"

    dev.write(bytes([0x00, DIAG_CMD, DIAG_POLICY_DUMP] + [0] * 29))
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        pkt = dev.read(32, timeout_ms=500)
        if not pkt or pkt[0] != POLICY_MAGIC:
            continue
        if pkt[1] != POLICY_VERSION:
            print(f"Version de volcado {pkt[1]}, esperaba {POLICY_VERSION}: firmware y cliente no coinciden.")
            return
        d = pkt[2:]
        flags = d[0]
        print("Politica resuelta (contra el keymap VIVO, remaps de Launcher incluidos):\n")
        print(f"  predictivo      : {'ON' if flags & 0x01 else 'off'}")
        print(f"  F6 release-str. : {'ON' if flags & 0x02 else 'off'}   slots {coord(d[25])} {coord(d[26])}")
        print(f"  F9 press-stretch: {'ON' if flags & 0x04 else 'off'}   slot  {coord(d[27])}")
        print(f"  continuous RT   : {'ON' if flags & 0x08 else 'off'}")
        if flags & 0x01:
            print("\n  Mascaras predictivas por fila (bit = columna):")
            print(f"    {'fila':>4s} {'press':>18s} {'repress':>18s}")
            for r in range(6):
                p = d[1 + r * 2] | (d[2 + r * 2] << 8)
                q = d[13 + r * 2] | (d[14 + r * 2] << 8)
                cols_p = ",".join(str(c) for c in range(16) if p >> c & 1) or "-"
                cols_q = ",".join(str(c) for c in range(16) if q >> c & 1) or "-"
                print(f"    {r:4d} {cols_p:>18s} {cols_q:>18s}")
        print("\nRecuerda: la resolucion se recalcula en boot, cambio de perfil, giro del")
        print("interruptor y remap desde Launcher. Si algo sale '-', ese keycode no esta")
        print("en la capa base de Gaming.")
        return
    print("Sin respuesta al volcado. Build de torneo (sin politica compilada), o Launcher")
    print("abierto acaparando el endpoint.")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--plot", action="store_true", help="grafica de travel en vivo con matplotlib")
    ap.add_argument("--events", action="store_true", help="mistype-hunt: log + candidatos de chatter/rebote")
    ap.add_argument("--policy", action="store_true", help="volcar la politica por keycode ya resuelta a posiciones")
    ap.add_argument("--csv", metavar="ARCHIVO", help="guardar a CSV (con --plot o --events)")
    args = ap.parse_args()
    device = find_device()
    try:
        if args.policy:
            run_policy(device)
        elif args.events:
            run_events(device, csv_path=args.csv)
        elif args.plot:
            run_plot(device, csv_path=args.csv)
        else:
            run_console(device)
    finally:
        device.close()
