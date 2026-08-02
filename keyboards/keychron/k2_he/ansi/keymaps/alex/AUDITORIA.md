# Guía de auditoría — las 6 olas del 1-ago-2026

Qué revisar, en qué orden, y qué debería ser cierto. Ordenado por **riesgo**, no
por número de ola: primero lo que puede morderte en partida.

## Lo primero: qué está verificado y qué no

| | Estado |
|---|---|
| Compila (los dos binarios, sin warnings nuevos) | ✅ verificado |
| Invariante de tamaño y de símbolos | ✅ verificado |
| Lógica (FSM, F6/F9, resolución, histograma) | ✅ 111 comprobaciones, dos configuraciones |
| **Comportamiento en el teclado real** | ❌ **NADA de esto se ha flasheado** |

**Esa última fila es la importante.** No tengo el teclado. Todo lo de abajo está
verificado por compilación y por tests de host sobre el código real, pero el
histograma no ha visto un dedo, el par SOCD no se ha sembrado en una EEPROM de
verdad y F9-LSHIFT no ha corrido en una partida. Audita en ese orden: lo que
toca hardware primero.

Comandos base (desde MSYS2 MinGW64, en la raíz del repo):

```bash
make keychron/k2_he/ansi:alex keychron/k2_he/ansi:alex_lab
./keyboards/keychron/k2_he/ansi/keymaps/alex/tools/check-size-invariant.sh
cd keyboards/keychron/common/analog_matrix/hosttest && make run
```

Estado actual: `alex` 55844 B, `alex_lab` 58252 B, invariante de símbolos limpio,
111 comprobaciones en verde.

---

## 1. RIESGO ALTO — Ola C: el reset de perfil ahora hace algo distinto

**Qué cambió:** `profile_reset()` siembra un par SOCD (A/D, deeper-travel) que
antes no sembraba.

**Por qué es lo primero que auditar:** es el único cambio que **altera el
comportamiento observable del teclado sin que toques nada**. Si haces un "Reset
Profile" en Launcher, o si una migración de layout de EEPROM lo dispara sola,
ahora sales con SOCD activo en A/D donde antes salías sin él.

**Qué comprobar en hardware:**
1. Flashea `alex`. Haz un *Reset Profile* del perfil gaming desde Launcher.
2. Mira en Launcher que aparece el par SOCD en A/D. Si no aparece, la resolución
   por keycode no encontró las teclas — mira que A y D estén en la capa 0.
3. Pulsa A y D a la vez y verifica que gana la más hundida.

**Bandera roja:** si SOCD **no** era lo que querías por defecto, quítalo —
`socd_gaming[]` en `ansi/profiles.c`. Está en zona gris (no prohibido en
Minemen/Hypixel, baneado en CS2/ESL) y lo sembré porque `config.h` y
`DEVELOPMENT.md` lo tratan como feature viva, pero la decisión es tuya.

**Lo que dejé deliberadamente vacío:** `tuning_gaming[]` en el mismo fichero. Es
la tabla de actuación/sensibilidad por tecla. No inventé tus números — sembrar
una actuación equivocada sería peor que no sembrar ninguna, porque un reset
dejaría el teclado en una config que nadie eligió con apariencia de ser la buena.
**Rellénala desde Launcher y commitea**: eso es lo que cierra del todo el hueco
de "la config de torneo no está en git".

---

## 2. RIESGO ALTO — Ola A: la de-unión de `analog_key_t`

**Qué cambió:** `rpd_trig_sen`, `okmc_idx`, `js_axis` y `hold` dejan de
compartir byte, y desaparece el guardado+restauración defensivo que lo
compensaba en `update_key_config()`.

**Por qué auditar:** toca la estructura que usa todo el camino de teclas. La
lógica no cambió, pero si me equivoqué al quitar la curita, el síntoma sería una
sensibilidad de rapid trigger incorrecta en teclas con modo avanzado.

**Qué comprobar:**
1. `git show 3e4a817 -- keyboards/keychron/common/analog_matrix/analog_matrix_type.h`
   — que los cuatro campos son ahora `uint8_t` independientes.
2. En hardware: pon una tecla en modo DKS/Toggle/Gamepad desde Launcher, vuelve a
   ponerla en Rapid, y verifica que su sensibilidad RT es la que configuraste.
   Ése es exactamente el camino donde vivía el bug original.

**Verificación ya hecha:** `analog_key_matrix` sólo aparece en `memset` — nunca
se serializa a EEPROM ni a HID — así que la de-unión no puede corromper datos
guardados. Lo que sí cruza esas fronteras es `analog_key_config_t`, que **no
toqué**.

---

## 3. RIESGO MEDIO — Ola E: el histograma vive en el binario de torneo

**Qué cambió:** +1256 B permanentes en `alex` por diagnóstico, y un gancho nuevo
en el barrido que corre para cada tecla.

**Por qué auditar:** es la desviación consciente de "torneo mínimo" que
aprobaste. Merece que compruebes que el coste en *tiempo* también es aceptable,
no sólo el de flash.

**Qué comprobar:**
1. Flashea `alex_lab` (que lleva el probe de timing) y mira la duración del
   barrido en la telemetría. Referencia previa: **880–925 µs**. El gancho del
   histograma es un test de bit para ~93 teclas y trabajo real sólo para 3, así
   que no debería moverse de forma apreciable. **Si el barrido supera ~950 µs,
   dímelo: eso comería el margen del frame de 1 ms.**
2. Comandos nuevos: `0x30` reset, `0x31` volcado (key_idx, capa), `0x32` salud.
   Comprueba que pedirlos **no apaga** la sesión de diagnóstico — ésa era la
   trampa del `default` del switch y es lo que más fácil se rompe al añadir un
   comando.

**Nota:** el cliente Python todavía **no sabe leer** estos paquetes (0xEA / 0xE9).
Eso es trabajo pendiente, ver la sección final.

---

## 4. RIESGO MEDIO — Ola D: el refactor del camino crítico

**Qué cambió:** una sola copia del procesado por tecla
(`analog_process_key_sample`), y salida rápida en `analog_matrix_effective_mode`.

**Por qué es menos arriesgado de lo que suena:** va detrás de la Ola B a
propósito. La cadena F9→F6, la FSM y el histograma están cubiertos por los 111
tests, y los dos binarios **encogieron**.

**Qué comprobar:**
1. `git show 197d0eb` — que la secuencia dentro de `analog_process_key_sample`
   es idéntica a la que había duplicada, con F9 **antes** de F6.
2. En hardware: que W y espacio siguen sintiéndose igual en `alex`. Es
   passthrough puro ahí, así que cualquier diferencia percibida es una señal.

---

## 5. RIESGO BAJO — Ola B: la extracción a `action_stretch.c`

**La mejor evidencia ya está hecha:** tras mover ~300 líneas de `analog_matrix.c`
a `action_stretch.c`, los dos binarios salieron **byte a byte idénticos**
(54132 / 56656). Eso es prueba de que el traslado fue neutro; no hace falta
auditarlo línea a línea.

**Lo que sí merece una mirada:** los tests. Léelos como documentación ejecutable
del contrato — `hosttest/test_stretch.c` es el que pinea lo que F6/F9 prometen.
Si alguna aserción no coincide con lo que tú creías que hacía el firmware, **esa
discrepancia es el hallazgo**, y quiero saberla.

Dos cosas que los tests dejaron por escrito y no estaban en ningún sitio:
- La **zona muerta del fondo**: con travel ≥ 216 el release por rapid trigger NO
  dispara. Es deliberado (anti-chatter al bottom-out) y afecta a quien machaca el
  espacio a fondo.
- Dos taps de espacio muy rápidos se **funden en uno** con F9. No es un bug, es
  aritmética; en 1.8.9 no cuesta nada porque `jumpTicks` limita el ritmo muy por
  debajo, pero está pineado por si algún día se sube `ANALOG_PRESS_STRETCH_MS`.

**Un bug real que los tests cazaron**, en código que yo mismo acababa de
escribir: la primera ventana tras un reset del histograma está truncada y se
contaba igual, sesgando los cubos bajos hacia arriba — justo la dirección que
habría **inflado el argumento a favor de F6/F9**. A ojo en una traza no se
habría visto nunca.

---

## 6. RIESGO BAJO — Ola F: F9 en LSHIFT

Está **apagado en torneo** y verificado: `alex` no se movió ni un byte al
añadirlo. Sólo vive en `alex_lab`.

**No lo promociones por sensación.** El drill que decide si merece la pena
siquiera es el D1 del plan de diagnóstico: medir en el binario de **torneo** qué
fracción de presses de LSHIFT al bridgear dura menos de un tick. Si sale ~0, se
apaga para siempre y habrá costado 20 minutos.

---

## Lo que queda pendiente, y por qué

Soy explícito con esto para que no lo descubras a medias:

1. **El cliente Python no lee los paquetes nuevos** (0xEA histograma, 0xE9
   salud). El firmware los emite; falta el lado del PC. Es lo siguiente que
   haría, y sin ello el histograma no es usable todavía.

2. **El `--record` / `--replay` del plan no está.** Los tests de host cubren la
   parte de "validar lógica contra un contrato"; falta la parte de "reproducir
   una traza real de tu sesión a través de dos políticas". El harness está
   montado para soportarlo (reloj falso, keymap falso, los `.c` reales), así que
   es una extensión, no un rediseño.

3. **`tuning_gaming[]` está vacía**, por la razón de la sección 1.

4. **`set_mods()` sigue con el bug del refcount**, documentado y verificado como
   inalcanzable en este build. Se arregla el día que vuelva wireless o que el
   parche se upstree — está escrito en `quantum/action_util.c`.

5. **Instalé `mingw-w64-x86_64-gcc`** en tu MSYS2 (lo autorizaste). Los tests de
   host lo necesitan. Se quita con `pacman -R mingw-w64-x86_64-gcc` si molesta.

---

## Resumen de comprobaciones

```
[ ] Reset de perfil → aparece el par SOCD A/D en Launcher, y lo quiero
[ ] Tecla en modo avanzado → vuelta a Rapid → sensibilidad RT correcta
[ ] Duración del barrido en alex_lab sigue en 880-925 us
[ ] Comandos 0x30/0x31/0x32 no apagan la sesión de diagnóstico
[ ] alex se siente igual que antes en W y espacio
[ ] Leer hosttest/test_stretch.c y confirmar que el contrato es el que creías
[ ] Rellenar tuning_gaming[] desde Launcher y commitear
```
