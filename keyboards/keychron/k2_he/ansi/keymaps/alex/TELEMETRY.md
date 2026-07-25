# Telemetría de profundidad (Fase 0 del ROADMAP)

## Para qué es

Transmite en vivo, por el endpoint Raw HID que ya usa VIA, el travel analógico
(0–240) y el estado lógico de 6 teclas críticas de PvP: **W, A, S, D, espacio y
LShift**. Sirve para tres cosas concretas:

1. **Medir el ruido real del sensor**: con el dedo apoyado sin presionar, el
   pico-a-pico del travel es la sensibilidad mínima honesta de rapid trigger.
   Una sensibilidad por debajo de esa cifra eleva el riesgo de chatter.
   (La industria vende 0.1 mm; el ruido físico suele estar en 0.05–0.1 mm.)
2. **Medir la deriva térmica**: comparar el valor de reposo recién encendido vs
   tras 1–2 h de sesión. Con la auto-calibración apagada
   (`ANALOG_AUTO_CALIBRATION_ENABLE 0`), este dato decide si la Fase 2b del
   ROADMAP (compensación lenta de drift) es necesaria o no.
3. **Verificar que la config aplicada es la que ejecuta la ISR** (el bug "la UI
   miente" del changelog de NuPhy): lo que reporta la telemetría sale del mismo
   estado que usa la lógica de actuación, no de una copia en EEPROM.

## Cómo se usa

- **Activar/desactivar**: automático — el cliente lo arma por comando HID al
  arrancar y lo apaga al salir. Solo funciona con el interruptor en
  Win/productividad (en Gaming el stream compite con el input y se corta solo).
- **En Gaming no existe**: el toggle se ignora y, si el stream estaba activo y
  mueves el interruptor a Gaming, se corta solo. Razón: el streaming compite
  por CPU y por el endpoint USB con el input — jamás debe convivir con PvP.
- **Cliente**: `tools/telemetry_client.py` (requiere `pip install hidapi`).
  Grafica el travel en vivo y calcula ruido pico-a-pico por tecla.
- **VIA/Launcher comparten endpoint con el stream.** Si Launcher intenta
  conectar con el stream activo, sus respuestas quedan pisadas y muestra
  "not-connect". El firmware apaga los diagnósticos automáticamente en cuanto
  llega cualquier comando de configuración que no sea el nuestro (`0xEE`), y
  el cliente los apaga al salir. Aun así, no dejes Launcher abierto durante
  una medición.

## Formato de paquete (32 bytes)

| Offset | Contenido |
|---|---|
| 0 | `0xED` (magic) |
| 1 | versión (`1`) |
| 2–3 | `timer_read16` del firmware, little-endian (ms) |
| 4 | secuencia (u8, detecta paquetes perdidos) |
| 5 | número de teclas N (=6) |
| 6+2i | travel de la tecla i (0–240; 240 = 4.0 mm a fondo, unidad = 1/60 mm) |
| 7+2i | estado lógico (1 = registrada como pulsada) |

Orden de teclas: W, A, S, D, espacio, LShift (definido en `telemetry.c`).
Cadencia: un paquete cada 5 ms (200 Hz) desde `housekeeping_task_user` — fuera
del hot path de escaneo; el scan no se toca.

Las teclas se declaran por **keycode**, no por coordenada (24-jul), y se
resuelven a posiciones de matriz contra `ANALOG_POLICY_LAYER` en
`keyboard_post_init_user` y tras cada remap desde Launcher. Antes era una tabla
`{row, col}` fija que mentía en silencio en cuanto remapeabas: el stream seguía
leyendo el hueco viejo. Una tecla cuyo keycode no esté en la capa base de Gaming
reporta travel 0, en vez de leer fuera de rango.

## Qué mirar (protocolo de medición)

1. **Ruido en reposo**: 30 s sin tocar → pico-a-pico por tecla. Anotar.
2. **Ruido con dedo apoyado**: 30 s con dedos sobre WASD sin intención de
   pulsar → esa cifra manda para elegir `rt_up` (release) de la Fase 1.
3. **Drift**: valor medio de reposo al encender vs tras 1 h. Si supera ~2–3
   unidades de travel, implementar Fase 2b.
4. **Curva completa**: pulsación lenta de 0 a fondo → debe llegar a 240 sin
   saltos; saltos = problema de calibración o de LUT.

Registrar resultados en TEST-RESULTS.md del lab (mismo protocolo que Ornithe).

## Logger de eventos (mistype-hunt)

A diferencia del stream de travel (200 Hz, apagado en Gaming), el logger de
eventos registra **solo los cambios de estado** (press/release) de las teclas
de movimiento — W, A, S, D, espacio, LShift, **LCtrl** — con el travel del
instante. Al ser event-driven cuesta ~cero cuando no pasa nada, así que **sí
corre en Gaming**: el objetivo es cazar pulsaciones fantasma durante juego
real, sobre todo en los modificadores mantenidos (LShift/LCtrl), que es el
modo de fallo que Wooting documentó (Phantom Shift Detection).

### Uso

- **No hay tecla que pulsar.** El cliente arma el logger solo, por comando HID
  (`0xEE`). Arráncalo, pasa el interruptor a Gaming y juega — el logger sigue
  registrando en Gaming.
- **Cliente**: `python tools/telemetry_client.py --events` (opcional
  `--csv sesion.csv`). Imprime en vivo solo las **anomalías** (dobles y presses
  marginales) y al salir (Ctrl+C) un resumen por tecla.
- Se auto-apaga si Launcher habla (comparte endpoint).

> Antes esto colgaba de un keycode (Fn+U). Se quitó: el keymap de VIA vive en
> **EEPROM** y puede dejar cualquier tecla desasignada, así que el toggle era
> inalcanzable tras flashear sin resetear el keymap. Un comando HID siempre
> llega, y de paso el diagnóstico queda desacoplado del keymap.

### Qué marca

- **candidato de rebote**: un release→press del mismo key en menos de
  `PHANTOM_MS` (10 ms). Es una firma estrecha de chatter, no una prueba de
  ausencia de todo falso input; el CSV conserva el contexto para confirmarlo.
- **rápido** (info): release→press de 10-20 ms — humanamente posible pero
  raro; tapeo agresivo, no necesariamente un fallo.

(Histórico: hubo un criterio "MARGINAL = press con travel < 30". Se eliminó:
con rapid trigger medía la configuración del usuario, no fantasmas.)

### Histograma de ventanas OFF (mecánica de tick de MC 1.8.9)

Al salir, el resumen incluye por tecla la distribución de **ventanas OFF**
(release→re-press, solo taps < 1 s; resolución 1 ms; columna `off_ms` del
CSV). Por qué importa: el cliente 1.8.9 muestrea el *estado* de las teclas de
movimiento **una vez por tick (50 ms)** — una ventana OFF de `d` ms solo es
observada con probabilidad ~`d/50` cuando `d < 50`. En cristiano:

- **W**: ventana no vista = w-tap que NO resetea sprint (golpe sin el KB extra).
- **Espacio**: ventana no vista = `jumpTicks` sin resetear → el siguiente
  salto puede retrasarse hasta 500 ms justo bajo combo.

Un `%<50` alto en W/SPC con el build de torneo es la evidencia que justifica
el **release-stretch** (F6, solo `alex_lab`): con él activo, W y SPC no deben
mostrar ninguna ventana < 55 ms — si aparecen, el stretch no está actuando.

### Cómo decidir

Juega una sesión real y larga (para que entre el drift térmico). Si el resumen
sale sin candidatos sub-10 ms → no hay evidencia de chatter con esa firma; no
autoriza a concluir que no existe ninguna otra clase de falso input. Si los
candidatos se **concentran en LShift/LCtrl** → es
el caso de Wooting: implementar histéresis/dead-zone adaptativa cerca del
reposo en esos modificadores. Para F6, decide el histograma de ventanas OFF,
no la sensación. Decidir con los datos, no antes.

Formato de paquete **v3** (32 B): `[0]=0xEC [1]=3 [2]=N`, luego N×5 bytes
`[t_lo, t_hi, key_idx, flags, travel]`. key_idx: 0=W 1=A 2=S 3=D 4=SPC
5=LSFT 6=LCTL. `[28]=secuencia`; `[29..30]=eventos descartados` LE. El
cliente rechaza otras versiones y marca como incompleta cualquier pérdida.

`flags` es máscara de bits: **bit0 = pulsada**, **bit1 = flanco FÍSICO** (crudo,
antes de los stretches F6/F9). En v2 ese byte era un booleano.

### Los dos flujos: reportado vs físico

El evlog cuelga de `process_record_user`, o sea **aguas abajo** de F6 y F9. En un
build de torneo eso da igual (sin stretch, reportado == físico), pero en un build
lab el clamp se come justo los eventos que hay que contar: los taps de <55 ms en
W/SPC. Medir las dos cosas exigía dos sesiones con drills distintos, y que dos
drills «iguales» sean comparables es la mayor fuente de error del A/B.

Con v3, el firmware emite además el flanco físico vía
`analog_matrix_physical_edge_hook` (weak en `common/`, override en
`telemetry.c`), que se dispara **dentro** de las funciones de stretch — ya
detectan el flanco y sólo corren para las 2-3 teclas de la whitelist, así que no
cuesta nada en el barrido. Reportan el keycode con el que se declaró el slot, no
sólo la coordenada, para no tener que leer el keymap en el hot path.

Cuando F6 y F9 se encadenan en la misma tecla, el flanco físico lo reporta **el
primero de la cadena** (F9), que es el único que ve el estado sin alterar; F6 se
calla en esa tecla para no emitir un flanco falso desplazado hasta 55 ms.

El cliente lleva los dos flujos por separado e imprime el histograma de ventanas
OFF **dos veces** — `REPORTADO` y `FÍSICO` — de la misma sesión, así que son
directamente comparables. El CSV gana una columna `src` (`rep` / `fis`).

## Volcado de la política resuelta (`--policy`)

`python telemetry_client.py --policy` manda `0xEE 0x20` y el firmware responde
con un paquete `[0]=0xEB [1]=1` + 29 bytes: flags de qué features están
compiladas, las máscaras predictivas por fila, y las coordenadas resueltas de los
slots de F6 y F9 (empaquetadas `(row << 4) | col`, `0xFF` = sin resolver).

Sirve para responder en dos segundos la pregunta que antes exigía una sesión de
evlog: **¿a qué teclas físicas aterrizaron las whitelists declaradas por
keycode?** Útil sobre todo tras un remap en Launcher.

Sólo responde en builds con política activa (lab). En torneo `ANALOG_POLICY_NEEDED`
es 0, no hay nada que volcar, y compilarlo rompería el invariante de que el
binario de torneo no crece por diagnóstico opcional — el `0x20` cae ahí al camino
de «comando desconocido» y apaga los diagnósticos.
