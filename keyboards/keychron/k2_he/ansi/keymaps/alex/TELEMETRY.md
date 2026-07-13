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

- **Activar/desactivar**: `Fn + Y` (capa WIN_FN). Solo funciona con el
  interruptor en Win/productividad.
- **En Gaming no existe**: el toggle se ignora y, si el stream estaba activo y
  mueves el interruptor a Gaming, se corta solo. Razón: el streaming compite
  por CPU y por el endpoint USB con el input — jamás debe convivir con PvP.
- **Cliente**: `tools/telemetry_client.py` (requiere `pip install hidapi`).
  Grafica el travel en vivo y calcula ruido pico-a-pico por tecla.
- **VIA/Launcher comparten endpoint con el stream.** Si Launcher intenta
  conectar con el stream activo, sus respuestas quedan pisadas por paquetes de
  telemetría y muestra "not-connect". Desde el 13-jul el firmware apaga la
  telemetría automáticamente en cuanto llega cualquier comando de
  configuración (`via_command_kb`); aun así, apágala con `Fn+Y` al terminar
  una sesión de medición — el cliente al cerrarse NO apaga el stream del
  teclado.

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
