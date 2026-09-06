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
        raise RuntimeError("No se encontro la interfaz Raw HID del Keychron K2 HE (VID 0x3434, PID 0x0E20).")
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
    if not resp or len(resp) < 19:
        return None
    status = resp[2]
    if status == 2:
        return {'status': 'DISABLED'}
    if status != 0:
        return {'status': f'ERR_{status}'}
    dur_last = struct.unpack_from('<H', bytes(resp), 3)[0]
    dur_min  = struct.unpack_from('<H', bytes(resp), 5)[0]
    dur_max  = struct.unpack_from('<H', bytes(resp), 7)[0]
    phase_last = struct.unpack_from('<H', bytes(resp), 9)[0]
    phase_min  = struct.unpack_from('<H', bytes(resp), 11)[0]
    phase_max  = struct.unpack_from('<H', bytes(resp), 13)[0]
    count      = struct.unpack_from('<I', bytes(resp), 15)[0]
    return {
        'status': 'OK',
        'duration_last_us': dur_last,
        'duration_min_us': dur_min,
        'duration_max_us': dur_max,
        'phase_last_us': phase_last,
        'phase_min_us': phase_min,
        'phase_max_us': phase_max,
        'count': count
    }

def get_poll_phase(device, reset=False):
    req = bytearray(33)
    req[1] = 0xA9
    req[2] = 0x60
    req[3] = 1 if reset else 0
    device.write(req)
    resp = device.read(32, timeout_ms=500)
    if not resp or len(resp) < 19:
        return None
    status = resp[2]
    if status == 2:
        return {'status': 'DISABLED'}
    if status != 0:
        return {'status': f'ERR_{status}'}
    last_us = struct.unpack_from('<I', bytes(resp), 3)[0]
    min_us  = struct.unpack_from('<I', bytes(resp), 7)[0]
    max_us  = struct.unpack_from('<I', bytes(resp), 11)[0]
    count   = struct.unpack_from('<I', bytes(resp), 15)[0]
    return {
        'status': 'OK',
        'poll_last_us': last_us,
        'poll_min_us': min_us,
        'poll_max_us': max_us,
        'count': count
    }

def main():
    print("==================================================")
    print("  Keychron K2 HE - Telemetria de Hardware en Vivo ")
    print("==================================================")
    try:
        dev = open_k2he()
        print("[+] Conectado a Raw HID (MI_01)")
    except Exception as e:
        print(f"[-] Error: {e}")
        return

    scan = get_scan_phase(dev, reset=False)
    poll = get_poll_phase(dev, reset=False)

    print(f"\n[SCAN METRICS (AMC_GET_SCAN_PHASE)]: {scan}")
    print(f"[POLL METRICS (AMC_GET_POLL_PHASE)]: {poll}\n")

    if scan and scan.get('status') == 'OK':
        print(f"  -> Duracion actual de barrido ADC: {scan['duration_last_us']} us")
        print(f"  -> Duracion Min/Max: {scan['duration_min_us']} us / {scan['duration_max_us']} us")
        print(f"  -> Fase de finalizacion vs SOF: {scan['phase_last_us']} us")
        print(f"  -> Fase Min/Max vs SOF: {scan['phase_min_us']} us / {scan['phase_max_us']} us")
        print(f"  -> Barridos analizados: {scan['count']}")

    if poll and poll.get('status') == 'OK':
        print(f"\n  -> Momento del Poll USB de Windows: {poll['poll_last_us']} us tras SOF")
        print(f"  -> Poll Min/Max: {poll['poll_min_us']} us / {poll['poll_max_us']} us")
        print(f"  -> Polls analizados: {poll['count']}")

    dev.close()

if __name__ == '__main__':
    main()
