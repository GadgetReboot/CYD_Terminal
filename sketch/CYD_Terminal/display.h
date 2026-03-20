/*
 * display.h - Display and touch screen management
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <TFT_eSPI.h>
#include "config.h"
#include "XPT2046_Bitbang.h"

extern TFT_eSPI tft;
extern XPT2046_Bitbang touchscreen;

// Display functions
void displayInit();

// Touch functions
bool getTouch(uint16_t *x, uint16_t *y);

#endif
