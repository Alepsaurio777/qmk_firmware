# K2 HE firmware custom — registro del proyecto
### Sesiones 12 jul – 25 jul 2026 · Lo medido, medido con la telemetría propia sobre hardware real

## Estado actual

**La cadena de latencia está cerrada y validada** (12-13 jul, tabla abajo). Lo
posterior no es latencia: es **mecánica de tick** — que el juego vea el input o
no — más la instrumentación para poder decidirlo con datos.

| Área | Estado |
|---|---|
| Latencia y jitter del barrido | **Cerrado y medido.** Nada más que ganar (ver «Qué podría seguir») |
| F6 mínimo-OFF (W, SPC) | Construido, `alex_lab`. **Sin validar en hardware** |
| F9 mínimo-ON (SPC) | Construido 24-jul, `alex_lab`. **Sin validar en hardware** |
| F7 whitelist predictiva | Construido. RT predictivo marcado como juguete permanente de lab |
| F8 ON-stretch para S | **No construido**, condicionado a s-tapear |
| Instrumentación | evlog v3 (flujo físico + reportado en una sesión), `--policy`, re-resolución en caliente |
| Hardcodes de coordenadas | **Eliminados del proyecto** (24-jul, incluida la tabla de la telemetría). Todo por keycode |

Tamaños de referencia: `alex` 54288 B, `alex_lab` 56680 B.

### La cadena de input, antes y después (12-13 jul)

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
- **F9 Mínimo-ON en el espacio** (24-jul, SOLO `alex_lab`): el espejo de F6.
  `jumpTicks` falla de **dos** formas distintas y cada una pide su garantía —
  *press* no visto (ON < 1 tick) ⇒ **el salto no existió**; *release* no visto
  (OFF < 1 tick) ⇒ `jumpTicks` no se resetea y el siguiente salto llega hasta
  500 ms tarde. Por eso el espacio lleva **F6 y F9 a la vez** y no hay que
  elegir. Se encadenan **F9 → F6** y el orden es load-bearing: al revés, F6
  vería el release físico, abriría su ventana OFF en t=0 y pelearía contra el ON
  que F9 sostiene. Encadenados se **apilan**, que es lo correcto — el OFF tiene
  que verse *después* de que el ON se viera, porque son dos muestreos distintos.
  Coste peor caso 55+55 = **110 ms** de ciclo para un tap; mal número en
  abstracto, 4.5x a favor contra los 500 ms que se pagan hoy. Un solo slot: W no
  lo lleva (su mecánica la dispara que se vea el OFF, y extender su ON sería
  movimiento no pedido — mortal en un borde de sumo). Misma capa de reporte que
  F6: no toca FSM ni travel, no sintetiza un press que no hiciste — sostiene uno
  que sí hiciste.
- **Instrumentación (24-jul)**: el evlog pasa a **v3** con bit de flanco
  *físico*, así que una sola sesión da los histogramas reportado **y** físico y
  ya no hay que cruzar dos drills distintos (era la mayor fuente de error del
  A/B). Y `--policy` (comando `0xEE 0x20`) vuelca la política por keycode ya
  resuelta a posiciones. Detalles en TELEMETRY.md.
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

**El evlog v3 disolvió el requisito de las dos sesiones.** El plan anterior pedía
una sesión TORNEO y otra LAB con «el mismo drill, mismos números» para tener
histogramas comparables — y que dos drills «iguales» lo sean de verdad era la
mayor fuente de error del A/B. Ya no hace falta: el flujo **físico** en LAB es lo
que el reportado en TORNEO sería, porque los stretches no tocan el flanco físico.
Una sola sesión LAB da los dos histogramas. Cobertura completa: W y SPC dan los
dos flujos; A/S/D no tienen stretch, así que su reportado *es* el físico.

### Antes de jugar (2 min, LAB flasheado, Launcher CERRADO)

- [ ] `--policy`: los slots deben salir F6 en `(2,2)` y `(5,6)`, F9 en `(5,6)`.
      Un `-` significa que ese keycode no está en la capa base de Gaming y la
      feature no está actuando — mejor saberlo antes de jugar media hora.
- [ ] Remapear W en Launcher y re-correr `--policy` **sin girar el interruptor**:
      la coordenada debe moverse. Eso valida la re-resolución en caliente.
      Deshacer el remap después.
- [ ] **Efecto observador del evlog. Va antes que todo lo demás.**
      `telemetry_task` se auto-apaga en Gaming; `evlog_task` no — y no debe,
      medir en Gaming es su razón de ser. O sea que durante una sesión armada
      manda paquetes Raw HID desde housekeeping, al lado del barrido; y como el
      barrido está anclado al SOF con espera acotada a 400 µs, una iteración
      larga no lo retrasa: le hace **perder la ventana**. Correr el probe con el
      evlog armado vs en reposo y comparar duración y fase. Si la fase se sale
      del rango estable 890–935 µs con el evlog armado, todos los números
      posteriores llevan ese sesgo.
- [ ] F9 en seco: un tap corto de espacio debe dar ≥55 ms de ON en `src=rep`
      mientras `src=fis` muestra la duración real, con el ciclo completo por
      debajo de ~110 ms.
- [ ] Que un tap de espacio produzca **un solo** flanco `src=fis`, no dos. El
      guard contra el doble conteo está verificado en el desensamblado (F6 se
      calla en las teclas que F9 ya reporta), pero eso confirma que el código
      está, no que el comportamiento en vivo sea el esperado. Si salieran dos,
      el histograma físico estaría inflado al doble justo en la medición para la
      que existe F9.

### La sesión que decide F6 y F9 (una sola, con el build LAB)

- [ ] Sesión de juego real con `--events --csv`: w-taps y jump-resets. De ahí
      salen las dos tablas de ventanas OFF, `REPORTADO` y `FÍSICO`, de la misma
      corrida y por tanto comparables. El físico dice cuántos taps hiciste de
      verdad; el reportado, cuántos podía ver el juego. La diferencia es lo que
      compran los stretches, y su coste en latencia.
      Antes de empezar: **subir el release de W a 0.3 mm en Launcher** — el
      stretch hace visible también el micro-release accidental por temblor con el
      dedo apoyado, y a 0.2 mm queda muy justo en pelea.
      Evaluar además la sensación del w-tap (re-press hasta ~30 ms más tarde) y
      los falsos sprint-reset por temblor. La columna `stretch` cuenta los
      disparos del clamp.
- [ ] Mismo CSV: fantasmas de espacio tras el recorte REPRESS (SPC fuera de la
      máscara) deben ir a ~0 **estructuralmente** — sin predicción de re-press no
      hay re-press especulativo. Ahora sí es medible en LAB: el flujo `src=fis`
      ve los sub-10 ms que el clamp esconde del reportado.
- [ ] F7: que S y W ya no predigan (una prensa rápida superficial no debe
      disparar antes del cruce físico) y que SPC (solo primer press) y A/D sigan
      prediciendo.

### Control, ya no prerrequisito

- [ ] Sesión con build TORNEO: confirmar que el físico-en-LAB coincide de verdad
      con el reportado-en-TORNEO, y medir el efecto observador en el build que de
      verdad se usa (el probe es solo-lab, así que aquí solo se puede comparar
      indirectamente).

### Independientes del batch de stretches

- [ ] Launcher: RT gaming 0.3/0.2 · Rappy Snappy A/D · OKMC de prueba
- [ ] Escritura con OKMC disparando (valida cola diferida)
- [ ] Medición D: reposo en frío vs tras 1-2 h sin desconectar → decide F2b
- [ ] Veredicto OKMC → decide F5
- [ ] Config Launcher sin firmware: hotbar 1-5 actuación 1.2-1.5 mm ·
      segundo par SOCD W/S para s-taps (condiciona F8)
- [ ] Launcher: verificar qué tipo SOCD tiene el par A/D — con DEEPER_TRAVEL
      (no-SINGLE), ambas al fondo se registran las dos y el strafe se anula
      en MC; si el estilo es aplastar ambas, usar DEEPER_TRAVEL_SINGLE
      (mantiene ganador al fondo). Diseño, no bug.
- [ ] Sanidad post-revert del lockdown HID: con el build actual, Launcher
      debe funcionar completo en ambos modos (cambiar sensibilidad por tecla
      en Gaming incluido); solo calibrar y reset de perfil siguen bloqueados
      en Gaming. Escotilla de tuning = idea aparcada, construir solo si
      algún día reaparece la necesidad de endurecer.

## Qué podría seguir (inventario honesto, 25-jul)

**Performance: se terminó, aritméticamente.** El barrido son 880–925 µs de un
frame de 1000, el poll está en el techo físico del F401 Full-Speed (8 kHz exige
otro MCU, no otro firmware) y **un tick de MC son 50 frames USB**. Para que el
juego note una mejora de latencia habría que ahorrar ~50 ms; la latencia total de
la cadena es 1–2 ms. No hay 50 ms que ahorrar en ningún sitio. Todo trabajo de
latencia que quede es medible e inútil: falla el criterio #4 por construcción.

Corolario: el firmware ya no puede mover un límite de tick **haciéndose más
rápido**. Solo puede moverlo cambiando **si el juego ve el input o no**. Esa es la
única palanca que queda, y es la clase F6/F9/F8.

Inventario de mecánicas de tick, cerrado. Separando lo que MC 1.8.9 muestrea por
**estado** (vulnerable a sub-tick) de lo que procesa por **evento** (no se pierde):

| Tecla | Muestreo | Garantía |
|---|---|---|
| Espacio | estado | **F6 + F9**, las dos. Hecho |
| W | estado | Solo OFF (**F6**). Su mecánica la dispara que se vea el OFF; un ON invisible cuesta un tick de movimiento, molesto pero no rompe mecánica. Extender su ON sería movimiento no pedido, mortal en un borde de sumo |
| S | estado | **F8**, condicionado a s-tapear y al tipo de par SOCD |
| A / D | estado | Ninguna: se sostienen ≫50 ms y el null-bind lo hace SOCD |
| LShift | estado | Ninguna: un unshift no visto al bridgear es un fallo seguro |
| LCtrl (sprint) | estado | Ninguna: se sostiene, no se tapea |
| 1-9, Q, E, chat | **evento** | Ninguna: LWJGL los encola y el cliente los drena por frame, no por tick |

Con eso, lo único construible que queda es **F8**, y está condicionado. El pozo
está casi seco y eso es buena señal: la superficie útil es estrecha por
construcción — el tick deja una sola palanca, la mayoría del resto lo resuelve
Launcher, y lo que quedaría choca con el criterio #3 o el #4.

### Descartado con razón (para no volver a proponerlo)

- **Actuación condicional por modificador** (S más profunda con LShift
  sostenido). Contradice un principio ya establecido: el mismo por el que
  `ANALOG_BOTTOM_OUT_LEARN` está apagado en torneo, *«un rango dinámico que muta
  a mitad de partida contradice el objetivo de configuración inmutable»*. Y el
  switch se sentiría idéntico con el umbral movido, o sea hardware impredecible
  para el usuario. Si algún día estorban los pasos accidentales de S, la
  respuesta es subir S en Launcher, no que el firmware adivine el contexto.
- **Inyectar mistypes / aleatoriedad** para «ser impredecible». Lo prohíbe el
  criterio #3 palabra por palabra (sintetizar algo indistinguible de un error), y
  además no funciona: un teclado que falla a propósito es un teclado peor. La
  impredecibilidad ante un rival sale de las decisiones de movimiento, no del
  jitter del teclado.
- **Inferir la fase del tick del servidor** para alinear reportes: no hay canal,
  el teclado no sabe nada del juego.
- **Estirar el release de A/D** en cambios de strafe: forzaría ~55 ms de neutral
  en cada reversión.
- **Tuning del RT predictivo.** Adelanta 1–3 ms; en un tick de 50 ms la ganancia
  *esperada* es exactamente ese 1–3 ms (el tick no la amplifica en promedio, solo
  la vuelve grumosa: ~5% de las veces ganas un tick, el resto nada). Falla el #4
  estructuralmente igual que la latencia del barrido, y paga una tasa no nula de
  presses fantasma, que es el #3. Marcado como **juguete permanente de lab**, no
  candidato a torneo — para dejar de gastarle sesiones de medición. No se borra:
  `vel_ema` y su telemetría son datos útiles.

### Deuda técnica conocida

- **Parches en QMK core** (`quantum/action.c`, `action_util.c`,
  `tmk_core/protocol/report.c`): el refcount por keycode y modificador en el path
  de reporte. Arregla un bug real de upstream, pero cada rebase contra QMK va a
  doler. Está en su propio commit (`42cd346`) para poder bisecarlo o replicarlo.
- **La política y las teclas de la telemetría se resuelven contra
  `ANALOG_POLICY_LAYER`**, y la coincidencia de keycode es exacta: un `KC_W`
  envuelto en mod-tap o layer-tap no entra en las whitelists.
- **`ANALOG_DISABLE_OKMC/TOGGLE/GAMEPAD/PROFILE_COMBO_IN_GAMING_MODE`** siguen
  siendo firmware pisando config de Launcher. Es política deliberada (son los
  modos que sintetizan o enganchan input) y se mantiene a sabiendas, pero es la
  única excepción que queda a la regla de «el firmware no fija lo que la config
  puede fijar».

## Guardas del modo Gaming (recordatorio)

`process_record_user` bloquea en Gaming: macros VIA, layer-switch, cambio de
perfil, bootloader/reboot/EEPROM, power/sleep/wake del host (flashear =
interruptor en Win), QK_MAGIC, keycodes wireless. LGUI se queda en el keymap (KC_NO hardcodeado revertido el
19-jul: desactivar Win en Gaming es preferencia por tecla que Launcher ya
resuelve — el firmware no fija lo que la config puede fijar). Aplicando esa
misma regla se eliminaron el 24-jul dos **hardcodes dormidos** (inertes con la
config de hoy, y que se despertaban al mover un slider en Launcher sin que nada
lo indicase): `STATIC_HYSTERESIS_GAMING_FAST_KEY` + `ANALOG_GAMING_FAST_KEY_*`
(histéresis propia para el Espacio, elegida por coordenada de matriz; ya la
capaba `actn_pt/2` con actuación ≤ 0.4 mm, divergía sólo a partir de 0.6 mm) y
`ANALOG_GAMING_DEFAULT_RAPID_PROFILE` (clavaba Espacio y LShift en `AKM_RAPID`
explícito al cargar la EEPROM, así que un cambio posterior de modo global a
Regular ya no las movía). Los defaults de modo por tecla se quedan donde
corresponde: `default_profiles[]`, que es tabla de reset y por tanto pisable.
`config.h` desactiva en Gaming: OKMC, toggle, gamepad, combos de perfil (eso
sí es política deliberada — son los modos que sintetizan o enganchan input — y
sigue siendo firmware pisando config, a sabiendas). Canal Raw HID
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
- Las whitelists por keycode (continuous RT, RT predictivo, release-stretch) se
  resuelven en `update_travel_configs()`, o sea en boot, cambio de perfil y giro
  del interruptor. Un **remap en caliente desde Launcher no las recoloca hasta
  uno de esos tres eventos** — gira el interruptor a Win y vuelve, o reinicia.
- Las teclas vigiladas por la telemetría también se declaran por keycode
  (24-jul): era el último resto de coordenadas fijas del proyecto, y con la
  re-resolución en caliente se habría vuelto alcanzable de verdad (`--policy`
  mostrando la posición nueva y `--plot` leyendo la vieja, sin avisar). Se
  resuelven en `keyboard_post_init_user` y tras cada remap, contra
  `ANALOG_POLICY_LAYER`. Si un keycode no está en la capa base de Gaming, esa
  tecla reporta travel 0 en vez de leer fuera de rango.
- La coincidencia de keycode es **exacta**: un `KC_W` envuelto en mod-tap o
  layer-tap no entra en la whitelist. Y si dos posiciones mapean al mismo
  keycode, el release-stretch (que lleva estado por slot) se queda con la
  primera en orden de barrido; las máscaras predictivas marcan las dos.
- Compilar: MSYS2 MinGW64 (`qmk compile -kb keychron/k2_he/ansi -km alex`);
  qmk no está en el PATH de PowerShell. Ojo: en shell no interactivo
  `USERPROFILE` viene vacío y `qmk` muere con "Could not determine home
  directory" — exportarlo antes.

## Referencias

Algoritmos portados/auditados contra: [libhmk](https://github.com/peppapighs/libhmk)
(FSM RT, Null Bind, calibración solo-crece), [minipad](https://github.com/minipadKB/minipad-firmware)
(histéresis de zona asimétrica, arranque seguro), [macrolev](https://github.com/heiso/macrolev).
Historia upstream: ramas `hall_effect_playground` / `2025q3` de Keychron/qmk_firmware.
