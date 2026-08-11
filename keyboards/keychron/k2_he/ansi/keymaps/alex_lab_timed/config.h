#pragma once

// Binario de MEDICION. Hereda TODO el config experimental de alex_lab (probe de
// timing, telemetria, histograma, F6/F7/F9, predictivo) y anade encima las DOS
// optimizaciones de timing que hoy solo viven en el binario de torneo `alex`:
// SOF-sync y pipeline. Objetivo: poder LEER (via el probe/telemetria) el efecto
// de esas optimizaciones, que en `alex` no se puede porque no compila telemetria.
//
// A/B directo: `alex_lab` (ya flasheado) = misma config SIN estas dos lineas.
// La diferencia de scan dur_us y de la distribucion de fase es su valor medido.
#include "../alex_lab/config.h"

// Retrasar el arranque del barrido hasta un offset fijo tras el SOF USB: mata la
// loteria de fase scan<->poll. Requiere el timestamp de SOF, que usb_main.c
// provee cuando esto o USB_SOF_TIMING_PROBE estan activos (ambos lo estan aqui).
#define ANALOG_SCAN_SOF_SYNC 1

// Procesar las muestras de la columna previa durante la ventana de settle de la
// actual, recuperando los ~20 us/columna de CPU muerta. Requiere
// ANALOG_DEBOUNCE_TIME == 1 (lo fija k2_he/config.h).
#define ANALOG_SCAN_PIPELINE 1
