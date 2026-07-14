#pragma once

// Binario LAB / experimental. Hereda TODO el config estable de `alex` y
// enciende lo experimental encima. Aqui se rompen cosas sin miedo; el binario
// de torneo probado es keymaps/alex. La unica diferencia de codigo entre ambos
// son los dos #define de abajo — la logica es compartida (ver keymap.c).
#include "../alex/config.h"

// Instrumentacion de timing del scan (duracion del barrido + fase respecto al
// SOF), exportada en el paquete v2 de telemetria. Debug, fuera del torneo.
#define USB_SOF_TIMING_PROBE

// RT predictivo por velocidad: enciende el feed de vel_ema en update_raw_value
// y su consumo en la FSM del rapid trigger. Especulativo (dispara antes del
// cruce fisico) — a evaluar con la telemetria antes de considerarlo estable.
#define ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE 1
