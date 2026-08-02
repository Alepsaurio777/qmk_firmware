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

// F9: minimo-ON en el espacio (solo lab). Espejo de F6 y la otra mitad de lo
// que jumpTicks necesita: F6 arregla el release no visto (siguiente salto hasta
// 500 ms tarde), F9 arregla el press no visto (el salto no existio). Los dos
// activos en el espacio dan 110 ms de ciclo en el peor caso para un tap — mal
// numero en abstracto, buen numero contra los 500 ms que pagas hoy.
// OJO al medir: el evlog cuelga de process_record_user, aguas abajo de los dos
// stretches. El flanco FISICO llega por analog_matrix_physical_edge_hook, que
// solo existe con uno de los dos encendido; por eso una sesion LAB ahora da los
// dos histogramas y ya no hace falta cruzar dos drills distintos.
#define ANALOG_PRESS_STRETCH_IN_GAMING_MODE 1

// F9 en LSHIFT (slot 2). Aqui y no en torneo, que es exactamente donde debe
// vivir un experimento sin validar: alex_lab es el binario donde se rompen
// cosas. El keycode se declara en k2_he/config.h y es inerte con el flag
// apagado, asi que el binario de torneo no lo compila — comprobado: `alex` no
// se movio ni un byte al anadirlo.
//
// Lo que decide si esto se promociona NO es como se siente: es el drill D1 del
// plan de diagnostico, que mide en el binario de TORNEO que fraccion de presses
// de LSHIFT al bridgear dura menos de un tick. Si sale ~0, esto se apaga para
// siempre y habra costado 20 minutos de drill en vez de un ciclo entero.
//
// Coste conocido si se enciende: en 1.8.9 el sneak cancela el sprint, asi que
// 55 ms de shift forzado obligan a re-doble-tap de W. Eso hay que verlo en el
// histograma de W, no en la sensacion.
