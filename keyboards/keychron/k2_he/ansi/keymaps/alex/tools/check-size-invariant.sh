#!/usr/bin/env bash
# Enforza el invariante de los tres binarios (ver DEVELOPMENT.md, "La regla de oro"):
# todo codigo experimental vive detras de un #if FLAG y el binario de TORNEO lo
# compila FUERA. La prueba de que quedo bien aislado es que `alex` no crece.
#
# Hasta ahora eso se verificaba a ojo, comparando el tamano contra un numero
# recordado. Esto lo vuelve un fallo del build en vez de un descuido: si `alex`
# crece y nadie declaro por que, el script para.
#
# Uso (desde MSYS2 MinGW64, en la raiz del repo):
#   make keychron/k2_he/ansi:alex
#   make keychron/k2_he/ansi:alex_lab
#   make keychron/k2_he/ansi:alex_cal_lab
#   ./keyboards/keychron/k2_he/ansi/keymaps/alex/tools/check-size-invariant.sh
#
# Cuando el crecimiento es LEGITIMO (un arreglo de correccion en codigo que ya
# estaba en torneo a proposito, no una feature de lab que se fugo), se sube la
# baseline AQUI, en su propio commit, con el motivo en el mensaje. Ese commit es
# la declaracion. Bajarla sin motivo tambien falla: un `alex` que encoge de
# sorpresa significa que algo se compilo fuera sin querer.

set -u

# Baselines.
#
# 1-ago-2026 (calibracion reversible): alex_cal_lab nace en 57436 B. Hereda
# torneo, suma Raw HID y el learner por confianza; aplica solo a RAM y permite
# rollback. alex/alex_lab permanecen en sus baselines anteriores.
#
# 1-ago-2026 (separacion estable/lab): alex 55852 -> 53200 (-2652) al compilar
# fuera telemetria, evlog, histograma y el callback de remap sin consumidores.
# alex_lab 58268 -> 58484 (+216) por HEALTH v2, segundo slot F9 observable,
# re-resolucion del histograma y sus tests/correcciones. Diagnostico solo en lab.
#
# 1-ago-2026 (Ola F, F9 slot 2 = LSHIFT): alex 55844 SIN CAMBIOS —prueba de que
# el segundo slot se compila FUERA del torneo— y alex_lab 58088 -> 58252 (+164).
# Feature de lab, correctamente aislada.
#
# 1-ago-2026 (Ola D, hot path): alex 55872 -> 55844 (-28), alex_lab
# 58276 -> 58088 (-188). Encogen: una sola copia del procesado por tecla en vez
# de dos, y analog_matrix_effective_mode() con salida rapida por modo antes de
# leer default_layer_state.
#
# 1-ago-2026 (Ola C, config de torneo en git): alex 55388 -> 55872 (+484),
# alex_lab 57848 -> 58276 (+428). Decision, no fuga: profile_reset() ahora
# siembra afinado por tecla y el par SOCD A/D resolviendo keycodes contra el
# keymap vivo. Antes la afinacion fina vivia solo en la EEPROM de Launcher y un
# reset la revertia en silencio.
#
# 1-ago-2026 (Ola E, histograma de ventanas): alex 54132 -> 55388 (+1256),
# alex_lab 56656 -> 57848 (+1192). CRECE EL BINARIO DE TORNEO, y es deliberado.
#
# Esta fue la decision de Ola E en ese commit: el histograma vivia en los dos
# binarios para medir ambos. La separacion estable/lab de arriba la reemplaza:
# ahora el A/B se hace dentro de lab apagando/encendiendo el flag, y el binario
# final no carga instrumentacion.
# Lo que se compra: las ventanas dejan de reconstruirse en Python desde un evlog
# que pierde eventos en ráfaga, y pasan a contarse sin pérdida en firmware.
#
# 1-ago-2026 (Ola A, desminado): alex 54296 -> 54132 (-164), alex_lab
# 56680 -> 56656 (-24). ENCOGEN, y el motivo esta declarado: al des-unionar
# analog_key_t desaparece el guardado+restauracion defensivo de rpd_trig_sen en
# update_key_config() —con su llamada a analog_matrix_effective_mode()— y ademas
# `mode` se movio debajo del early return de update_raw_value(), donde se
# calculaba para tirarlo. Los dos borran codigo. No es "algo se compilo fuera sin
# querer": es codigo que dejo de hacer falta. El coste esta en RAM (+288 B de
# bss por la de-union), que este script no mide.
#
# 4-sep-2026 (latencia y robustez de torneo): alex 53200 -> 54380 (+1180 B).
# Optimizaciones del hot path validadas sin fuga de laboratorio:
# startup guard (150ms/8 scans), refcount de modificadores, cache de modo analogo,
# SOCD compacto con early-exit, saneador de perfiles en RAM, buffer ping-pong ADC
# (sin memcpy), inline NOPs de HC164 (32 nops), settle 14 us, ADC 15 ciclos y top
# dead zone 4. Invariante por simbolos 100% limpio.
#
# 4-sep-2026 (retiro definitivo de telemetria): lab 58484 -> 54276 (-4208 B),
# cal_lab 57436 -> 53876 (-3560 B). Telemetria retirada permanentemente;
# los binarios compilan sin la capa de telemetria ni hooks de logging.
BASE_ALEX=54380
BASE_LAB=54276
BASE_CAL_LAB=53876

BUILD_DIR="${BUILD_DIR:-.build}"
FAIL=0

# (1-ago) Sin esto, correr el script fuera de MSYS2 MinGW64 —donde vive el
# toolchain— daba "0 B" por tecla y un FALLA con delta -54132, que parece una
# regresion catastrofica y es sólo un PATH. El invariante tiene que fallar por
# lo que mide, no por donde se ejecuta.
if ! command -v arm-none-eabi-size >/dev/null 2>&1; then
    cat <<'EOF' >&2
ERROR: no encuentro arm-none-eabi-size en el PATH.

Este script necesita el toolchain ARM, que en esta maquina vive dentro de MSYS2
MinGW64. Desde Windows:

  C:\msys64\msys2_shell.cmd -mingw64 -defterm -no-start -where C:\Users\Alex\keychron-qmk \
    -c "./keyboards/keychron/k2_he/ansi/keymaps/alex/tools/check-size-invariant.sh"

No se ha comprobado nada. Esto NO es un fallo del invariante.
EOF
    exit 2
fi

# Exactamente el numero que imprime QMK en "Size after": size --target=ihex sobre
# el .hex, columna `data` (= text + data del .elf). El .bin en disco NO sirve —
# arrastra un offset fijo de padding y no es el numero que cita DEVELOPMENT.md.
size_of() {
    arm-none-eabi-size --target=ihex "$1" | awk 'NR==2 {print $2}'
}

check() {
    local name="$1" base="$2" hex="$BUILD_DIR/$1.hex"

    if [ ! -f "$hex" ]; then
        FAIL=1
        echo "FALLA    $name  (no existe $hex — compilalo primero)"
        return
    fi

    local now
    now=$(size_of "$hex")
    local delta=$((now - base))

    if [ "$delta" -eq 0 ]; then
        printf 'OK       %-28s %d B\n' "$name" "$now"
        return
    fi

    FAIL=1
    printf 'FALLA    %-28s %d B (baseline %d, delta %+d)\n' "$name" "$now" "$base" "$delta"
}

check keychron_k2_he_ansi_alex     "$BASE_ALEX"
check keychron_k2_he_ansi_alex_lab "$BASE_LAB"
check keychron_k2_he_ansi_alex_cal_lab "$BASE_CAL_LAB"

# ---------------------------------------------------------------------------
# Invariante por SIMBOLOS (1-ago)
# ---------------------------------------------------------------------------
# Los bytes son un proxy: fallan igual ante una fuga de lab que ante un bump de
# toolchain o un cambio del linker script. Esto comprueba lo que el invariante
# QUIERE decir — "el codigo de lab no esta en el binario de torneo" — mirando si
# sus simbolos existen. Las dos capas se complementan: los bytes son la alarma de
# humo, los simbolos el diagnostico.
LAB_ONLY_SYMBOLS='stretch|predictive|scan_probe|policy|telemetry|evlog|window_hist|awh_|hist_(dump|reset)|health_dump|bottom_out_confidence|confident_bottom'

check_symbols() {
    local elf="$BUILD_DIR/keychron_k2_he_ansi_alex.elf"
    if [ ! -f "$elf" ]; then
        FAIL=1
        echo "FALLA    invariante por simbolos (no existe $elf)"
        return
    fi

    local leaked
    leaked=$(arm-none-eabi-nm "$elf" 2>/dev/null | grep -iE "$LAB_ONLY_SYMBOLS" || true)

    if [ -n "$leaked" ]; then
        FAIL=1
        echo "FALLA    simbolos de lab presentes en el binario de TORNEO:"
        echo "$leaked" | sed 's/^/           /'
        echo "         El #if FLAG esta mal puesto: ese codigo ocupa flash y ciclos"
        echo "         en \`alex\` aunque el flag este apagado en runtime."
    else
        printf 'OK       %-28s sin simbolos de lab
' "invariante por simbolos"
    fi
}

check_symbols

if [ "$FAIL" -ne 0 ]; then
    cat <<'EOF'

El tamano se movio sin declararlo. La pregunta NO es "cuanto", es:

  ¿es una feature de lab que se fugo al binario de torneo, o un arreglo de
  correccion en codigo que ya estaba en torneo a proposito?

Si es lo primero, el #if FLAG esta mal puesto: el codigo sigue ocupando flash y
ciclos en `alex` aunque el flag este apagado en runtime. Arreglalo, no subas la
baseline.

Si es lo segundo, sube la baseline en este script, en su propio commit y con el
motivo escrito.
EOF
    exit 1
fi

echo "Invariante intacto."
