# Reutiliza reglas del keymap estable sin duplicarlas.
include keyboards/keychron/k2_he/ansi/keymaps/alex/rules-common.mk

# V4.2.1 LAB: hard-strip del stack Gamepad/Joystick/XInput sin cambiar el
# "flavor" de SRC a simple-expanded. QMK agrega fuentes de plataforma despues
# de cargar el rules.mk del keymap; entre ellas aparece
# bootloaders/$(BOOTLOADER_TYPE).c antes de que BOOTLOADER_TYPE se resuelva.
# Usar directamente `SRC := $(filter-out ...)` hace que los `SRC +=` posteriores
# expandan ese nombre demasiado pronto y puede producir bootloaders/.o.
#
# Congelamos SOLO la lista existente ya filtrada y restauramos SRC como variable
# recursive para que las adiciones posteriores de QMK se expandan en su momento.
ALEX_LAB_SRC_BASE := $(filter-out %/action_joystick.c %/action_xinput.c %/game_controller_common.c %/usb_descriptor_override.c,$(SRC))
SRC = $(ALEX_LAB_SRC_BASE)

# Invariante de build: no volver a convertir SRC a simple-expanded aqui.
ifneq ($(flavor SRC),recursive)
$(error alex_lab requires recursive SRC so late QMK platform sources resolve correctly)
endif

# --- Medicion de fase del poll USB (metodo C, LAB-only, temporal) ------------
# Activa usb_poll_phase_last/min/max/count en usb_driver.c: desactivado en produccion.
# OPT_DEFS += -DUSB_POLL_PHASE_PROBE

# Medicion de duracion y fase de fin del scan: desactivado en produccion.
# OPT_DEFS += -DUSB_SOF_TIMING_PROBE
