// ESPEJO de los valores de keyboards/keychron/k2_he/config.h que afectan a la
// logica bajo test.
//
// Por que un espejo y no incluir el config.h real: ese fichero declara pines
// (C13, A8, B10...) que son macros de QMK y no compilan en host. Separar lo que
// es hardware de lo que es politica esta fuera del alcance de esto.
//
// El riesgo del espejo es obvio: si el teclado cambia un valor y esto no, los
// tests validan un firmware que no existe. Mitigacion en dos capas:
//   1. test_rapid_trigger.c ancla los valores DERIVADOS (puntos de actuacion y
//      desactuacion ya escalados) con numeros literales. Un cambio de
//      histeresis o de escala sale como test rojo, no como un A/B que miente.
//   2. Este fichero se lee junto al config.h real en la guia de auditoria.
#pragma once

// --- Politica de actuacion / histeresis -----------------------------------
#define STATIC_HYSTERESIS_GAMING 5
#define STATIC_HYSTERESIS_TYPING 5
#define ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING 1

// --- Capas ------------------------------------------------------------------
// Gaming = capas 0 y 1 (interruptor fisico). analog_matrix_is_gaming_mode()
// compara default_layer_state contra esta mascara.
#define ANALOG_GAMING_LAYERS_MASK ((layer_state_t)0x03)
#define ANALOG_POLICY_LAYER 0

// --- Whitelists por keycode -------------------------------------------------
#define ANALOG_CONTINUOUS_RT_KEY1_KEYCODE KC_SPACE
#define ANALOG_CONTINUOUS_RT_KEY2_KEYCODE KC_LEFT_SHIFT
#define ANALOG_PREDICTIVE_RT_KEY3_KEYCODE KC_W
#define ANALOG_PREDICTIVE_RT_KEY4_KEYCODE KC_A
#define ANALOG_PREDICTIVE_RT_KEY5_KEYCODE KC_S
#define ANALOG_PREDICTIVE_RT_KEY6_KEYCODE KC_D

#define ANALOG_RELEASE_STRETCH_KEY1_KEYCODE KC_W
#define ANALOG_RELEASE_STRETCH_KEY2_KEYCODE KC_SPACE
#define ANALOG_PRESS_STRETCH_KEY1_KEYCODE KC_SPACE
#define ANALOG_PRESS_STRETCH_KEY2_KEYCODE KC_LEFT_SHIFT

// --- Defaults del perfil gaming (profiles.c) --------------------------------
// No los consume el codigo bajo test; los usan los tests para expresar
// "la configuracion real de torneo" en vez de numeros inventados.
#define HOSTTEST_GAMING_ACT_PT 20 // DEFAULT_ACTUATION_POINT, 2.0 mm
#define HOSTTEST_GAMING_SEN 3     // profile_default_rt_sen[1], 0.3 mm
#define HOSTTEST_GAMING_SEN_RLS 2 // profile_default_rt_sen_rls[1], 0.2 mm
