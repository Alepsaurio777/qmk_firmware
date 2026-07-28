#!/usr/bin/env bash
# Enforza el invariante de los dos binarios (ver DEVELOPMENT.md, "La regla de oro"):
# todo codigo experimental vive detras de un #if FLAG y el binario de TORNEO lo
# compila FUERA. La prueba de que quedo bien aislado es que `alex` no crece.
#
# Hasta ahora eso se verificaba a ojo, comparando el tamano contra un numero
# recordado. Esto lo vuelve un fallo del build en vez de un descuido: si `alex`
# crece y nadie declaro por que, el script para.
#
# Uso (desde MSYS2 MinGW64, en la raiz del repo):
#   make keychron/k2_he/ansi:alex keychron/k2_he/ansi:alex_lab
#   ./keyboards/keychron/k2_he/ansi/keymaps/alex/tools/check-size-invariant.sh
#
# Cuando el crecimiento es LEGITIMO (un arreglo de correccion en codigo que ya
# estaba en torneo a proposito, no una feature de lab que se fugo), se sube la
# baseline AQUI, en su propio commit, con el motivo en el mensaje. Ese commit es
# la declaracion. Bajarla sin motivo tambien falla: un `alex` que encoge de
# sorpresa significa que algo se compilo fuera sin querer.

set -u

# Baselines. 28-jul-2026: alex 54296 (era 54288; +8 por el arreglo B1, el
# id_dynamic_keymap_reset que faltaba en la lista de re-resolucion — correccion,
# no feature). alex_lab 56680.
BASE_ALEX=54296
BASE_LAB=56680

BUILD_DIR="${BUILD_DIR:-.build}"
FAIL=0

# Exactamente el numero que imprime QMK en "Size after": size --target=ihex sobre
# el .hex, columna `data` (= text + data del .elf). El .bin en disco NO sirve —
# arrastra un offset fijo de padding y no es el numero que cita DEVELOPMENT.md.
size_of() {
    arm-none-eabi-size --target=ihex "$1" | awk 'NR==2 {print $2}'
}

check() {
    local name="$1" base="$2" hex="$BUILD_DIR/$1.hex"

    if [ ! -f "$hex" ]; then
        echo "SALTADO  $name  (no existe $hex — compilalo primero)"
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
