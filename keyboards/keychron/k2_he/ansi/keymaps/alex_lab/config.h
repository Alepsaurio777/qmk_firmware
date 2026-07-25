#pragma once

// Binario LAB / experimental. Hereda TODO el config estable de `alex` y
// enciende lo experimental encima. Aqui se rompen cosas sin miedo; el binario
// de torneo probado es keymaps/alex. Las directivas experimentales de abajo
// son la unica diferencia de configuracion; la logica es compartida.
#include "../alex/config.h"

// Instrumentacion de timing del scan (duracion del barrido + fase respecto al
// SOF), exportada en el paquete v2 de telemetria. Debug, fuera del torneo.
#define USB_SOF_TIMING_PROBE

// RT predictivo por velocidad: enciende el feed de vel_ema en update_raw_value
// y su consumo en la FSM del rapid trigger. Especulativo (dispara antes del
// cruce fisico) — a evaluar con la telemetria antes de considerarlo estable.
#define ANALOG_PREDICTIVE_ACTUATION_IN_GAMING_MODE 1

// Re-enciende el aprendizaje de bottom-out (alex lo apaga para torneo). Aqui es
// donde se prueban sesiones termicas largas y el manejo de outliers del learner.
#undef ANALOG_BOTTOM_OUT_LEARN
#define ANALOG_BOTTOM_OUT_LEARN 1

// F6: release-stretch anclado al tick de MC (W + espacio; coords en
// k2_he/config.h, racional en analog_matrix.h). Garantiza que todo release
// fisico de W/espacio quede reportado OFF >= 55 ms: cada w-tap resetea sprint
// y cada tap de espacio resetea jumpTicks aunque el dedo sea mas rapido que el
// muestreo de 50 ms del cliente. Solo retrasa presses reales; no sintetiza.
// OJO al probar: el stretch hace visible TODO release, tambien el accidental
// por temblor con el dedo encima — subir el release de W a 0.3 mm en Launcher
// (0.2 mm queda muy justo en pelea). Validar con el histograma de ventanas OFF
// del cliente evlog (--events): W/SPC no deben bajar de 55 ms con este build.
#define ANALOG_RELEASE_STRETCH_IN_GAMING_MODE 1

// F7: whitelist predictiva por camino y por riesgo de mecanica, no "todo
// movimiento". bits: 0=SPC 1=LSFT 2=W 3=A 4=S 5=D.
//  - S fuera de ambos: el fantasma mas caro del juego (corta sprint en chase).
//  - W fuera de ambos: el press fantasma arriesga el borde en sumo, y el
//    re-press predictivo acorta la ventana OFF del w-tap (pelearia contra F6).
//  - LSFT fuera: la prediccion no aporta nada al sneak y su fantasma no es gratis.
//  - PRESS: SPC + A + D (0x29) — fantasmas baratos, unico sitio donde anticipar
//    ~1-3 ms puede pescar un tick anterior.
//  - REPRESS: solo A + D (0x28) — con F6 activo, un re-press de espacio dentro
//    de la ventana se reporta en la frontera de 55 ms igual (la prediccion no
//    adelanta nada ahi) y un re-press fantasma se convierte en salto fantasma
//    DIFERIDO 55 ms — mas dificil de diagnosticar. El unico upside restante
//    (ventanas >55 ms, ~1-3 ms de loteria) no paga ese riesgo. OJO bits:
//    A=bit3, D=bit5 -> 0x28 (0x18 seria A+S: re-meteria a S y sacaria a D).
#define ANALOG_PREDICTIVE_PRESS_KEY_MASK 0x29
#define ANALOG_PREDICTIVE_REPRESS_KEY_MASK 0x28
