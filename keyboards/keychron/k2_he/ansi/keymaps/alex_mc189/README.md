# alex_mc189 — Calm / Continuous RT v2

Rama de juego deliberadamente conservadora, basada en `alex` estable.
No intenta predecir ni alargar inputs: la prioridad es que Launcher siga siendo
la fuente de configuracion y que Rapid Trigger se comporte de forma repetible.

## Diferencias intencionales

- `TOP_OUT_DEAD_ZONE_GAMING`: 12 -> 4.
- Continuous RT: ON para W/A/S/D/Space/LShift **solo cuando su modo efectivo es Rapid Trigger**.
- Bottom guard adicional: bypass solo en esas teclas Continuous RT.
- Peak tracking: en Continuous RT sigue actualizando por encima de travel 240 hasta el maximo fisico.
- Re-press max: vuelve al maximo fisico normal (245), eliminando el cap experimental de 240.
- Predictor: OFF.
- F6 release-stretch: OFF.
- F9 press-stretch: OFF.
- Startup adaptive noise-floor: OFF.
- Profile sanitizer: ON (sin CRC/layout nuevo).
- ADC/settle/HC164: valores estables de `alex` (28 / 20 us / 50 NOP).
- SOCD: no se siembra desde firmware; Launcher conserva la configuracion explicita.
- Gamepad/Joystick/XInput: fuera del runtime/build de esta rama.

## Reset Profile reproducible

En `alex_mc189`, el perfil gaming vuelve a sembrar:

- W/A/S/D/Space/LShift: Rapid Trigger
- actuation: 0.2 mm
- press distance: 0.3 mm
- release distance: 0.2 mm
- LCtrl: Regular
- SOCD: vacio

Launcher puede cambiar estos valores despues del reset; no estan hardcodeados en
la FSM de Rapid Trigger. El seed solo define a donde vuelve `Reset Profile`.

## Perfiles Launcher incluidos

- `profiles/HEProfile-MC189-Calm-Clean-2026-08-13.json`
  - W/A/S/D/Space/LShift = Rapid 0.2 / 0.3 / 0.2
  - E = Regular 0.2
  - LCtrl = Regular 0.2
  - sin SOCD/Snap Tap
- `profiles/HEProfile-MC189-Calm-SOCD-Optional-2026-08-13.json`
  - misma configuracion Hall/RT
  - conserva los pares W/S y A/D tipo 3 del export original
- `profiles/HEProfile-Gaming-2026-08-13.json`
  - export original del usuario, sin modificar, solo como referencia

## Regression central v2

El hosttest repite 100 ciclos cerca de bottom-out. En cada ciclo, con release
0.2 mm, el peak debe refrescar hasta 245 y el release debe volver a ocurrir en
233. Esto cubre el fallo donde el segundo ciclo terminaba liberando en 228.

Lejos del fondo tambien se prueba que press 0.3 mm siga siendo exactamente 18
travel units, sin acelerar el threshold por software.

## Build ARM

```sh
make -e SKIP_GIT=yes keychron/k2_he/ansi:alex_mc189
```

Este paquete de conversacion no contiene `.git` ni `lib/`, por lo que el build
ARM final debe hacerse sobre el arbol Keychron/QMK completo del usuario.
