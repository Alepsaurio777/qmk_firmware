# Rama MC 1.8.9: base de build compatible con Keychron Launcher.
include keyboards/keychron/k2_he/ansi/keymaps/alex/rules-common.mk

# Telemetria en tiempo real: medicion precisa por hardware de escaneo y poll USB
OPT_DEFS += -DUSB_SOF_TIMING_PROBE -DUSB_POLL_PHASE_PROBE

