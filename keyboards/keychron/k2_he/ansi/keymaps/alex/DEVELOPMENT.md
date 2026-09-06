# Desarrollo: binario de torneo vs experimental

Tres binarios del mismo teclado, misma lógica, distinto perfil de riesgo.

| Keymap | Binario | Para qué |
|---|---|---|
| `alex` | `keychron_k2_he_ansi_alex.bin` | **Torneo.** Mínimo, probado, sin instrumentación de debug ni features especulativas. Es el que flasheas para jugar en serio. |
| `alex_lab` | `keychron_k2_he_ansi_alex_lab.bin` | **Laboratorio.** Telemetría, histograma y experimentos encendidos — RT predictivo, stretches y probe de timing. Aquí se miden y se rompen cosas. |
| `alex_cal_lab` | `keychron_k2_he_ansi_alex_cal_lab.bin` | **Calibración reversible.** Hereda torneo, agrega telemetría y el learner robusto de bottom-out. No enciende los otros experimentos de `alex_lab`. |

## Cómo se relacionan (importante)

`alex_lab` **no duplica ni una línea de lógica**. Su estructura:
- `keymap.c` → `#include "../alex/keymap.c"` (misma lógica de teclado).
- `config.h` → `#include "../alex/config.h"` + los `#define` experimentales (el inventario de abajo es la lista canónica).
- `rules.mk` → incluye `rules-common.mk` y activa `telemetry.c`; `alex` la deja compilada fuera.

Consecuencia: **cualquier cambio de lógica se hace en `alex`** y `alex_lab` lo hereda automáticamente. Nunca editas `alex_lab` salvo para encender/apagar un flag experimental. No hay drift posible entre los dos.

## La regla de oro

`alex_cal_lab` también incluye `alex/keymap.c`, pero hereda directamente
`alex/config.h`: su única feature de comportamiento es
`ANALOG_CONFIDENT_BOTTOM_OUT_ENABLE`. Así se puede juzgar la calibración sin RT
predictivo ni stretches actuando al mismo tiempo.

Todo código experimental que viva en `common/` (el core analógico compartido) **debe ir detrás de un `#if FLAG`**, apagado por defecto. Así el binario de torneo lo compila **fuera** — no basta con apagarlo en runtime, porque el código seguiría ocupando flash y ciclos.

Mal (código vivo aunque el flag esté off):
```c
k->vel_ema = ...;   // corre siempre, cuesta en el hot path
```
Bien (se compila fuera cuando el flag está off):
```c
#if ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE
    k->vel_ema = ...;
#endif
```

La prueba de que quedó bien aislado: el **delta de tamaño** entre los dos `.bin`. Si `alex` no creció al añadir la feature, está fuera de verdad.

Referencia actual (1-ago): **`alex` 53200 B**, **`alex_lab` 58484 B** y
**`alex_cal_lab` 57436 B**.

Y ya no se comprueba a ojo. `tools/check-size-invariant.sh` lleva las dos
baselines y **falla el build si cualquiera se mueve**, en cualquier dirección —
crecer puede ser una feature de lab fugada, encoger puede ser algo que se compiló
fuera sin querer. Subir la baseline es un commit deliberado con el motivo escrito,
que es exactamente la declaración que antes sólo existía en la cabeza de quien
compilaba.

```bash
make keychron/k2_he/ansi:alex
make keychron/k2_he/ansi:alex_lab
make keychron/k2_he/ansi:alex_cal_lab
./keyboards/keychron/k2_he/ansi/keymaps/alex/tools/check-size-invariant.sh
```

No pongas keymaps con `config.h` distintos como varios objetivos de una sola
invocación `make`: este fork puede conservar defines LTO entre objetivos. Usa
una invocación separada por keymap, como arriba.

Matiz importante sobre el invariante: sólo aplica a **features experimentales o
diagnósticos detrás de un flag**. No aplica a arreglos de corrección en código
que está en torneo a propósito. Telemetría e histograma son ahora exclusivos de
`alex_lab`; si cualquiera de sus símbolos aparece en `alex`, es una fuga. Si
`alex` crece, la pregunta no es «¿cuánto?» sino **«¿es una feature de lab o un
arreglo de algo que ya estaba?»**.

## Cómo añadir una feature experimental nueva

1. Escribe el código en `common/` detrás de `#if MI_FLAG`.
2. Dale default 0 en `analog_matrix.h` con `#ifndef` (para que `alex` lo deje apagado sin tocar nada):
   ```c
   #ifndef MI_FLAG
   #    define MI_FLAG 0
   #endif
   ```
   No lo fuerces en `k2_he/config.h` — deja que el default lo apague y que el keymap lo encienda.
3. Enciéndelo en `alex_lab/config.h`: `#define MI_FLAG 1`.
4. Compila **ambos** y mira el delta de tamaño = tu código aislado.
5. Prueba en `alex_lab`, mide con telemetría.
6. **Promoción a torneo** (solo si pasa los criterios de abajo): mueve el `#define MI_FLAG 1` a `k2_he/config.h` (lo heredan ambos) o al `config.h` de `alex`.

## Build

```bash
# Torneo
qmk compile -kb keychron/k2_he/ansi -km alex
# Laboratorio
qmk compile -kb keychron/k2_he/ansi -km alex_lab
# Calibración reversible
qmk compile -kb keychron/k2_he/ansi -km alex_cal_lab
```
(vía MSYS2 MinGW64; `qmk` no está en el PATH de PowerShell).

## Inventario de flags

| Flag | `alex` | `alex_lab` | Qué hace |
|---|---|---|---|
| `ANALOG_SCAN_PIPELINE` | 1 | 1 | Procesa columna previa durante el settle (barrido más corto) |
| `ANALOG_SCAN_SOF_SYNC` | 1 | 1 | Ancla el barrido al SOF USB (elimina jitter de fase) |
| `ANALOG_RUNTIME_CONFIG_CACHE` | **1** | **1** | Resuelve una vez por reconfiguración el modo efectivo y los knobs del camino caliente |
| `ANALOG_SOCD_RUNTIME_COMPACT` | **1** | **1** | Recorre sólo pares SOCD activos en cada barrido |
| `ANALOG_PROFILE_SANITIZER_ENABLE` | **1** | **1** | Canonicaliza perfiles EEPROM antes de usarlos, sin cambiar layout ni escribir durante boot |
| report rate USB | 1 kHz | 1 kHz | Fijo por descriptor (`bInterval=1`); no existe flag runtime |
| `ANALOG_BOTTOM_OUT_LEARN` | **0** | **1** | Aprende bottom-out por tecla, solo-crece (torneo lo apaga: drift descartado con datos, config inmutable) |
| `ANALOG_SOCD_DEEPER_HYSTERESIS` | 6 | 6 | Histéresis del Rappy Snappy (anti-chatter A/D) |
| `ALEX_TELEMETRY_ENABLE` | **no** | **sí** | Raw HID de diagnóstico, travel y event logger |
| `ANALOG_WINDOW_HISTOGRAM` | **0** | **1** | Histograma ON/OFF y salud; hook por tecla sólo en lab |
| `USB_SOF_TIMING_PROBE` | **no** | **sí** | Instrumentación de duración/fase del barrido |
| `ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE` | **0** | **1** | RT predictivo por velocidad (especulativo) |
| `ANALOG_RELEASE_STRETCH_IN_GAMING_MODE` | **0** | **1** | F6: OFF reportado ≥55 ms tras release físico de W/SPC (el tick de 50 ms de MC siempre lo ve) |
| `ANALOG_PRESS_STRETCH_IN_GAMING_MODE` | **0** | **1** | F9: ON reportado ≥55 ms tras press físico de SPC. Espejo de F6 y la otra mitad de `jumpTicks` — F6 arregla el release no visto (siguiente salto hasta 500 ms tarde), F9 el press no visto (el salto no existió). Los dos activos en SPC dan 110 ms de ciclo en el peor caso, contra los 500 ms de hoy |
| `ANALOG_PREDICTIVE_PRESS_KEY_MASK` / `_REPRESS_KEY_MASK` | 0x3F (inertes) | **0x29 / 0x28** | F7: whitelist predictiva por camino — press SPC+A+D; re-press solo A+D (con F6, predecir el re-press de SPC no adelanta nada y difiere el fantasma) |

El timestamp del SOF lo provee `usb_main.c` mientras `ANALOG_SCAN_SOF_SYNC` **o** `USB_SOF_TIMING_PROBE` estén activos, así que el torneo tiene sync sin arrastrar el probe.

`alex_cal_lab` conserva los valores de `alex` en esta tabla y sólo cambia dos
cosas: telemetría Raw HID = sí y `ANALOG_CONFIDENT_BOTTOM_OUT_ENABLE = 1`.
`ANALOG_BOTTOM_OUT_LEARN` permanece en 0 porque el learner viejo aplica durante
el uso y puede persistir; mezclar ambos rompería el rollback.

## Experimento de calibración reversible

El learner de `alex_cal_lab` toma **una** muestra por pulsación que alcanza la
zona profunda y vuelve a soltar; sostener la tecla no infla la confianza. Con
siete muestras ordena la ventana, ignora un extremo por lado, exige que las
cinco centrales abarquen como máximo 40 cuentas raw y que la mediana mejore el
fondo actual por más de 30 cuentas. Hasta entonces no cambia nada.

```bash
python keyboards/keychron/k2_he/ansi/keymaps/alex/tools/telemetry_client.py --cal-status
python keyboards/keychron/k2_he/ansi/keymaps/alex/tools/telemetry_client.py --cal-apply
python keyboards/keychron/k2_he/ansi/keymaps/alex/tools/telemetry_client.py --cal-revert
python keyboards/keychron/k2_he/ansi/keymaps/alex/tools/telemetry_client.py --cal-clear
```

`--cal-apply` sólo funciona fuera de Gaming y modifica exclusivamente RAM.
`--cal-revert` funciona también en Gaming. Reiniciar o flashear otro build
revierte aunque no se ejecute el cliente: EEPROM nunca recibe el candidato.

## Criterio de promoción lab → torneo

Una feature graduá de `alex_lab` a `alex` solo si:
1. **Validada con datos**, no con sensación (telemetría, no "se siente mejor").
2. **Sin regresión** de latencia ni de jitter de fase (medir el probe antes/después).
3. **Sin riesgo de input fantasma** — nada que sintetice o adelante pulsaciones de forma que un anticheat o tú mismo no puedan distinguir de un error.
4. **Para MC 1.8.9**: beneficio real por encima del tick de servidor de 50 ms. Ganar sub-milisegundos no cuenta.

Lo que no pase los 4 se queda en `alex_lab` como juguete, no como firmware de torneo.

**El #4 es el que más ha matado ideas, y conviene tenerlo a mano en su forma
aritmética**: un tick de MC 1.8.9 son 50 ms = **50 frames USB**. Cualquier mejora
de µs o de pocos ms es sub-tick, y su ganancia *esperada* es exactamente su
tamaño — el tick no la amplifica en promedio, solo la vuelve grumosa. Por eso el
único trabajo que puede graduarse es el que cambia **si el juego ve el input o
no** (F6/F9/F8), no el que lo hace llegar antes.

**El #3 no es una puerta de promoción, es una puerta de «esto lo corro en un
servidor real»**. Una feature puede fallar el #1, #2 o #4 y seguir siendo un
experimento legítimo en `alex_lab`. Si falla el #3, no se construye ni ahí.

## Guardas del modo Gaming

`process_record_user` bloquea en Gaming: macros VIA, layer-switch, cambio de
perfil, bootloader/reboot/EEPROM, power/sleep/wake del host (flashear =
interruptor en Win), QK_MAGIC, keycodes wireless. LGUI **no** se bloquea: se
queda en el keymap, porque desactivar Win en Gaming es preferencia por tecla y
Launcher ya la resuelve — el firmware no fija lo que la config puede fijar.

`config.h` sí desactiva en Gaming OKMC, toggle, gamepad y combos de perfil. Es
la única excepción deliberada a esa regla, y se mantiene a sabiendas: son los
modos que **sintetizan o enganchan input**.

Canal Raw HID en Gaming: blacklist mínima (`RESET_PROFILE` y `CALIBRATE`). La
whitelist de solo-lectura se probó y se revirtió el mismo día — el tuning real
exige el modo Gaming activo (el perfil gaming sólo corre ahí) y el lockdown
rompía ese flujo. Si el endurecimiento vuelve, el diseño aparcado es la
**escotilla de tuning**: desbloqueo por `0xEE` con timeout ~10 min y recierre al
girar el interruptor. Nunca una whitelist permanente.

SOCD está desbloqueado en Gaming (Rappy Snappy). Zona gris: no prohibido
explícitamente en Minemen/Hypixel, baneado en CS2/ESL.

## Gotchas conocidos

- Las whitelists por keycode (continuous RT, RT predictivo, release/press
  stretch), las teclas vigiladas por telemetría y las del histograma se resuelven contra
  `ANALOG_POLICY_LAYER` en `update_travel_configs()` — boot, cambio de perfil y
  giro del interruptor — más la re-resolución en caliente tras un remap de VIA.
  **La coincidencia de keycode es exacta**: un `KC_W` envuelto en mod-tap o
  layer-tap no entra. Si un keycode no está en la capa base de Gaming, esa tecla
  reporta travel 0 en vez de leer fuera de rango. Si dos posiciones dan el mismo
  keycode, el release-stretch (estado por slot) se queda con la primera en orden
  de barrido; las máscaras predictivas marcan las dos.
- El cliente de telemetría apaga el diagnóstico en `finally` al salir, cerrar la
  gráfica o fallar. Cualquier comando de Launcher también lo auto-apaga (comparten
  endpoint).
- Los defaults RT por perfil sólo aplican en perfiles **reseteados**; la EEPROM
  con valores de Launcher siempre gana.
- El bottom-out aprendido nunca se encoge: al cambiar un switch por otro de imán
  más débil, recalibrar a mano desde Launcher.
- Compilar en shell no interactivo: MSYS2 vacía `USERPROFILE` y `qmk` muere con
  *"Could not determine home directory"*. Ruta que funciona desde Windows:
  ```
  C:\msys64\msys2_shell.cmd -mingw64 -defterm -no-start -where C:\Users\Alex\keychron-qmk -c "make keychron/k2_he/ansi:alex"
  ```

## Deuda técnica conocida

- **Parches en QMK core** (`quantum/action.c`, `action_util.c`,
  `tmk_core/protocol/report.c`): refcount por keycode y modificador en el path de
  reporte. Arregla un bug real de upstream, pero cada rebase contra QMK va a
  doler. Aislado en su propio commit (`42cd346`) para bisecarlo o replicarlo.
  Añadido a esa lista: `usb_main.c` exporta `usb_sof_timing_last_cycles`, del que
  cuelga todo el SOF-sync.
- **El repo es un clon shallow.** `merge-base` no resuelve y cualquier merge
  contra Keychron da *"refusing to merge unrelated histories"*. Hace falta
  `git fetch --unshallow` contra el remoto de Keychron (no contra `origin`, que
  es el fork propio).
- **QMK mainline va 3 ciclos de breaking changes por delante** del árbol
  (`20250831` aquí vs `20260531` en master). Eso lo arrastra Keychron, no
  nosotros, pero fija el coste de cualquier rebase futuro.

## Estabilización de arranque (31-ago-2026)

La primera fase para el síntoma de teclas que se traban al iniciar Windows ya
está integrada en el camino común del K2 HE:

- Después de bootmagic, el firmware mantiene la matriz analógica silenciosa
  durante **150 ms** y exige **8 barridos ADC válidos** consecutivos. USB sigue
  activo, pero no se publican transiciones físicas ni virtuales durante esa
  ventana; una tecla mantenida se evalúa de nuevo al terminarla.
- La calibración de reposo de power-on puede reintentar **3 ventanas completas**
  si una tecla está mantenida o el ADC entrega un valor anómalo. Si no converge,
  conserva la calibración anterior/default y no deja el escaneo bloqueado.
- El learner de bottom-out, el guardado de calibración y los despachos de HID
  virtual pasan a `housekeeping`; las escrituras de EEPROM sólo ocurren tras
  250 ms de coalescencia y 1 s sin actividad. Un fallo de I2C reintenta con
  backoff de 5 s, no en cada vuelta del escaneo.

Lo que **no** se promociona en esta fase: predicción RT, stretches F6/F9, un
filtro de ruido adaptativo permanente y una reingeniería de la inicialización
I2C. Esas piezas siguen siendo experimentales o requieren una medición de
hardware; el binario recomendado para empezar es `alex`.

### Prueba de aceptación en hardware

1. Flashear `alex`, desconectar y conectar el teclado en frío **20 veces**, sin
   tocar teclas durante el primer segundo.
2. En cada arranque, probar `W`, `A`, `S`, `D`, espacio y Shift justo después de
   que Windows enumere el dispositivo; anotar cualquier tecla fantasma,
   release perdido o primera pulsación ignorada.
3. Repetir 10 ciclos de suspensión/despertar de Windows y separar esos resultados
   de los de arranque en frío.
4. Aceptar sólo si no hay eventos fantasma/pegados y la primera pulsación
   deliberada funciona; si falla, repetir con `alex_lab` activando temporalmente
   la telemetría/probes para distinguir sensor, I2C y fase USB.

No se flashea ningún binario automáticamente desde este flujo: la validación
final depende del teclado físico y de la versión de Windows que presenta el
síntoma.

## Tests de host

`common/analog_matrix/hosttest/` compila los `.c` REALES (FSM del rapid trigger,
F6/F9, resolución por keycode, histograma) contra shims de QMK, con reloj y
keymap falsos. Regla del directorio: **no se copia lógica del firmware**; si un
test pasa, es sobre el mismo código que corre en el teclado.

```bash
cd keyboards/keychron/common/analog_matrix/hosttest && make run
```

Las suites del camino de input se compilan con las dos configuraciones; en
torneo comprueban además que los stretches son passthrough. El histograma se
compila y ejecuta sólo en la configuración lab, igual que en los binarios reales.
Necesita gcc de host (`pacman -S mingw-w64-x86_64-gcc`).

## Histograma de ventanas

Vive sólo en **`alex_lab`**. Cuenta duraciones de ventana ON/OFF en cubos
cortados en el tick de 50 ms, en capa física y reportada. Comandos
`0xEE 0x30/0x31/0x32`. La implementación permanece en `common/` detrás de flag:
si sus resultados justifican una corrección, se promociona esa corrección al
estable, no la instrumentación. Racional completo en
`common/analog_matrix/window_histogram.h`.

## Auditoría del batch del 1-ago

`AUDITORIA.md` — qué revisar de las 6 olas, ordenado por riesgo, con lo que está
verificado y lo que no.

## Historial

`historial/ROADMAP-2026-07-12_25-cerrado.md` — registro congelado de las fases
F0-F9, la tabla antes/después de la cadena de latencia, el inventario de
mecánicas de tick y la lista de ideas descartadas con su razón. Se consulta antes
de proponer algo «nuevo»: la mayoría ya está descartada ahí, con motivo.
