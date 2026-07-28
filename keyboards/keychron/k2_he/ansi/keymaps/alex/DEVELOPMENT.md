# Desarrollo: binario de torneo vs experimental

Dos binarios del mismo teclado, misma lógica, distinto perfil de riesgo.

| Keymap | Binario | Para qué |
|---|---|---|
| `alex` | `keychron_k2_he_ansi_alex.bin` | **Torneo.** Mínimo, probado, sin instrumentación de debug ni features especulativas. Es el que flasheas para jugar en serio. |
| `alex_lab` | `keychron_k2_he_ansi_alex_lab.bin` | **Laboratorio.** Todo encendido — RT predictivo, probe de timing. Aquí se rompen cosas. |

## Cómo se relacionan (importante)

`alex_lab` **no duplica ni una línea de lógica**. Su estructura:
- `keymap.c` → `#include "../alex/keymap.c"` (misma lógica de teclado).
- `config.h` → `#include "../alex/config.h"` + los `#define` experimentales (el inventario de abajo es la lista canónica).
- `rules.mk` → incluye `rules-common.mk`; solo cambia la ruta a `telemetry.c`.

Consecuencia: **cualquier cambio de lógica se hace en `alex`** y `alex_lab` lo hereda automáticamente. Nunca editas `alex_lab` salvo para encender/apagar un flag experimental. No hay drift posible entre los dos.

## La regla de oro

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

Referencia actual (28-jul): **`alex` 54296 B**, **`alex_lab` 56680 B**.

Y ya no se comprueba a ojo. `tools/check-size-invariant.sh` lleva las dos
baselines y **falla el build si cualquiera se mueve**, en cualquier dirección —
crecer puede ser una feature de lab fugada, encoger puede ser algo que se compiló
fuera sin querer. Subir la baseline es un commit deliberado con el motivo escrito,
que es exactamente la declaración que antes sólo existía en la cabeza de quien
compilaba.

```bash
make keychron/k2_he/ansi:alex keychron/k2_he/ansi:alex_lab
./keyboards/keychron/k2_he/ansi/keymaps/alex/tools/check-size-invariant.sh
```

Matiz importante sobre el invariante: sólo aplica a **features experimentales
detrás de un flag**. No aplica a arreglos de corrección en código que está en
torneo a propósito — la telemetría, por ejemplo, que vive en los dos binarios
porque la comparación torneo/lab necesita medir ambos. El 24-jul `alex` creció de
53952 a 54288 B al resolver las teclas vigiladas por keycode en vez de por
coordenada fija; eso es correcto y no una fuga. Si `alex` crece, la pregunta no es
«¿cuánto?» sino **«¿es una feature de lab o un arreglo de algo que ya estaba?»**.

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
```
(vía MSYS2 MinGW64; `qmk` no está en el PATH de PowerShell).

## Inventario de flags

| Flag | `alex` | `alex_lab` | Qué hace |
|---|---|---|---|
| `ANALOG_SCAN_PIPELINE` | 1 | 1 | Procesa columna previa durante el settle (barrido más corto) |
| `ANALOG_SCAN_SOF_SYNC` | 1 | 1 | Ancla el barrido al SOF USB (elimina jitter de fase) |
| report rate USB | 1 kHz | 1 kHz | Fijo por descriptor (`bInterval=1`); no existe flag runtime |
| `ANALOG_BOTTOM_OUT_LEARN` | **0** | **1** | Aprende bottom-out por tecla, solo-crece (torneo lo apaga: drift descartado con datos, config inmutable) |
| `ANALOG_SOCD_DEEPER_HYSTERESIS` | 6 | 6 | Histéresis del Rappy Snappy (anti-chatter A/D) |
| `ALEX_TELEMETRY_ENABLE` | sí | sí | Diagnóstico explícito; arranque/parada por comando HID 0xEE |
| `USB_SOF_TIMING_PROBE` | **no** | **sí** | Instrumentación de duración/fase del barrido |
| `ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE` | **0** | **1** | RT predictivo por velocidad (especulativo) |
| `ANALOG_RELEASE_STRETCH_IN_GAMING_MODE` | **0** | **1** | F6: OFF reportado ≥55 ms tras release físico de W/SPC (el tick de 50 ms de MC siempre lo ve) |
| `ANALOG_PRESS_STRETCH_IN_GAMING_MODE` | **0** | **1** | F9: ON reportado ≥55 ms tras press físico de SPC. Espejo de F6 y la otra mitad de `jumpTicks` — F6 arregla el release no visto (siguiente salto hasta 500 ms tarde), F9 el press no visto (el salto no existió). Los dos activos en SPC dan 110 ms de ciclo en el peor caso, contra los 500 ms de hoy |
| `ANALOG_PREDICTIVE_PRESS_KEY_MASK` / `_REPRESS_KEY_MASK` | 0x3F (inertes) | **0x29 / 0x28** | F7: whitelist predictiva por camino — press SPC+A+D; re-press solo A+D (con F6, predecir el re-press de SPC no adelanta nada y difiere el fantasma) |

El timestamp del SOF lo provee `usb_main.c` mientras `ANALOG_SCAN_SOF_SYNC` **o** `USB_SOF_TIMING_PROBE` estén activos, así que el torneo tiene sync sin arrastrar el probe.

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
  stretch) y las teclas vigiladas por la telemetría se resuelven contra
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

## Historial

`historial/ROADMAP-2026-07-12_25-cerrado.md` — registro congelado de las fases
F0-F9, la tabla antes/después de la cadena de latencia, el inventario de
mecánicas de tick y la lista de ideas descartadas con su razón. Se consulta antes
de proponer algo «nuevo»: la mayoría ya está descartada ahí, con motivo.
