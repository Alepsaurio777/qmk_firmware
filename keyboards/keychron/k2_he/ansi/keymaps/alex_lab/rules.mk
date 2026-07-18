VIA_ENABLE = yes
RGB_MATRIX_ENABLE = no
LK_WIRELESS_ENABLE = no
KC_BLUETOOTH_ENABLE = no
DEBOUNCE_TYPE = none
MOUSEKEY_ENABLE = no
LTO_ENABLE = yes
# Reutiliza la telemetria del keymap estable (misma fuente, sin duplicar).
SRC += ../alex/telemetry.c

# Recorte de defaults de QMK sin uso en este teclado (verificado en cflags):
# grave-esc/space-cadet/magic solo aportaban keycodes que nadie usa.
# TRI_LAYER no se puede cortar: VIA lo fuerza con := en common_features.mk
# (dependencia del protocolo); por eso el lockdown de Gaming bloquea
# TL_LOWR/TL_UPPR explicitamente — esa defensa NO es redundante.
GRAVE_ESC_ENABLE = no
SPACE_CADET_ENABLE = no
MAGIC_ENABLE = no
