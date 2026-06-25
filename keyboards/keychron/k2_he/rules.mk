JOYSTICK_ENABLE = no
KEYCHRON_RGB_ENABLE = yes
# Increase stack size to avoid crashing of eeprom_update_block()
USE_PROCESS_STACKSIZE = 0x2000
USE_FPU = yes

OPT_DEFS += -DSHARED_EP_ENABLE -DKEYBOARD_SHARED_EP

include keyboards/keychron/common/analog_matrix/analog_matrix.mk
include keyboards/keychron/common/keychron_common.mk
include keyboards/keychron/common/wireless/wireless.mk

VPATH += $(TOP_DIR)/keyboards/keychron

OPT = 2
