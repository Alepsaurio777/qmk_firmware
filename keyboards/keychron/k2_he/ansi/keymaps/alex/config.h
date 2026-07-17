#pragma once

// Binario ESTABLE / torneo: SOF-sync + pipeline + telemetria de travel, SIN el
// probe de timing (debug) ni prediccion. El keymap alex_lab hereda este config
// y enciende esas dos cosas para experimentar. El timestamp de SOF lo provee
// usb_main.c mientras SOF_SYNC este activo, asi que el sync no necesita probe.

// Fase 3 (parte 2a): procesar la columna previa durante el settle de la
// siguiente. Medido: 1041-1052 us sin pipeline -> 878-892 us con pipeline.
#define ANALOG_SCAN_PIPELINE 1

// Fase 3 (parte 2b): arrancar el barrido ~10 us despues de cada SOF para que
// el barrido (878-892 us) + el procesado de cambios de QMK quede armado antes
// del siguiente poll. Convierte la loteria de fase (0-1 ms aleatorio por
// pulsacion) en una constante. Verificacion: la fase de la telemetria debe
// quedar estable ~895-905 us en vez de uniforme 0-1000.
#define ANALOG_SCAN_SOF_SYNC 1

// Torneo: aprendizaje adaptativo de bottom-out APAGADO. El drift termico se
// descarto empiricamente (sesion de 24.047 eventos, travel de actuacion
// mediano plano en 25.0 los 5 tramos) y un rango dinamico que muta a mitad de
// partida contradice el objetivo de configuracion inmutable. La calibracion de
// reposo por boot (CALIB_ZERO_TRAVEL_POWER_ON) sigue compensando temperatura.
// alex_lab lo re-enciende para seguir experimentando.
#undef ANALOG_BOTTOM_OUT_LEARN
#define ANALOG_BOTTOM_OUT_LEARN 0

#ifdef RGB_MATRIX_ENABLE
// Optimize RGB matrix for maximum performance
// Limit the max brightness to reduce power draw
#undef RGB_MATRIX_MAXIMUM_BRIGHTNESS
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS 120

// Limit framerate to free up CPU cycles for the analog matrix
#undef RGB_MATRIX_FRAME_RATE
#define RGB_MATRIX_FRAME_RATE 30

// Reduce how many LEDs are processed per task tick to prevent blocking
#undef RGB_MATRIX_LED_PROCESS_LIMIT
#define RGB_MATRIX_LED_PROCESS_LIMIT 5

// Disable ALL mathematical effects to drastically reduce CPU overhead
#undef ENABLE_RGB_MATRIX_ALPHAS_MODS
#undef ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#undef ENABLE_RGB_MATRIX_GRADIENT_LEFT_RIGHT
#undef ENABLE_RGB_MATRIX_BREATHING
#undef ENABLE_RGB_MATRIX_BAND_SAT
#undef ENABLE_RGB_MATRIX_BAND_VAL
#undef ENABLE_RGB_MATRIX_BAND_PINWHEEL_SAT
#undef ENABLE_RGB_MATRIX_BAND_PINWHEEL_VAL
#undef ENABLE_RGB_MATRIX_BAND_SPIRAL_SAT
#undef ENABLE_RGB_MATRIX_BAND_SPIRAL_VAL
#undef ENABLE_RGB_MATRIX_CYCLE_ALL
#undef ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#undef ENABLE_RGB_MATRIX_CYCLE_UP_DOWN
#undef ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
#undef ENABLE_RGB_MATRIX_CYCLE_OUT_IN
#undef ENABLE_RGB_MATRIX_CYCLE_OUT_IN_DUAL
#undef ENABLE_RGB_MATRIX_CYCLE_PINWHEEL
#undef ENABLE_RGB_MATRIX_CYCLE_SPIRAL
#undef ENABLE_RGB_MATRIX_DUAL_BEACON
#undef ENABLE_RGB_MATRIX_RAINBOW_BEACON
#undef ENABLE_RGB_MATRIX_RAINBOW_PINWHEELS
#undef ENABLE_RGB_MATRIX_RAINDROPS
#undef ENABLE_RGB_MATRIX_JELLYBEAN_RAINDROPS
#undef ENABLE_RGB_MATRIX_HUE_BREATHING
#undef ENABLE_RGB_MATRIX_HUE_PENDULUM
#undef ENABLE_RGB_MATRIX_HUE_WAVE
#undef ENABLE_RGB_MATRIX_PIXEL_RAIN
#undef ENABLE_RGB_MATRIX_PIXEL_FLOW
#undef ENABLE_RGB_MATRIX_PIXEL_FRACTAL
#undef ENABLE_RGB_MATRIX_TYPING_HEATMAP
#undef ENABLE_RGB_MATRIX_DIGITAL_RAIN
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_SIMPLE
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_WIDE
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTIWIDE
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_CROSS
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTICROSS
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_NEXUS
#undef ENABLE_RGB_MATRIX_SOLID_REACTIVE_MULTINEXUS
#undef ENABLE_RGB_MATRIX_SPLASH
#undef ENABLE_RGB_MATRIX_MULTISPLASH
#undef ENABLE_RGB_MATRIX_SOLID_SPLASH
#undef ENABLE_RGB_MATRIX_SOLID_MULTISPLASH

// Keep only solid color enabled
#ifndef ENABLE_RGB_MATRIX_SOLID_COLOR
#define ENABLE_RGB_MATRIX_SOLID_COLOR
#endif

#undef RETAIL_DEMO_ENABLE

#endif
