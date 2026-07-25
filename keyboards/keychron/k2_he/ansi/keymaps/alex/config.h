#pragma once

// Binario ESTABLE / torneo: SOF-sync + pipeline + telemetria de travel, SIN el
// probe de timing (debug) ni prediccion. El keymap alex_lab hereda este config
// y enciende esas dos cosas para experimentar. El timestamp de SOF lo provee
// usb_main.c mientras SOF_SYNC este activo, asi que el sync no necesita probe.

// Fase 3 (parte 2a): procesar la columna previa durante el settle de la
// siguiente. Medido: 1041-1052 us sin pipeline -> 880-925 us con pipeline.
#define ANALOG_SCAN_PIPELINE 1

// Fase 3 (parte 2b): arrancar el barrido ~10 us despues de cada SOF para que
// el barrido (880-925 us) + el procesado de cambios de QMK quede armado antes
// del siguiente poll. Convierte la loteria de fase (0-1 ms aleatorio por
// pulsacion) en una constante. Verificacion: la fase de la telemetria debe
// quedar estable ~890-935 us en vez de uniforme 0-1000.
#define ANALOG_SCAN_SOF_SYNC 1

// Torneo: aprendizaje adaptativo de bottom-out APAGADO. El drift termico se
// descarto empiricamente (sesion de 24.047 eventos, travel de actuacion
// mediano plano en 25.0 los 5 tramos) y un rango dinamico que muta a mitad de
// partida contradice el objetivo de configuracion inmutable. La calibracion de
// reposo por boot (CALIB_ZERO_TRAVEL_POWER_ON) sigue compensando temperatura.
// alex_lab lo re-enciende para seguir experimentando.
#undef ANALOG_BOTTOM_OUT_LEARN
#define ANALOG_BOTTOM_OUT_LEARN 0

// RGB_MATRIX_ENABLE = no en rules.mk: sin iluminacion en estos builds. El
// bloque de tuning RGB que vivia aqui (limites de brillo/framerate y #undef de
// efectos) era codigo muerto bajo un #ifdef que nunca se cumplia; esta en el
// historial de git por si algun dia se re-enciende RGB.
