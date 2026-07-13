# Plan de implementación — K2 HE firmware custom
### Basado en la investigación de 4 agentes (Wooting, Razer/SteelSeries, open-source, gama media) — 12-jul-2026

---

## Contexto: dónde estamos parados

El fork actual ya tiene resuelto lo que a la mayoría de vendors les costó años de
parches: rapid trigger continuo en punto fijo, escaneo ADC a 18 MHz con ~13 µs por
barrido, debounce cero (la histéresis analógica hace ese trabajo), 8 kHz de polling
real y determinístico (`KEYCHRON_FIXED_REPORT_RATE`), y un lockdown de modo Gaming
que ningún teclado comercial ofrece.

La investigación concluyó que **solo Wooting tiene ventajas técnicas reales sobre
este fork, y son tres**: sincronización scan↔USB, sensibilidades de RT separadas
para bajada/subida, y su cadena de calibración de fábrica. Las dos primeras son
portables; la tercera se compensa con calibración robusta en firmware (Fase 2).

El resto de la industria (DrunkDeer, NuPhy, Akko, Meletrix, MonsGeek) no aporta
ideas — aporta **advertencias**: sus changelogs documentan los modos de fallo de
un firmware HE, y este plan los usa como checklist negativo.

**El talón de Aquiles actual del fork es conocido**: la auto-calibración está
desactivada (`ANALOG_AUTO_CALIBRATION_ENABLE 0`) para sacar el trabajo del hot
path. Eso fue correcto para latencia, pero deja al teclado ciego ante la deriva
térmica del sensor Hall — que Akko documenta oficialmente como real. Hoy el
margen de seguridad son las dead zones fijas (`TOP_OUT_DEAD_ZONE_GAMING 12`,
`TYPING 20`). La Fase 2 reemplaza ese margen bruto por calibración inteligente
fuera del hot path, que es estrictamente mejor en ambos ejes.

---

## Orden del plan y por qué

```
Fase 0  Telemetría          → sin datos no se puede tunear nada de lo demás
Fase 1  FSM de RT (libhmk)  → el corazón; todo lo demás se apoya en él
Fase 2  Calibración robusta → fiabilidad; usa la telemetría de F0 para validarse
Fase 3  Sync scan↔SOF USB   → último 0.125 ms de jitter
Fase 4  SOCD por profundidad→ se apoya en la FSM de F1
Fase 5  Mod-tap por profundidad (productividad) → feature original
```

La telemetría va primero aunque sea la menos glamorosa: cada fase posterior
necesita ver el travel real para ajustar umbrales con datos y no a sensación.
DrunkDeer y NuPhy lanzaron RT sin esa visibilidad y el resultado fueron años de
"deadzone update" en sus changelogs.

---

## Fase 0 — Telemetría de profundidad por Raw HID

**Qué es.** Un modo debug, activable solo desde WIN_FN (nunca en Gaming), que
transmite por el endpoint Raw HID existente de Keychron el travel en vivo de
hasta 8 teclas seleccionadas, más sus valores de calibración (rest, bottom-out).

**Por qué.** Tres usos concretos:
1. Tunear las sensibilidades de RT de la Fase 1 viendo el ruido real del sensor
   (¿cuánto tiembla el valor con el dedo apoyado sin presionar? esa cifra ES la
   sensibilidad mínima honesta — la industria vende 0.1 mm y hasta 0.01 mm, pero
   el consenso técnico es que bajo ~0.2 mm el ruido + temblor del dedo genera
   dobles actuaciones).
2. Detectar deriva térmica: comparar el valor de reposo en frío vs tras una hora
   de sesión. Con auto-cal apagada, este dato dice si la Fase 2 es urgente o no.
3. Verificar que la config que dice la EEPROM es la que ejecuta la ISR (el bug
   "la UI miente" de NuPhy: los parámetros de RT no se aplicaban).

**Diseño.**
- Nuevo comando en `keychron_raw_hid.c` (hay espacio en el dispatch de comandos):
  `0xE0 = start stream (payload: lista row/col)`, `0xE1 = stop`, `0xE2 = dump
  calibración completa`.
- El streaming NO va en el hot path: `housekeeping_task_user` copia los valores
  ya calculados por el scan a un buffer y los envía cada ~10 ms (100 Hz basta
  para tunear; el scan sigue intacto).
- Bloqueado en gaming mode con el mismo patrón del lockdown actual.
- Cliente: script Python con `hidapi` que grafique en vivo (matplotlib animado).
  ~80 líneas; se guarda en `keymaps/alex/tools/`.

**Validación.** Presionar W lentamente y ver la curva completa 0→255; dejar el
dedo apoyado y medir el ruido pico-a-pico; anotar en TEST-RESULTS del lab.

**Esfuerzo estimado:** una tarde. **Riesgo:** casi nulo (no toca el scan).

---

## Fase 1 — FSM de rapid trigger con press/release separados (libhmk)

**Qué es.** Reemplazar la lógica actual de rapid trigger por la máquina de
estados de libhmk, que es el diseño más limpio encontrado en toda la
investigación (mejor que lo que se puede inferir de Wooting, y verificado
leyendo el código real).

**El algoritmo, explicado.** Por tecla se guardan 3 cosas: la distancia actual
`d` (0–255), un `extremum` (el pico o valle más reciente del recorrido), y una
dirección `key_dir` con 3 estados:

- **INACTIVE** (tecla arriba de la zona): si `d` cruza el punto de actuación
  hacia abajo → press, guarda `extremum = d`, pasa a DOWN.
- **DOWN** (pulsada, bajando): `extremum` persigue el punto más profundo. Si la
  tecla sube más de `rt_up` desde ese punto → **release**, re-ancla `extremum`,
  pasa a UP. Si vuelve a la zona de reposo → release y a INACTIVE.
- **UP** (soltada, subiendo): `extremum` persigue el punto más alto. Si la tecla
  baja más de `rt_down` desde ese punto → **press**, re-ancla, vuelve a DOWN.

Detalles que hacen superior este diseño:
- `rt_down` y `rt_up` son **independientes por tecla**. `rt_up = 0` significa
  "usa el mismo valor que rt_down" (modo simétrico = comportamiento actual).
- RT continuo es un caso particular gratis: `reset_point = continuous ? 0 :
  actuation_point`. No hay rama especial.
- Todo entero de 8 bits, sin timers, sin floats, O(1) por tecla por scan.
  Compatible directo con el punto fijo del fork (`ANALOG_FIXED_POINT_TRAVEL`).
- Un solo `extremum` en vez de peak/valley separados: se re-ancla en cada
  transición, imposible que quede estado obsoleto (el bug de "release
  prematuro" que SteelSeries parcheó en el Apex Pro).

**Complemento de minipad** (segundo firmware analizado): la histéresis de
*entrada* y *salida* de la zona RT también puede ser asimétrica — entra en zona
al 55% del umbral, sale al 67.5%. Eso evita chatter cuando el dedo se queda
justo en el borde de la zona de actuación. Se integra como dos umbrales en el
estado INACTIVE↔activo.

**Por qué importa para MC 1.8.9.** El caso de uso concreto:
- **Espacio (jump-reset)**: release ultra corto (~0.15 mm) para resetear el
  salto lo antes posible al aflojar, pero re-press algo mayor (~0.3 mm) para
  que la vibración del pulgar tras el salto no genere un segundo salto fantasma
  que rompa el timing del combo. Con sensibilidad simétrica esto es imposible:
  o ambos rápidos (fantasmas) o ambos lentos (reset tardío).
- **W (w-tapping)**: mismo razonamiento — soltar sprint rápido, re-engancharlo
  con intención.

**Dónde tocar.**
- `common/analog_matrix/action_rapid_trigger.c`: la FSM va aquí, reemplazando
  la lógica actual pero conservando la interfaz (que `analog_matrix.c` llama).
- `common/analog_matrix/analog_matrix_type.h`: añadir `rt_up` al struct de
  config por tecla (hoy hay un solo valor de sensibilidad). Cuidado con el
  layout persistido en EEPROM — bump de versión de config o migración.
- Los macros de tu `config.h` (`STATIC_HYSTERESIS_GAMING`,
  `_FAST_KEY`, `ANALOG_ADAPTIVE_SHALLOW_HYSTERESIS_GAMING`) se remapean a
  valores por defecto de `rt_down`/`rt_up`, manteniendo el comportamiento
  actual como punto de partida antes de tunear.
- El código fuente de referencia está copiado en el scratchpad de la sesión
  (`hmk/src/matrix.c`) y en github.com/peppapighs/libhmk.

**Validación.** Con la telemetría de F0: grabar una sesión de bhop en el lab
Ornithe y verificar cero releases fantasma con release=0.15 mm; comparar
count de saltos registrados vs intencionados. Registrar en TEST-RESULTS.md.

**Esfuerzo:** 1–2 días con calma. **Riesgo:** medio — toca el corazón del input.
Mitigación: flag de compilación para alternar FSM nueva/vieja durante pruebas.

---

## Fase 2 — Calibración robusta y dead zones inteligentes

**Qué es.** El paquete de fiabilidad que sustituye la auto-calibración apagada,
sin devolver trabajo al hot path. Es una síntesis de lo mejor de los tres
firmwares open-source analizados:

**2a. Reposo calibrado en boot, no en runtime (libhmk).**
Ventana de 500 ms al arrancar: EMA del ADC en reposo, con la regla de que el
valor solo se acepta si mejora en más de un épsilon (5 cuentas ADC). Después
queda congelado. Coste en runtime: cero.

**2b. Variante anti-drift (macrolev), opcional según lo que diga la telemetría.**
El valor de reposo puede seguir ajustándose en runtime pero SOLO en una
dirección (el drift térmico va hacia un lado predecible), con EMA lento y
únicamente cuando la tecla lleva N ciclos quieta. Coste: una comparación por
tecla por scan — aceptable si la F0 demuestra que el drift existe en sesiones
largas. Si no existe, no se implementa (dato, no fe).

**2c. Bottom-out por tecla que solo crece (libhmk).**
El fondo del recorrido se aprende: arranca en un valor conservador persistido
en EEPROM y solo se expande si el ADC lo supera con épsilon. Nunca se contrae
→ el rango dinámico no puede degradarse solo. La escritura a EEPROM va en
housekeeping, jamás en el scan (el bug de corrupción de flash de NuPhy <v110
vino de escribir config en caliente).

**2d. Arranque seguro (minipad).**
Una tecla no puede actuar hasta que su calibración sea coherente: rango
`rest − bottom_out` mayor que un mínimo. Hasta entonces reporta "suelta".
Elimina la clase de bug de Akko V111 donde una calibración corrupta dejaba
teclas disparadas permanentemente o muertas.

**2e. Sanidad al cargar EEPROM.**
Clamp de todos los valores de calibración a rangos físicamente posibles al
arrancar. Un byte corrupto degrada a default, nunca a comportamiento loco.

**2f. Dead zones por tecla derivadas, no fijas.**
Con 2a–2e, las dead zones dejan de ser el margen bruto actual (12/20) y pasan a
ser: superior = épsilon sobre el reposo calibrado; inferior = clamp a 255 cerca
del bottom-out aprendido. Configurables por tecla (la lección de Luminkey, el
único vendor chino que lo hace bien) — espacio puede tener menos dead zone
superior que una tecla del pinky.

**Filtrado (transversal).** El EMA entero de libhmk — `y += (x − y) >> 4`,
α=1/16, sin división — aplicado al ADC crudo antes de toda lógica. Verificar
qué filtra hoy `ANALOG_RAW_NOISE_FILTER_GAMING 5` y si conviene sustituirlo o
componerlo. La telemetría de F0 decide: el filtro correcto es el mínimo que
deja el ruido pico-a-pico bajo la sensibilidad RT elegida.

**Validación.** Sesión larga (2h+) con telemetría: reposo estable ±épsilon,
cero actuaciones en reposo, curva completa tras cambiar un switch de sitio.

**Esfuerzo:** 2–3 días. **Riesgo:** bajo por diseño (todo fuera del hot path,
degradación siempre hacia comportamiento seguro).

---

## Fase 3 — Sincronización scan↔SOF USB

**Qué es.** La única ventaja genuina de Wooting. Su "True 8K" no es el número
del descriptor USB: es que el firmware garantiza un reporte **recién calculado**
en cada ventana de poll de 0.125 ms, alineando el final del ciclo de escaneo
con el Start-of-Frame del bus.

**El problema que resuelve.** Si el scan corre libre (asíncrono al USB), un
evento puede quedar listo justo *después* de que el host hizo poll → espera
hasta 0.125 ms extra. A 8 kHz eso es jitter del mismo orden que el periodo:
en el peor caso duplica la latencia efectiva del polling.

**Diseño.**
1. Medir primero (no optimizar a ciegas): con el analizador lógico o con
   timestamps internos, medir la fase entre fin-de-scan y SOF. Si el scan
   completo (~13.3 µs de ADC + lógica) cabe holgado en 125 µs — que cabe —
   la pregunta es solo dónde empieza.
2. ChibiOS expone el callback de SOF (`SOF_CB` en el driver USB de STM32).
   Disparar el inicio del ciclo de escaneo desde ahí, con offset tal que el
   reporte quede listo ~10–20 µs antes del siguiente SOF.
3. Fallback: si engancharse al SOF compite con otras ISR, la alternativa es
   correr el scan a frecuencia ligeramente superior al poll (oversampling
   temporal), que reduce el jitter esperado a la mitad sin sincronía.

**Validación.** Medición externa antes/después (el VXE R1 Pro sirve de
generador de eventos si se le suelda un GPIO, o photodiode + osciloscopio
sobre una tecla). Sin medición no se declara victoria — este es exactamente
el terreno donde los vendors inflan números.

**Esfuerzo:** 1 día de medición + 1–2 de implementación. **Riesgo:** medio
(toca timing de ISRs); beneficio acotado (≤0.125 ms) pero es el último jitter
que queda en la cadena del teclado.

---

## Fase 4 — SOCD por profundidad (Null Bind DISTANCE)

**Qué es.** La evolución del SOCD clásico que ya quedó desbloqueado. En vez de
"la última tecla pulsada gana" (Razer Snap Tap), el comparador usa la señal
analógica: **gana la tecla más hundida**, re-evaluado en cada scan, con la
última pulsada como desempate. Es el "Rappy Snappy" de Wooting / modo
`DISTANCE` del Null Bind de libhmk.

**Por qué es mejor para strafes A↔D en 1.8.9.** El last-input-wins responde a
un evento discreto (cruce de umbral); el modo profundidad responde a la
intención física continua — al transferir peso del dedo de A a D, la dirección
cambia exactamente cuando D supera a A en profundidad, sin esperar cruce de
actuación. Transición más temprana y más suave.

**Diseño.**
- Par configurable (A/D por defecto). Estado mínimo: 2 keycodes + 2 flags.
- Comparador con histéresis de ~0.1 mm: la dirección solo conmuta si la
  diferencia supera la histéresis (sin esto, con ambas teclas casi iguales el
  output oscila a frecuencia de scan — chatter que un anticheat sí podría ver
  como inhumano).
- Detalle fino de libhmk que vale la pena: `bottom_out_point` opcional — si
  ambas teclas superan ese punto, se registran ambas (equivale a "quiero las
  dos, en serio").
- Los 4 modos restantes (last, primary, secondary, neutral) salen casi gratis
  de la misma estructura; implementarlos todos y elegir por config.

**Reglas.** No prohibido explícitamente en Minemen/Hypixel (verificado 12-jul;
solo cláusula genérica de "unfair advantage"); baneado en CS2 y LAN ESL.
Protocolo: probar primero en el lab Ornithe con el registro de TEST-RESULTS,
medir si de verdad mejora los strafes (en 1.8.9 no hay counter-strafe, el
beneficio es menor que en CS), y decidir con datos si se usa en servers.

**Esfuerzo:** medio día sobre la FSM de F1. **Riesgo:** técnico bajo;
el riesgo es de política de servers, no de código.

---

## Fase 5 — Mod-tap por profundidad (modo productividad)

**Qué es.** Feature original: no existe en ningún firmware comercial ni
open-source (verificado — Wooting usa tiempo puro para su Mod-Tap, libhmk y
macrolev también). Todas las piezas están en libhmk para componerla.

**La idea.** El tap-hold clásico de QMK decide tap vs hold con un timer
(`TAPPING_TERM 175` actual): compromiso inevitable — corto genera holds
accidentales, largo hace lento el hold. Con sensor analógico la intención se
lee por **profundidad**: pulsación ligera = tap (letra), pulsación a fondo =
hold (modificador), sin esperar a ningún timer.

**Máquina de estados.**
1. Cruce del punto de actuación → estado TENTATIVE, no se emite nada aún.
2. Si cruza el segundo umbral (bottom-out, ~3.4 mm) antes del tapping_term →
   **HOLD inmediato** (registra modificador). Ni siquiera espera al timer.
3. Si sube y sale de la zona antes del timer → **TAP** (letra, con la cola de
   acciones diferidas de libhmk: press en este scan, release en el siguiente).
4. Timer vencido sin bottom-out → hold clásico (fallback temporal, conserva
   `PERMISSIVE_HOLD` actual).

**Casos de uso en el modo escritura.**
- Caps Lock: ligero = Esc, a fondo = Ctrl (o Caps real).
- Quizá Enter ligero = Enter, a fondo = Ctrl+Enter (enviar en apps de chat).
- Empezar con UNA tecla (Caps) y vivir con ella una semana antes de expandir.

**Restricción dura:** deshabilitado de raíz en gaming mode (mismo patrón del
lockdown), tanto por latencia como porque una pulsación sintetizada con retardo
variable es exactamente lo que no debe existir en PvP.

**Esfuerzo:** 1–2 días. **Riesgo:** bajo (solo capa productividad).

---

## Descartado, con razón documentada

| Feature | Vendor | Por qué no |
|---|---|---|
| Protection Mode | SteelSeries | Filtro anti-fat-finger que sube la actuación de teclas adyacentes; en PvP perjudica W↔S rápidos. Solo se reconsideraría con whitelist WASD+espacio, y la solución simple (actuación profunda en perfil escritura) ya cubre el problema. |
| Dual actuation (W medio=W, fondo=sprint) | SteelSeries | Marginal con toggle-sprint; interfiere con w-tapping. |
| Gamepad analógico / XInput | Wooting/Keychron | MC 1.8.9 no consume ejes; ya se eliminó el perfil 2. |
| Presets por juego (GG Quickset) | SteelSeries | Puro software de escritorio. |
| Auto-switch de perfil por app | Wooting | Es host-side (daemon + HID), no firmware. Referencia futura: ShayBox/Wooting-Profile-Switcher. |
| RT a 0.01 mm | MonsGeek | Resolución de marketing; el ruido físico útil ronda 0.05–0.1 mm en Hall. TMR (su hardware) sí es mejor, pero no aplica al K2. |
| Tachyon "completo" | Wooting | Su contenido real (scan 8k, RGB off) ya está implementado en el fork. Lo único pendiente es la F3. |

## Checklist negativo — errores de vendors que este plan evita por diseño

- **Chattering de RT cerca del reposo** (DrunkDeer 2023 "automated dispatch",
  NuPhy subiendo deadzone máx a 1.0 mm): cubierto por F1 (histéresis
  asimétrica de zona) + F2 (dead zone derivada del reposo calibrado).
- **Corrupción de config persistente** (NuPhy <v110): F2c/F2e — escritura solo
  en housekeeping, CRC/clamp al cargar, defaults sanos.
- **Calibración que bloquea o dispara teclas** (Akko V107/V111): F2d — arranque
  seguro, tecla no actúa sin calibración coherente.
- **La UI miente sobre el estado real** (NuPhy "RT no se aplicaba"): F0 — la
  telemetría reporta lo que ejecuta la ISR, no la copia de la EEPROM.
- **Deriva térmica ignorada** (Akko la documenta, casi nadie la maneja): F0
  la mide, F2b la compensa solo si los datos lo justifican.
- **8K inflado** (DrunkDeer: scan 8k, report 1k): ya resuelto con
  `KEYCHRON_FIXED_REPORT_RATE`; F3 lo lleva al límite físico.

## Resumen de esfuerzo

| Fase | Qué | Esfuerzo | Riesgo |
|---|---|---|---|
| 0 | Telemetría Raw HID + cliente Python | 1 tarde | mínimo |
| 1 | FSM RT press/release separados | 1–2 días | medio |
| 2 | Calibración robusta + dead zones | 2–3 días | bajo |
| 3 | Sync scan↔SOF | 2–3 días | medio |
| 4 | SOCD por profundidad | ½ día | bajo (técnico) |
| 5 | Mod-tap por profundidad | 1–2 días | bajo |

Referencias de código: github.com/peppapighs/libhmk (matrix.c, advanced_keys.c),
github.com/heiso/macrolev, github.com/minipadKB/minipad-firmware. Copias locales
de los tres en el scratchpad de la sesión del 12-jul.
