#pragma once

// Variante MC 1.8.9 agresiva. Hereda lab completo (telemetria, histograma,
// stretches F6/F9, predictivo F7) y aprieta los parametros profundos del
// firmware que Launcher no expone: histeresis, zonas muertas, filtro ADC,
// continuous RT, motor predictivo y SOCD.
//
// NO tocar actuacion ni sensibilidad RT aqui — eso es Launcher.
#include "../alex_lab/config.h"

// SOF-sync + pipeline (torneo ya los tiene; lab base no).
#define ANALOG_SCAN_SOF_SYNC 1
#define ANALOG_SCAN_PIPELINE 1

// --- Histeresis y zonas muertas -------------------------------------------

// Deactuacion mas rapida: 0.3 mm en vez de 0.5 mm. El cap adaptativo
// (actuation/2) sigue activo, asi que actuaciones de 0.2 mm usan 0.1 mm.
// Efecto directo en la velocidad del ciclo w-tap.
#undef STATIC_HYSTERESIS_GAMING
#define STATIC_HYSTERESIS_GAMING 3

// Detectar travel 0.4 mm antes desde reposo. La calibracion de zero (dead
// zone 2) ya es minima; esto recorta el otro extremo.
#undef TOP_OUT_DEAD_ZONE_GAMING
#define TOP_OUT_DEAD_ZONE_GAMING 8

// Menos filtrado ADC = respuesta mas rapida. Telemetria muestra ruido = 0
// con dedos apoyados; 3 sigue siendo conservador.
#undef ANALOG_RAW_NOISE_FILTER_GAMING
#define ANALOG_RAW_NOISE_FILTER_GAMING 3

// --- Continuous RT --------------------------------------------------------

// Re-press desde cualquier profundidad sin soltar del todo (Space/LShift).
// Critico para salto rapido y bridging.
#undef ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE
#define ANALOG_CONTINUOUS_RAPID_TRIGGER_IN_GAMING_MODE 1

// No tiene que subir tanto para re-registrar: 180 en vez de 240 (de 245 max).
// Equivale a que baste con soltar ~25% del recorrido en vez de ~100%.
#undef ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL
#define ANALOG_CONTINUOUS_RT_REPRESS_MAX_TRAVEL 180

// --- Motor predictivo (vel_ema) -------------------------------------------

// Proyectar 3 scans en vez de 2: anticipa ~0.3 mm a velocidad de w-tap.
#undef ANALOG_PREDICTIVE_LOOKAHEAD
#define ANALOG_PREDICTIVE_LOOKAHEAD 3

// Umbral mas bajo: mas golpes califican como "rapidos". Un tap de escritura
// lenta (delta ~6) sigue por debajo.
#undef ANALOG_PREDICTIVE_MIN_VELOCITY
#define ANALOG_PREDICTIVE_MIN_VELOCITY 3

// --- SOCD (Rappy Snappy A/D) ----------------------------------------------

// Cambio de direccion mas rapido: 4 unidades de histeresis en vez de 6.
// Menos margen contra chatter ADC, pero con noise filter 3 sobra.
#undef ANALOG_SOCD_DEEPER_HYSTERESIS
#define ANALOG_SOCD_DEEPER_HYSTERESIS 4
