# Reutiliza las reglas del estable y anade el diagnostico de lab (telemetria +
# probe). El config.h enciende SOF-sync y pipeline encima de alex_lab.
ALEX_TELEMETRY_ENABLE = yes
ALEX_TELEMETRY_SOURCE = telemetry_build.c
include keyboards/keychron/k2_he/ansi/keymaps/alex/rules-common.mk
