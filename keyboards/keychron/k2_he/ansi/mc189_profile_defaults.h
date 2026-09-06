/* Copyright 2024 ~ 2025 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// Defaults reproducibles para la rama alex_mc189. Estas constantes son la
// configuracion que un "Reset Profile" debe volver a sembrar en el perfil 1.
// Unidades de Launcher: 0.1 mm.
#define MC189_GAMING_ACT_PT 2
#define MC189_GAMING_RT_PRESS 3
#define MC189_GAMING_RT_RELEASE 2

// Mantener una sola lista fuente evita que profiles.c, hosttests y artefactos de
// validacion se desalineen silenciosamente.
#define MC189_GAMING_TUNING_KEYS(X) \
    X(KC_W)                         \
    X(KC_A)                         \
    X(KC_S)                         \
    X(KC_D)                         \
    X(KC_SPACE)                     \
    X(KC_LSFT)

#define MC189_GAMING_TUNING_COUNT 6

// Ctrl permanece Regular en la rama calm. No forma parte de Continuous RT.
#define MC189_GAMING_LCTRL_PROFILE_MODE 0
