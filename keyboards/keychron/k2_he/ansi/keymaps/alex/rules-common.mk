VIA_ENABLE = yes
RGB_MATRIX_ENABLE = no
LK_WIRELESS_ENABLE = no
KC_BLUETOOTH_ENABLE = no
DEBOUNCE_TYPE = none
MOUSEKEY_ENABLE = no
LTO_ENABLE = yes

# Diagnostico exclusivo del binario lab. El estable no compila telemetry.c ni
# conserva sus callbacks/tareas: no basta con dejar el stream apagado en runtime.
ALEX_TELEMETRY_ENABLE ?= no
ifeq ($(strip $(ALEX_TELEMETRY_ENABLE)), yes)
    OPT_DEFS += -DALEX_TELEMETRY_ENABLE
    SRC += $(ALEX_TELEMETRY_SOURCE)
endif

# Recorte de defaults de QMK sin uso en este teclado (verificado en cflags):
# grave-esc/space-cadet/magic solo aportaban keycodes que nadie usa.
# TRI_LAYER no se puede cortar: VIA lo fuerza con := en common_features.mk
# (dependencia del protocolo); por eso el lockdown de Gaming bloquea
# TL_LOWR/TL_UPPR explicitamente — esa defensa NO es redundante.
GRAVE_ESC_ENABLE = no
SPACE_CADET_ENABLE = no
MAGIC_ENABLE = no
