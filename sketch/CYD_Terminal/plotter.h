/*
 * plotter.h - Serial data plotter for CYD Terminal
 *
 * Reads numeric values (integer or float) arriving over UART and displays
 * them as a scrolling line graph on the ILI9341 320x240 display.
 *
 * Layout (terminal mode active, status bar at y=0..19):
 *   y=20..39  : plotter control bar (MODE button + scale buttons)
 *   y=40..239 : plot area  (200 px tall, 320 px wide)
 *
 * The plotter shares the same HardwareSerial* that the terminal already
 * opened; it does not reinitialise UART.
 */

#ifndef PLOTTER_H
#define PLOTTER_H

#include <Arduino.h>

// ── Public API ──────────────────────────────────────────────────────────────

// Call once when the user switches into plotter mode.
// Pass the HardwareSerial pointer that terminalInit() already opened.
void plotterInit(HardwareSerial* serial);

// Call every loop() iteration while plotter mode is active.
// Reads available serial bytes, parses numbers, and updates the display.
void plotterUpdate();

// Touch handler – call instead of the terminal touch handlers while in plotter mode.
void plotterHandleTouch();

// Cleanly tear down plotter state (called when switching back to terminal).
void plotterExit();

#endif // PLOTTER_H
