// El keymap lab reutiliza toda la logica del keymap estable; solo difiere el
// config.h (enciende prediccion + probe). Sin duplicar codigo — cualquier
// cambio de logica se hace en keymaps/alex y ambos binarios lo heredan.
#include "../alex/keymap.c"
