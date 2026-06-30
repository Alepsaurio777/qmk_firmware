# Keychron K2 HE ANSI - Firmware Alex

Fecha del documento: 2026-06-29

Este documento resume los cambios hechos sobre el firmware Keychron/QMK del
Keychron K2 HE ANSI, la logica tecnica detras de cada cambio, los binarios
generados y como probarlos.

## Objetivo

El objetivo principal es construir un firmware para uso competitivo/gaming con:

- Baja latencia de escaneo analogico.
- Compatibilidad con Keychron Launcher.
- Modo Gaming mas puro y predecible.
- Menos codigo de fondo innecesario.
- Correcciones defensivas de bugs reales sin romper el layout de EEPROM ni el
  protocolo del Launcher.

El objetivo no es convertir todos los perfiles en un comportamiento hardcodeado.
El Launcher sigue siendo la fuente de configuracion para actuation distance,
Rapid Trigger y perfiles.

## Estado actual del baseline

El archivo principal actual es:

```text
C:\Users\Alex\keychron-qmk\keychron_k2_he_ansi_alex.bin
```

Este archivo quedo como baseline normal, no experimental.

Configuracion actual relevante:

```c
#define ANALOG_ADC_SAMPLE_TIME ADC_SAMPLE_28
#define ANALOG_SELECT_SETTLE_US 20
#define ANALOG_FIXED_POINT_TRAVEL 1
#define ANALOG_AUTO_CALIBRATION_ENABLE 0
#define ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE 0
```

Y en el keymap:

```make
RGB_MATRIX_ENABLE = no
LK_WIRELESS_ENABLE = no
KC_BLUETOOTH_ENABLE = no
DEBOUNCE_TYPE = none
LTO_ENABLE = yes
```

## Debloat y build

### Cambios

- `RGB_MATRIX_ENABLE = no`
- `KEYCHRON_RGB_ENABLE = no`
- `LK_WIRELESS_ENABLE = no`
- `KC_BLUETOOTH_ENABLE = no`
- Factory Test apagado.
- LTO habilitado.
- Debounce QMK desactivado con `DEBOUNCE_TYPE = none`.
- Debounce analogico minimo con `ANALOG_DEBOUNCE_TIME = 1`.

### Logica

RGB, wireless, factory test y debounce generico no aportan al objetivo de
latencia minima en modo USB/gaming. Quitarlos reduce interrupciones, trabajo de
fondo, uso de flash/RAM y posibles rutas accidentales.

El debounce clasico no es la herramienta correcta para un teclado Hall Effect:
el estado real depende de travel analogico, actuation point, release point,
histeresis y Rapid Trigger.

`ANALOG_DEBOUNCE_TIME = 1` no significa debounce QMK clasico. Es una sola
pasada del loop analogico por columna, el minimo seguro para esta implementacion.
Se agrego una guarda de compilacion para prohibir `0`, porque el contador
`uint8_t` del `do/while` podria hacer underflow y repetir hasta 255 lecturas si
hay un cambio de estado.

## Modo Gaming puro

### Cambios

En Gaming se bloquean:

- Macros VIA.
- Cambios de capa y FN.
- Seleccion directa de perfiles `PROF1..PROF3` desde `process_record_user()`.
- Combo interno de perfiles por `virtual_matrix` en `profile.c`.
- `QK_BOOTLOADER`.
- `QK_REBOOT`.
- `QK_CLEAR_EEPROM`.
- `QK_MAGIC`, incluyendo toggles y swaps como GUI/NKRO/Ctrl/Alt.
- Keycodes wireless/battery cuando wireless esta habilitado.

Tambien se desactivan modos avanzados dentro de Gaming:

```c
#define ANALOG_DISABLE_OKMC_IN_GAMING_MODE 1
#define ANALOG_DISABLE_TOGGLE_IN_GAMING_MODE 1
#define ANALOG_DISABLE_GAMEPAD_IN_GAMING_MODE 1
#define ANALOG_DISABLE_SOCD_IN_GAMING_MODE 1
#define ANALOG_DISABLE_PROFILE_COMBO_IN_GAMING_MODE 1
```

### Logica

Gaming debe comportarse como un bloque de teclas directo, sin rutas que puedan
cambiar perfiles, entrar a bootloader, borrar EEPROM, lanzar macros o mutar el
layout durante una partida.

SOCD se dejo desactivado en Gaming por decision de diseno: el teclado envia las
direcciones reales y el juego decide que hacer.

`FN_GAMING` se dejo como `KC_NO`. La capa `GAMING_FN` permanece reservada para
mantener el orden de capas, pero no se puede alcanzar desde el layout Gaming.

## Motor analogico y latencia

### Fixed-point travel

Se agrego `ANALOG_FIXED_POINT_TRAVEL = 1`.

La conversion ADC -> travel ya no usa float en el hot path. Se usa una version
fixed-point Q16 del delta polinomial y `scale_factor_q16`.

### Logica

El Cortex-M4 tiene FPU, pero usar fixed-point en el scan reduce variacion y
trabajo por tecla. El FPU sigue existiendo para calculos fuera del hot path,
por ejemplo al recalcular `scale_factor` al boot o al cambiar perfiles.

No se elimino el array `scale_factor` float porque el Launcher lo usa para
diagnostico/calibracion. Esto cuesta RAM, pero conserva compatibilidad.

### `auto_calib` fuera del build normal

Con `ANALOG_AUTO_CALIBRATION_ENABLE 0`, el array `auto_calib` y sus escrituras
asociadas no tienen lectores reales en runtime. Se compilaron fuera mediante
guardas `#if`, dejando `auto_calibration_init()` como stub vacio cuando la
auto-calibracion esta desactivada.

Logica:

- No cambia la calibracion manual del Launcher.
- No cambia `saved_calib_values`, `calib_values` ni `scale_factors`.
- Elimina codigo/estado muerto del build normal y experimental actual.

## Timing del scan

### Baseline actual

```c
#define ANALOG_ADC_SAMPLE_TIME ADC_SAMPLE_28
#define ANALOG_SELECT_SETTLE_US 20
```

Con ADC a 18 MHz:

- ADC sample 28: `(28 + 12) / 18 MHz = 2.22 us` por canal.
- 6 canales: aprox `13.3 us` por columna.
- Settle: `20 us` por columna.
- 16 columnas: aprox `0.53 ms` solo en settle + conversion ADC.

### Delays configurables del 74HC164

Se agregaron defaults configurables en `analog_matrix_scan.c`:

```c
#ifndef HC164_DELAY_NOPS
#    define HC164_DELAY_NOPS 50
#endif

#ifndef HC164_RESET_DELAY_NOPS
#    define HC164_RESET_DELAY_NOPS 20
#endif
```

### Logica

Antes el delay del shift register estaba hardcodeado. Ahora se puede compilar
una variante de prueba bajando esos NOPs sin tocar la logica del driver.

Esto permite probar si el 74HC164 y la matriz analogica toleran pulsos mas
rapidos sin crosstalk ni columnas fantasma.

## Builds de timing

Se generaron dos builds normales, sin Continuous RT experimental:

### Timing Safe

Archivo:

```text
C:\Users\Alex\keychron-qmk\K2HE_ALEX_TIMING_SAFE_adc28_settle15_hc164x5_noContinuousRT.bin
```

Configuracion usada temporalmente al compilar:

```c
#define ANALOG_ADC_SAMPLE_TIME ADC_SAMPLE_28
#define ANALOG_SELECT_SETTLE_US 15
#define HC164_DELAY_NOPS 5
#define HC164_RESET_DELAY_NOPS 5
```

Logica:

- Baja el settle de `20 us` a `15 us`.
- Mantiene ADC en `ADC_SAMPLE_28`.
- Acelera el pulso del shift register.
- Es la primera build que se debe probar.

### Timing Aggressive

Archivo:

```text
C:\Users\Alex\keychron-qmk\K2HE_ALEX_TIMING_AGGRESSIVE_adc15_settle10_hc164x5_noContinuousRT.bin
```

Configuracion usada temporalmente al compilar:

```c
#define ANALOG_ADC_SAMPLE_TIME ADC_SAMPLE_15
#define ANALOG_SELECT_SETTLE_US 10
#define HC164_DELAY_NOPS 5
#define HC164_RESET_DELAY_NOPS 5
```

Logica:

- Baja el settle a `10 us`.
- Baja sample time del ADC a `ADC_SAMPLE_15`.
- Es mas rapida, pero puede revelar ruido analogico, lecturas nerviosas o
  crosstalk si el hardware necesita mas tiempo de asentamiento.

## Calibracion power-on

### Cambios

Con `ANALOG_AUTO_CALIBRATION_ENABLE = 0`, se corrigio el problema donde una
deriva pequena de reposo al encender podia quedarse toda la sesion por el umbral
de 30 unidades.

Ahora, durante `CALIB_ZERO_TRAVEL_POWER_ON`:

- Se acepta la correccion de `zero_travel` aunque el delta sea menor a 30.
- Si existe `CALI_FULL_TRAVEL`, `full_travel` se desplaza por el mismo delta.
- `full_travel` se clampa dentro del rango ADC valido.
- Se recalculan `scale_factors` en el path RAM-only.

### Logica

El auto-calibrador stock corregia deriva termica durante la sesion. Como fue
desactivado para evitar trabajo de fondo, se necesitaba una correccion segura al
arranque.

Este ajuste:

- Corre una vez al boot.
- Toca RAM, no guarda EEPROM en el caso normal.
- Preserva calibracion manual del Launcher desplazando `zero` y `full` juntos.
- Evita wrap silencioso del bitfield de 12 bits con aritmetica signed y clamp.

## Histeresis, top-out y filtro analogico

### Cambios

```c
#define TOP_OUT_DEAD_ZONE_GAMING 12
#define TOP_OUT_DEAD_ZONE_TYPING 20
#define STATIC_HYSTERESIS_GAMING 5
#define STATIC_HYSTERESIS_GAMING_FAST_KEY 2
#define STATIC_HYSTERESIS_TYPING 5
#define ANALOG_RAW_NOISE_FILTER_GAMING 5
#define ANALOG_RAW_NOISE_FILTER_TYPING 5
#define ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING 1
```

### Logica

La zona top-out filtra vibracion fisica al soltar la tecla. Gaming usa menos
zona muerta para mantener respuesta rapida; typing usa mas margen para comodidad.

La histeresis adaptativa evita que una tecla configurada con actuation muy bajo
tenga que volver casi a `0.0 mm` para liberar. Esto ayuda especialmente con
teclas como `A` cuando el actuation esta muy agresivo.

## Rapid Trigger

### Baseline

El baseline conserva Rapid Trigger estandar de Keychron/Launcher. El firmware no
fuerza RT global ni decide por su cuenta que teclas tienen RT.

### Experimental Continuous RT

Existe una variante experimental ya compilada:

```text
C:\Users\Alex\keychron-qmk\K2HE_ALEX_EXPERIMENT_CONTINUOUS_RT_space_lshift_autoCalibClean_adc28_settle20_hc164stock_powerOnCal_security_launcher.bin
```

Tambien existe una variante mas agresiva que combina Continuous RT con
actuacion predictiva inicial:

```text
C:\Users\Alex\keychron-qmk\K2HE_ALEX_EXPERIMENT_CONTINUOUS_RT_PREDICTIVE_space_lshift_adc28_settle20_hc164stock_powerOnCal_security_launcher.bin
```

La logica experimental solo actua si:

- El modo Gaming esta activo.
- La tecla esta en modo Rapid Trigger por configuracion del Launcher.
- La tecla esta en whitelist: Space o Left Shift.

La actuacion predictiva no cambia el punto de actuation guardado por Launcher.
Solo adelanta el primer `pressed` si la tecla esta bajando rapido y ya esta a
menos de `ANALOG_PREDICTIVE_ACTUATION_ADVANCE` del punto configurado. En este
experimento el adelanto es conservador:

```c
#define ANALOG_PREDICTIVE_ACTUATION_ADVANCE TRAVEL_SCALE
#define ANALOG_PREDICTIVE_ACTUATION_MIN_DELTA TRAVEL_SCALE
```

Eso equivale aproximadamente a `0.1 mm` de adelanto y exige una bajada minima
de `0.1 mm` entre muestras para evitar disparos por ruido.

### Logica

No se quiso hardcodear Rapid Trigger global porque eso rompe la expectativa del
Launcher. El usuario debe poder activar/desactivar RT por tecla desde la web.

Continuous RT se dejo separado porque puede sentirse mejor en Space/Shift, pero
tambien puede introducir dobles eventos o comportamiento mas sensible. La
prediccion inicial pertenece a esa misma familia: puede ganar tiempo efectivo,
pero acepta mas riesgo de false trigger si la tecla vibra o si el usuario roza
la tecla sin terminar de presionarla.

## Seguridad y robustez

### Raw HID

Se corrigio un OOB/offset al responder matriz analogica via Raw HID.

Tambien se agrego un `return false` defensivo en el comando `0xAA` para evitar
fall-through a `0xAB` si Factory Test se reactiva en otro build.

### Factory Test

Aunque esta apagado en este build, se corrigieron:

- Copia de UID del MCU con aritmetica de puntero incorrecta.
- Nibble duplicado en version reportada.

### Limpiezas compartidas

Se corrigieron detalles pequenos en codigo comun que no afectan el hot path del
K2 HE, pero evitan reportes repetidos en auditorias y dejan el firmware mas
defensivo:

- `backlit_indicator.c`: al despertar de suspend con WinLock, el indicador RGB
  usa `idx_list[i]` en vez de `i`.
- `keychron_raw_hid.c`: la version de firmware se escribe nibble por nibble en
  vez de usar `itoa()` sobre posiciones solapadas.
- `analog_matrix.c`: la lectura de calibracion desde EEPROM usa
  `sizeof(saved_calib_values)` para expresar el tamano real del destino.
- `analog_matrix.c`: al guardar calibracion en EEPROM externa, se invalida el
  flag, se escribe el payload y solo despues se marca como valido.
- `profile.c`: se corrigio un comentario de `PROF_2_KEY_COL` etiquetado como
  Profile 3.
- `sqrt.c`: `sqrt_uint32()` evita overflow en el calculo inicial para no caer
  en division por cero con entradas extremas.

### Malloc fallback

Si `malloc(EECONFIG_SIZE_ANALOG_MATRIX)` falla durante init, el teclado ya no
queda muerto con estructuras sin inicializar. Se cargan defaults sanos, travel
configs, scale factors y estado de calibracion power-on.

### Union `analog_key_t`

Se corrigio un bug donde campos solapados por union (`rpd_trig_sen`,
`okmc_idx`, `js_axis`, `hold`) podian corromper la sensibilidad de Rapid Trigger
cuando Gaming convertia un modo avanzado de vuelta al modo base.

### Logica

Estos cambios no buscan bajar latencia directamente. Buscan evitar estados
corruptos, respuestas HID invalidas, errores silenciosos y rutas peligrosas si
alguien reactiva modulos apagados.

## Compatibilidad con Launcher y EEPROM

No se cambio el layout de EEPROM.

No se expandio `calibrated_value_t` de bitfields de 12 bits a `uint16_t` porque
eso romperia compatibilidad con EEPROM y potencialmente con el Launcher.

No se elimino `scale_factor` float porque el Launcher puede pedirlo como dato de
calibracion/diagnostico.

`info.json` fue limpiado para no declarar RGB activo y para no declarar un
`build.debounce_type` invalido. El valor real de debounce se controla desde:

```make
DEBOUNCE_TYPE = none
```

El debounce analogico minimo queda separado del debounce QMK:

```c
#define ANALOG_DEBOUNCE_TIME 1
```

No se debe bajar a `0`; `analog_matrix_scan.c` ahora falla en compilacion si se
intenta hacerlo.

`via_json/k2_he_ansi.json` tambien fue limpiado para este build ANSI USB:

- Se quito el menu `qmk_lighting`.
- Se quitaron custom keycodes wireless (`BTH1`, `BTH2`, `BTH3`, `2.4G`, `Batt`).
- `PROF1..PROF3` quedan alineados con los valores reales del firmware cuando
  wireless esta apagado.

## Cambios rechazados o pospuestos

### No quitar `__packed__` de `analog_key_t`

La ganancia seria menor a 1 us por scan y aumentaria RAM. No vale la pena.

### No expandir bitfields de calibracion

Rompe layout de EEPROM por una ganancia minima. No se hace.

### No forzar `always_inline` en `update_raw_value()`

La funcion es grande. Forzar inline puede inflar codigo y empeorar cache/flash
fetch. GCC con `-O2` y LTO decide mejor.

### No usar `-O3`

El CPU processing es una parte pequena del scan. `-O3` puede inflar codigo y no
ataca el cuello real, que son tiempos de settle, ADC y shift register.

### No mover `calibrate_values` a malloc por ahora

Ahorra RAM, pero aumenta complejidad y no baja latencia de scan. Se deja para
otro momento si RAM se vuelve un problema.

## Binarios principales

Baseline actual:

```text
C:\Users\Alex\keychron-qmk\keychron_k2_he_ansi_alex.bin
C:\Users\Alex\keychron-qmk\K2HE_ALEX_NORMAL_autoCalibClean_adc28_settle20_hc164stock_noContinuousRT_powerOnCal_security_launcher.bin
```

Pruebas de timing:

```text
C:\Users\Alex\keychron-qmk\K2HE_ALEX_TIMING_SAFE_adc28_settle15_hc164x5_noContinuousRT.bin
C:\Users\Alex\keychron-qmk\K2HE_ALEX_TIMING_AGGRESSIVE_adc15_settle10_hc164x5_noContinuousRT.bin
```

Experimental RT:

```text
C:\Users\Alex\keychron-qmk\K2HE_ALEX_EXPERIMENT_CONTINUOUS_RT_space_lshift_autoCalibClean_adc28_settle20_hc164stock_powerOnCal_security_launcher.bin
```

## Protocolo de prueba recomendado

1. Flashear baseline normal si se quiere una referencia estable.
2. Probar arranque en frio:
   - Desconectar/reconectar.
   - Probar varias teclas desde el primer segundo.
   - Confirmar que ya no se siente lento al iniciar.
3. Probar `A` fuera del juego:
   - Bloc de notas.
   - Keyboard Tester.
   - RT apagado para esa tecla.
   - Ver si hay repeticion anormal o solo repeticion normal de Windows.
4. Probar Space en Minecraft 1.8.9:
   - Primero baseline.
   - Luego `TIMING_SAFE`.
   - Luego `TIMING_AGGRESSIVE` solo si Safe no muestra ruido.
5. En Keychron Launcher:
   - Ver Trigger Demo.
   - Presionar lentamente y observar si el travel sube suave.
   - Buscar lecturas nerviosas o saltos raros.
6. Si aparece ghosting, crosstalk o teclas perdidas:
   - Volver a baseline o Safe.
   - No usar Aggressive como daily.

## Interpretacion de resultados

Si `TIMING_SAFE` se siente igual de estable que baseline, el hardware tolera
menos settle y un shift register mas rapido.

Si `TIMING_AGGRESSIVE` se siente mas rapido pero tiene lecturas nerviosas, el
limite probablemente es el front-end analogico/ADC, no el codigo C.

Si `A` repite incluso en baseline con RT apagado, mirar primero:

- Perfil activo del Launcher.
- Configuracion de repeticion de teclado en Windows.
- Calibracion de esa tecla.
- Posible variacion fisica/sensor.

Si Space falla solo en juego y no en tester, mirar:

- Actuation/release en Launcher.
- Rapid Trigger por tecla.
- Diferencia entre mantener presion y taps rapidos.

## Regla de mantenimiento

Para cambios futuros, evitar:

- Cambiar layout de EEPROM.
- Hardcodear actuation/RT por tecla si el Launcher ya lo controla.
- Meter optimizaciones de CPU que no ataquen el tiempo real del scan.
- Tocar `PAL_USE_CALLBACKS`; se usa para wakeup por `ANALOG_MATRIX_WAKEUP_PIN`.

Priorizar:

- Cambios medibles en settle/ADC/shift register.
- Parches que mantengan compatibilidad con Launcher.
- Cambios reversibles con binarios de prueba separados.
