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
  Se arma por comando HID 0xEE desde el cliente (los keycodes Fn+Y/Fn+U se
  quitaron: el keymap VIA vive en EEPROM y podía dejarlos desasignados); solo
  en modo Win, imposible en Gaming; auto-stop ante comandos de config
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
- **F6 Release-stretch anclado al tick** (19-jul, SOLO `alex_lab`): MC 1.8.9
  muestrea el *estado* de las teclas de movimiento 1 vez por tick (50 ms); un
  release+re-press que quepa entero entre dos muestreos no existe para el
  juego → w-tap sin sprint-reset (con RT 0.3/0.2 la ventana OFF de un tap
  rápido es ~15-30 ms = lotería) y tap de espacio sin reset de `jumpTicks`
  (salto retrasado hasta 500 ms bajo combo). Fix en la capa de REPORTE
  (`analog_matrix_release_stretch_apply`, no toca FSM/travel/scan): tras el
  release físico de W/espacio en Gaming, el estado reportado queda OFF
  ≥55 ms. Solo retrasa presses reales — no sintetiza ni adelanta nada.
  Al probar: subir release de W a 0.3 mm en Launcher (el stretch hace visible
  también el micro-release accidental por temblor).
- **F7 Whitelist predictiva por camino** (19-jul, máscaras en `alex_lab`):
  el re-press predictivo ACORTA la ventana OFF del w-tap (pelea contra F6),
  y el costo de un press fantasma depende de la mecánica, no de "ser tecla de
  movimiento". Máscaras separadas PRESS/REPRESS (default 0x3F = conducta
  previa): en lab PRESS = SPC+A+D (0x29) y REPRESS = solo A+D (0x28); S fuera (el
  fantasma más caro: corta sprint en chase), W fuera (borde en sumo +
  conflicto con F6), LSFT fuera (sin valor en sneak).
- **Cliente evlog**: histograma de ventanas OFF por tecla (release→re-press,
  resolución 1 ms, corre en Gaming, columna `off_ms` en el CSV). Es EL dato
  que decide F6: % de ventanas <50 ms en W/SPC con el build de torneo.
  (19-jul) + columna `fallo^` = Σ(1−d/50)/taps, la tasa esperada de taps
  invisibles (go/no-go del criterio #1), y columna `stretch` = ventanas en
  55-57 ms (conteo de disparos del clamp F6 en builds lab).
- **F8 CANDIDATO, NO construido — ON-stretch para S** (s-tap, la 3ª mecánica
  de reset): con par SOCD W/S, el OFF de W que ve el juego lo genera el
  ENMASCARADO de SOCD, no un release físico de W — F6 no lo clampea (el
  stretch ve estado físico, pre-SOCD). Fix simétrico: tras press físico de S,
  retener el ON reportado ≥55 ms antes de pasar el release. CONDICIONES para
  construirlo: (1) que el par W/S sobreviva la prueba de Launcher pendiente;
  (2) el par W/S debe ser **last-input, NO deeper-travel**: con deeper, el
  travel físico de S cae al soltar y el ganador vuelve a W a mitad del
  stretch (SOCD compara `analog_matrix_get_travel` = físico), cortando la
  ventana que el stretch intentaba garantizar; con last-input el ganador se
  mantiene hasta que el estado *reportado* de S cae → compone bien. Si no
  s-tapeas, es peso muerto y no se construye.

## Validación pendiente (Alex)

- [ ] Launcher: RT gaming 0.3/0.2 · Rappy Snappy A/D · OKMC de prueba
- [ ] Escritura con OKMC disparando (valida cola diferida)
- [ ] Lab Ornithe: strafes + jump-resets → TEST-RESULTS.md
- [ ] Medición D: reposo en frío vs tras 1-2 h sin desconectar → decide F2b
- [ ] Veredicto OKMC → decide F5
- [ ] Sesión evlog con build TORNEO (stretch off): % ventanas OFF <50 ms y
      `fallo^` en W/SPC durante w-taps/jump-resets reales → decide si F6 se queda
- [ ] A/B con build LAB: **mismo drill en Ornithe que la sesión torneo**
      (N w-taps + M jump-resets, mismos números) para histogramas comparables;
      piso 55 ms en W/SPC; evaluar sensación del w-tap (re-press hasta ~30 ms
      más tarde) y falsos sprint-reset por temblor (antes: W release 0.3 en
      Launcher). La columna `stretch` dice cuántas veces disparó el clamp.
- [ ] Re-correr mistype-hunt tras el recorte REPRESS (SPC fuera): fantasmas
      de espacio deben ir a ~0 estructuralmente (sin predicción de re-press no
      hay re-press especulativo). OJO: con build LAB el evlog no puede ver
      eventos <55 ms en W/SPC (el stretch los clampea) — la verificación de
      fantasmas sub-10 ms en esas dos teclas solo es medible con TORNEO.
- [ ] F7: verificar que S/W ya no predicen (prensa rápida superficial no
      dispara antes del cruce físico) y que SPC (solo primer press) y A/D
      siguen prediciendo
- [ ] Config Launcher sin firmware: hotbar 1-5 actuación 1.2-1.5 mm ·
      segundo par SOCD W/S para s-taps (opcional, probar en lab)
- [ ] Launcher: verificar qué tipo SOCD tiene el par A/D — con DEEPER_TRAVEL
      (no-SINGLE), ambas al fondo se registran las dos y el strafe se anula
      en MC; si el estilo es aplastar ambas, usar DEEPER_TRAVEL_SINGLE
      (mantiene ganador al fondo). Diseño, no bug.
- [ ] Sanidad post-revert del lockdown HID: con el build actual, Launcher
      debe funcionar completo en ambos modos (cambiar sensibilidad por tecla
      en Gaming incluido); solo calibrar y reset de perfil siguen bloqueados
      en Gaming. Escotilla de tuning = idea aparcada, construir solo si
      algún día reaparece la necesidad de endurecer.

## Guardas del modo Gaming (recordatorio)

`process_record_user` bloquea en Gaming: macros VIA, layer-switch, cambio de
perfil, bootloader/reboot/EEPROM, power/sleep/wake del host (flashear =
interruptor en Win), QK_MAGIC, keycodes wireless. LGUI se queda en el keymap (KC_NO hardcodeado revertido el
19-jul: desactivar Win en Gaming es preferencia por tecla que Launcher ya
resuelve — el firmware no fija lo que la config puede fijar). `config.h`
desactiva en Gaming: OKMC, toggle, gamepad, combos de perfil. Canal Raw HID
en Gaming: blacklist mínima (RESET_PROFILE y CALIBRATE). Se intentó (19-jul)
una whitelist de solo-lectura que bloqueaba todo SET/SAVE/SELECT + los sets
de VIA, y se REVIRTIÓ el mismo día: el tuning real exige el modo Gaming
ACTIVO (el perfil gaming solo corre ahí — se ajusta sensibilidad en Launcher
y se siente en vivo) y el lockdown rompía ese flujo. Si el endurecimiento
vuelve algún día, el diseño aparcado es la **escotilla de tuning**:
desbloqueo deliberado vía 0xEE (comando que solo nuestro tooling conoce) con
timeout ~10 min y recierre al cambiar el interruptor — nunca una whitelist
permanente. SOCD *desbloqueado* (12-jul) para
Rappy Snappy — zona gris en servers MC (no prohibido explícito en
Minemen/Hypixel; baneado en CS2/ESL), probar en lab primero.

## Gotchas conocidos

- El cliente apaga el diagnóstico en `finally` al salir, cerrar la gráfica o
  sufrir un error. Cualquier comando de Launcher también lo auto-apaga.
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
