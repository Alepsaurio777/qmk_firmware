VIA_ENABLE = yes
RGB_MATRIX_ENABLE = no
LK_WIRELESS_ENABLE = no
KC_BLUETOOTH_ENABLE = no
DEBOUNCE_TYPE = none
MOUSEKEY_ENABLE = no
LTO_ENABLE = yes

# Ownership/refcount opt-in para alex + alex_lab. QMK stock conserva bitmasks
# simples; limitar el define a estos keymaps evita alterar set_mods() en otros
# teclados del arbol.
OPT_DEFS += -DINPUT_OWNERSHIP_REFCOUNT_ENABLE
SRC += quantum/report_batch.c
SRC += tmk_core/protocol/chibios/usb_deferred.c

# Recorte de defaults de QMK sin uso en este teclado (verificado en cflags):
# grave-esc/space-cadet/magic solo aportaban keycodes que nadie usa.
# TRI_LAYER no se puede cortar: VIA lo fuerza con := en common_features.mk
# (dependencia del protocolo); por eso el lockdown de Gaming bloquea
# TL_LOWR/TL_UPPR explicitamente — esa defensa NO es redundante.
GRAVE_ESC_ENABLE = no
SPACE_CADET_ENABLE = no
MAGIC_ENABLE = no

# telemetry.c/h: modulo de diagnostico retirado formalmente; no se compila.
# Se conserva en disco por si se necesita re-instrumentar en el futuro.
