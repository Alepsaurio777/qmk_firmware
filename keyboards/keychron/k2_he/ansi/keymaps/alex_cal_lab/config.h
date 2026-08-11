#pragma once

// Experimento aislado de calibracion. Hereda el firmware estable, no alex_lab:
// RT predictivo y stretches quedan fuera para que no contaminen la prueba.
#include "../alex/config.h"

// Siete bottom-outs fisicos distintos -> mediana robusta, ignorando un extremo
// por lado. El resultado queda como preview hasta recibir --cal-apply.
#define ANALOG_CONFIDENT_BOTTOM_OUT_ENABLE 1

// El learner antiguo aplica al vuelo y puede persistir. Es incompatible con el
// contrato reversible de este build.
#undef ANALOG_BOTTOM_OUT_LEARN
#define ANALOG_BOTTOM_OUT_LEARN 0

