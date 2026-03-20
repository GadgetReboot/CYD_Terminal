/*
 * terminal.h - UART terminal with ESC sequence support
 */

#ifndef TERMINAL_H
#define TERMINAL_H

#include <Arduino.h>
#include "config.h"

// Terminal initialization
void terminalInit(int baudRateIndex, int mode);

// Terminal update loop
void terminalUpdate();

// Send text to UART
void terminalSendText(const char* text);
void terminalSendChar(char c);

// Send text to UART without local echo (for commands already displayed)
void terminalSendTextNoEcho(const char* text);

// Local echo only (no UART send)
void terminalLocalEcho(char c);
void terminalLocalEchoText(const char* text);

// Terminal control
void terminalClear();
void terminalReset();
void terminalRedraw();

// Scroll control
void terminalScroll(int delta); // delta > 0 = scroll up (back in history), delta < 0 = scroll down
int terminalGetScrollOffset();
int terminalGetMaxScroll();
void terminalScrollToBottom();

// Scroll control for keyboard visibility
void terminalScrollForKeyboard(bool keyboardVisible);

// Get cursor position
int terminalGetCursorY();

#endif
