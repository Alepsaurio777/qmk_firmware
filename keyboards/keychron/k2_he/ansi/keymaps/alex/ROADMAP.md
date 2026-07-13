# K2 HE firmware custom — registro del proyecto
### Sesiones 12-13 jul 2026 · Todo medido con la telemetría propia sobre hardware real

## Resultado final

Cadena de input del teclado, antes y después:

| Métrica | Antes | Después |
|---|---|---|
| Barrido completo | 1041-1052 µs (más lento que el frame USB) | **880-925 µs** (pipeline) |
| Ritmo de barrido | 938/s, libre | **1000/s exactos, anclado al SOF** |
| Fase barrido↔poll | Uniforme 9-980 µs (lotería por pulsación) | **Estable 890-935 µs** |
| Jitter interno peor caso | ~2 ms aleatorio | **~45 µs** |
| Polling USB | Dependía de EEPROM (podía ser div 3 = 1/8 de rate) | **1 kHz fijo en compile-time** (techo físico del F401 Full-Speed) |
| RT | 0.4 mm simétrico | **press 0.3 / release 0.2 mm** (asimétrico, por perfil) |
| Congelamiento por OKMC | Hasta 3 ms de scan muerto al disparar | **Cero** (cola diferida, 1 reporte/frame) |
| SOCD por profundidad | Comparador sin histéresis (chatter A/D a scan rate) | **Histéresis 0.1 mm + rastreo de dueño** |
| ISR de SOF | wait_us(20) cada frame (~2% CPU, ±20 µs jitter) | **Salida temprana con interval 0** |

Validado el 13-jul: fase estable *incluso con teclas activas y RT disparando*.

## Fases ejecutadas

- **F0 Telemetría** (`telemetry.c`, `TELEMETRY.md`, `tools/telemetry_client.py`):
  stream Raw HID 200 Hz de W/A/S/D/SPC/LSFT + métricas de timing (paquete v2).
  Fn+Y en modo Win; imposible en Gaming; auto-stop ante comandos de config
  (comparte endpoint con Launcher — sin auto-stop, Launcher muestra
  "not-connect" con el stream vivo).
- **F1 RT asimétrico**: el core de Keychron YA soportaba press/release
  separados por tecla/perfil (`rpd_trig_sen`/`_deact`, 0 = hereda); solo
  faltaban defaults por perfil (`profile_default_rt_sen[_rls]`). Valores
  derivados de medición: ruido post-filtro con dedos apoyados = 0 → release
  0.2 mm seguro; dips accidentales de mano ~0.7 mm → actuación se queda en
  2.0 mm (NO bajar a speed-switch).
- **F2 Calibración**: 2a/2d/2e ya existían en el core (recalibración de
  reposo en boot con compensación térmica, clamps, CRC16). Se añadió 2c:
  `ANALOG_BOTTOM_OUT_LEARN` — bottom-out por tecla solo-crece, 1 pasada/50 ms,
  flush EEPROM nunca en gaming. 2b (drift runtime) condicionada a medición D.
- **F3 Latencia**: pipeline del barrido (procesa columna N-1 durante el
  settle de N; recuperó ~160 µs) + `ANALOG_SCAN_SOF_SYNC` (barrido arranca
  SOF+10 µs, espera acotada 400 µs, free-run seguro sin SOF). Instrumentación
  bajo `USB_SOF_TIMING_PROBE`.
- **F4 SOCD**: Rappy Snappy/SOCD/OKMC ya existían en Launcher (no
  documentados). Auditoría contra upstream oficial: el comparador de
  profundidad NO tiene histéresis ni en `hall_effect_playground` ni en
  `2025q3` — bug de producción de todos los K2 HE. Fix:
  `ANALOG_SOCD_DEEPER_HYSTERESIS` (default 6 = 0.1 mm).
- **Deudas**: OKMC diferido (cola FIFO, corrige además orden press/release
  en pulsaciones rápidas); SOF ISR sin busy-wait a interval 0.
- **F5 Mod-tap exclusivo por profundidad**: PENDIENTE DE VEREDICTO. OKMC ya
  da "acción distinta por profundidad" pero dispara *ambos* umbrales al
  bajar; el mod-tap sería A *o* B exclusivo (decisión diferida hasta
  release/bottom-out). Se construye solo si el comportamiento de OKMC no
  basta en la práctica.

## Validación pendiente (Alex)

- [ ] Launcher: RT gaming 0.3/0.2 · Rappy Snappy A/D · OKMC de prueba
- [ ] Escritura con OKMC disparando (valida cola diferida)
- [ ] Lab Ornithe: strafes + jump-resets → TEST-RESULTS.md
- [ ] Medición D: reposo en frío vs tras 1-2 h sin desconectar → decide F2b
- [ ] Veredicto OKMC → decide F5

## Guardas del modo Gaming (recordatorio)

`process_record_user` bloquea en Gaming: macros VIA, layer-switch, cambio de
perfil, bootloader/reboot/EEPROM (flashear = interruptor en Win), QK_MAGIC,
keycodes wireless. `config.h` desactiva en Gaming: OKMC, toggle, gamepad,
combos de perfil. SOCD *desbloqueado* (12-jul) para Rappy Snappy — zona gris
en servers MC (no prohibido explícito en Minemen/Hypixel; baneado en CS2/ESL),
probar en lab primero.

## Gotchas conocidos

- El stream de telemetría NO se apaga al cerrar el cliente — solo Fn+Y (o
  cualquier comando de Launcher, vía auto-stop).
- Los defaults RT por perfil solo aplican en perfiles reseteados; la EEPROM
  con valores de Launcher siempre gana.
- Bottom-out aprendido nunca se encoge: si se cambia un switch por otro de
  imán más débil, recalibrar manualmente desde Launcher.
- Compilar: MSYS2 MinGW64 (`qmk compile -kb keychron/k2_he/ansi -km alex`);
  qmk no está en el PATH de PowerShell.

## Referencias

Algoritmos portados/auditados contra: [libhmk](https://github.com/peppapighs/libhmk)
(FSM RT, Null Bind, calibración solo-crece), [minipad](https://github.com/minipadKB/minipad-firmware)
(histéresis de zona asimétrica, arranque seguro), [macrolev](https://github.com/heiso/macrolev).
Historia upstream: ramas `hall_effect_playground` / `2025q3` de Keychron/qmk_firmware.
