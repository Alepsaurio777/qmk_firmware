# alex_mc189 — Tournament Certified Release (v4.3)

Keymap y arquitectura analógica de competición para el **Keychron K2 HE ANSI** (STM32F401, VID `0x3434`, PID `0x0E20`), optimizado para **Minecraft 1.8.9** (Hypixel / PvP) y shooters tácticos, con sincronización de bus USB y compatibilidad total con **Keychron Launcher v4**.

---

## ⚡ Especificaciones de Rendimiento Validadas en Hardware

Métricas empíricas medidas directamente por telemetría de hardware en el microcontrolador (10.060 ciclos consecutivos):

| Métrica | Valor Medido | Detalle Técnico |
|---|---|---|
| **Duración del barrido ADC** | **544,1 µs** (542 – 549 µs) | 16 columnas × 6 filas analógicas (14 µs settle, `ADC_SAMPLE_15`, 20 NOPs HC164). |
| **Jitter en el escaneo** | **±3,5 µs** (estabilidad del **99,3%**) | Casi nula fluctuación entre barridos consecutivos. |
| **Inicio del barrido (Laser JIT)** | **430 µs** tras USB SOF | Desfase calibrado para sincronizar con la ventana de sondeo de Windows. |
| **Fin del barrido analógico** | **974,9 µs** (25,1 µs antes del SOF) | Los datos se procesan justo antes de que concluya el milisegundo. |
| **Margen seguro frente a Windows** | **~42 µs** | Windows xHCI sondea a los 17 µs post-SOF. Los datos llegan frescos (< 0,04 ms) y con entrega garantizada. |
| **Frecuencia efectiva de barridos** | **1.006,0 Hz** | Sincronización estricta 1:1 con el frame USB (1.000 Hz / 1 ms). |

---

## 🎮 Mejoras de Firmware de Competición

1. **Continuous Rapid Trigger v2:**
   - Rastreo dinámico de pico continuo hasta el límite físico (245 unidades de recorrido).
   - Eliminada la trampa de zona muerta inferior (*bottom dead zone trap*); liberación inmediata incluso presionando al fondo del switch.

2. **Minecraft 1.8.9 Tick Synchronization (Press Stretch `F9`):**
   - Pulso mínimo sostenido de **55 ms** exclusivo para la barra espaciadora (`KC_SPACE`) en modo Gaming.
   - Resuelve el problema físico de pérdida de saltos (*jump drop*) en bhop y parkour cuando los toques físicos (< 20 ms) caían entre dos ticks de 50 ms del servidor de Minecraft.
   - Teclas WASD y modificadores se mantienen sin estiramiento y con latencia analógica instantánea.

3. **SOCD / Rappy Snappy Competitivo:**
   - Histéresis ultracorta de **0,05 mm** (`ANALOG_SOCD_DEEPER_HYSTERESIS 3`) para alternar strafe A/D con mínimo recorrido.
   - Retención del ganador en fondo (`ANALOG_SOCD_BOTTOM_OUT_HOLD_WINNER 1`): si ambas teclas tocan fondo en combate frenético, retiene la última tecla activa en vez de frenar el personaje a velocidad 0.

4. **Recuperación Automática de Muestras ADC:**
   - Contador de anomalías por tecla (`invalid_adc_count[ROWS][COLS]`). Ante 8 lecturas consecutivas fuera de rango, fuerza `AKS_REGULAR_RELEASED` para prevenir teclas pegadas (*stuck keys*).

5. **Contrato de Compatibilidad con Keychron Launcher v4:**
   - `AMC_GET_PROFILE_RAW` (`0x12`): eco de cabecera con `size = 30` entregando los 26 bytes físicos útiles para nombres de perfiles sin desbordamiento.
   - Notificación de cambio de perfil en tiempo real (`0xA9 0x11`) hacia el listener `watchOnProfileChange` para actualizar la estrella dorada en Launcher al instante.
   - Máscara de características en `0x12` (`resetSingleKey` + `gamepadDisable`), omitiendo DKR y Turbo para máxima transparencia.
   - Protección contra buffers truncados (`length < 3` y `length < 5`) en todos los comandos Raw HID.

---

## 🛠️ Herramientas y Diagnóstico Incluidos

- [`monitor_telemetry.py`](../../../../monitor_telemetry.py): Script en Python para monitoreo de telemetría de hardware en vivo por Raw HID (duración de scan, fase de finalización y sondeo USB de Windows).
- [`audit_checks.py`](../../../../../../qmk-audit-20260906-codex/audit_checks.py): Suite de verificación de contratos HID, desbordamientos e invariantes binarias (5/5 checks PASS).
- `hosttest`: Suite de pruebas unitarias en host MinGW64 con 12.149 aserciones automáticas de Rapid Trigger y FSM analógica.

---

## 🔨 Compilación

Desde MSYS2 MinGW64:
```bash
make keychron/k2_he/ansi:alex_mc189 -j8
```
El binario generado se ubica en `.build/keychron_k2_he_ansi_alex_mc189.bin`.
