import hid
import struct
import time
import sys

def open_k2he():
    devs = hid.enumerate(0x3434, 0x0E20)
    raw_path = None
    for d in devs:
        if d['interface_number'] == 1 or d['usage_page'] == 0xFF60:
            raw_path = d['path']
            break
    if not raw_path:
        raise RuntimeError("No se encontro la interfaz Raw HID del Keychron K2 HE.")
    device = hid.device()
    device.open_path(raw_path)
    return device

def get_scan_phase(device, reset=False):
    req = bytearray(33)
    req[1] = 0xA9
    req[2] = 0x61
    req[3] = 1 if reset else 0
    device.write(req)
    resp = device.read(32, timeout_ms=500)
    if not resp or len(resp) < 19 or resp[2] != 0:
        return None
    dur_last = struct.unpack_from('<H', bytes(resp), 3)[0]
    dur_min  = struct.unpack_from('<H', bytes(resp), 5)[0]
    dur_max  = struct.unpack_from('<H', bytes(resp), 7)[0]
    phase_last = struct.unpack_from('<H', bytes(resp), 9)[0]
    phase_min  = struct.unpack_from('<H', bytes(resp), 11)[0]
    phase_max  = struct.unpack_from('<H', bytes(resp), 13)[0]
    count      = struct.unpack_from('<I', bytes(resp), 15)[0]
    return {
        'dur_last': dur_last,
        'dur_min': dur_min,
        'dur_max': dur_max,
        'phase_last': phase_last,
        'phase_min': phase_min,
        'phase_max': phase_max,
        'count': count
    }

def get_poll_phase(device, reset=False):
    req = bytearray(33)
    req[1] = 0xA9
    req[2] = 0x60
    req[3] = 1 if reset else 0
    device.write(req)
    resp = device.read(32, timeout_ms=500)
    if not resp or len(resp) < 19 or resp[2] != 0:
        return None
    last_us = struct.unpack_from('<I', bytes(resp), 3)[0]
    min_us  = struct.unpack_from('<I', bytes(resp), 7)[0]
    max_us  = struct.unpack_from('<I', bytes(resp), 11)[0]
    count   = struct.unpack_from('<I', bytes(resp), 15)[0]
    return {
        'poll_last': last_us,
        'poll_min': min_us,
        'poll_max': max_us,
        'count': count
    }

def main(duration_seconds=15):
    dev = open_k2he()
    print("=" * 78)
    print(f"  Keychron K2 HE - Monitoreo de Telemetria en Vivo ({duration_seconds} segundos)")
    print("=" * 78)
    header = f"{'Sec':>3} | {'Scan Dur':>9} | {'Scan Min':>8} | {'Scan Max':>8} | {'Scans/s':>7} | {'Poll Min':>8} | {'Poll Max':>8} | {'Phase End':>9}"
    print(header)
    print("-" * 78)

    get_scan_phase(dev, reset=True)
    get_poll_phase(dev, reset=True)

    scan_durs = []
    poll_mins = []
    poll_maxs = []
    phase_lasts = []
    total_scans = 0

    for sec in range(1, duration_seconds + 1):
        time.sleep(1.0)
        scan = get_scan_phase(dev, reset=True)
        poll = get_poll_phase(dev, reset=True)
        if not scan or not poll:
            print(f"{sec:>3} | Error al leer telemetria")
            continue

        cnt = scan['count']
        total_scans += cnt
        scan_durs.append(scan['dur_last'])
        phase_lasts.append(scan['phase_last'])
        poll_mins.append(poll['poll_min'])
        poll_maxs.append(poll['poll_max'])

        dur_str = f"{scan['dur_last']} us"
        min_str = f"{scan['dur_min']} us"
        max_str = f"{scan['dur_max']} us"
        pmin_str = f"{poll['poll_min']} us"
        pmax_str = f"{poll['poll_max']} us"
        phase_str = f"{scan['phase_last']} us"

        line = f"{sec:>3} | {dur_str:>9} | {min_str:>8} | {max_str:>8} | {cnt:>7} | {pmin_str:>8} | {pmax_str:>8} | {phase_str:>9}"
        print(line)

    dev.close()
    print("-" * 78)
    avg_dur = sum(scan_durs) / len(scan_durs) if scan_durs else 0
    avg_phase = sum(phase_lasts) / len(phase_lasts) if phase_lasts else 0
    global_min_poll = min(poll_mins) if poll_mins else 0
    global_max_poll = max(poll_maxs) if poll_maxs else 0

    print(f"[+] Total de barridos evaluados: {total_scans:,}")
    print(f"[+] Frecuencia de escaneo promedio: {total_scans / duration_seconds:.1f} Hz")
    print(f"[+] Duracion media del barrido ADC: {avg_dur:.1f} us (minimo global: {min(scan_durs)} us, maximo global: {max(scan_durs)} us)")
    print(f"[+] Fase media de fin de scan vs SOF: {avg_phase:.1f} us tras SOF")
    print(f"[+] Ventana de recepcion USB del Host: [{global_min_poll} us - {global_max_poll} us] tras SOF")
    print("=" * 78)

if __name__ == '__main__':
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 15
    main(secs)
