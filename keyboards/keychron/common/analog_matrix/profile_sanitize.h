// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "analog_matrix.h"

#ifndef ANALOG_PROFILE_SANITIZER_ENABLE
#    define ANALOG_PROFILE_SANITIZER_ENABLE 0
#endif

#if ANALOG_PROFILE_SANITIZER_ENABLE
// Canonicaliza un perfil cargado desde EEPROM antes de usarlo. Los defaults se
// pasan explicitamente para que el helper sea puro/testeable y no dependa de
// globals del teclado. Devuelve true si corrigio algun byte de configuracion.
bool analog_profile_sanitize(analog_matrix_profile_t *prof, uint8_t default_mode, uint8_t default_act_pt, uint8_t default_rt_sen, uint8_t default_rt_sen_rls);
#endif
