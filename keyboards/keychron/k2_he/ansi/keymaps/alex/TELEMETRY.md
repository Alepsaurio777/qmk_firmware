# Telemetría de profundidad (Fase 0 del ROADMAP)

## Para qué es

Transmite en vivo, por el endpoint Raw HID que ya usa VIA, el travel analógico
(0–240) y el estado lógico de 6 teclas críticas de PvP: **W, A, S, D, espacio y
LShift**. Sirve para tres cosas concretas:

1. **Medir el ruido real del sensor**: con el dedo apoyado sin presionar, el
   pico-a-pico del travel es la sensibilidad mínima honesta de rapid trigger.
   Cualquier sensibilidad por debajo de esa cifra genera actuaciones fantasma.
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

- **DOBLE**: un release→press del mismo key en menos de `DOUBLE_MS` (40 ms por
  defecto) — re-disparo sospechoso / rebote.
- **MARGINAL**: un press cuyo travel en el instante fue < `MARGINAL_TRAVEL`
  (30 = ~0.5 mm) — actuación cerca del umbral, típica de un fantasma.

### Cómo decidir

Juega una sesión real y larga (para que entre el drift térmico). Si el resumen
sale **limpio** (0 dobles, 0 marginales) → el firmware no tiene fantasmas y la
histéresis adaptativa no hace falta. Si se **concentran en LShift/LCtrl** → es
el caso de Wooting: implementar histéresis/dead-zone adaptativa cerca del
reposo en esos modificadores. Decidir con los datos, no antes.

Formato de paquete (32 B): `[0]=0xEC [1]=version [2]=N`, luego N×5 bytes
`[t_lo, t_hi, key_idx, pressed, travel]`. key_idx: 0=W 1=A 2=S 3=D 4=SPC
5=LSFT 6=LCTL.
