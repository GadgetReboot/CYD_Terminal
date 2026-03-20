/*
 * keyboard.h - On-screen keyboard
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <Arduino.h>
#include "config.h"

// Keyboard control
void showKeyboard();
void hideKeyboard();
void handleKeyboardTouch();

// Command history
void saveCommandToHistory(const String& command);
String getPreviousCommand();
bool hasPreviousCommand();
void clearCommandHistory();

#endif
