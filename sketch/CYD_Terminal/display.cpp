/*
 * display.cpp - Display and touch screen implementation
 */

#include "display.h"
TFT_eSPI tft = TFT_eSPI();

// use extra touch library to allow custom SPI pins as defined in config.h
XPT2046_Bitbang touchscreen(TOUCH_DIN, TOUCH_DOUT, TOUCH_CLK, TOUCH_CS);

void displayInit() {
  // Initialize backlight FIRST
  /*
  pinMode(TFT_BL, OUTPUT);  // TFT_BL
  digitalWrite(TFT_BL, HIGH);  // Turn on backlight
  delay(100);
  */

  // Initialize TFT
  tft.init();
  tft.setRotation(1);  // Landscape mode
  tft.invertDisplay( true );  // uncomment if display has inverted colors from expected ones

  // Set touch calibration data for FNK0103L_3P2
  // Format: {x_min, x_max, y_min, y_max, rotation}
  // Calibrated for landscape mode (rotation 1)
  //uint16_t calData[5] = { 300, 3600, 400, 3600, 1 };
  //tft.setTouch(calData);

  tft.fillScreen(TFT_BLACK);

  // Set font
  tft.setTextFont(1);  // Default 6x8 font
  tft.setTextSize(1);
}

/*
bool getTouch(uint16_t *x, uint16_t *y) {
  // Use built-in TFT_eSPI touch support with calibration
  bool pressed = tft.getTouch(x, y);
  if (pressed) {
    // Invert X coordinate (screen is mirrored horizontally)
    *x = SCREEN_WIDTH - *x;
  }
  return pressed;
}
*/

bool getTouch(uint16_t *x, uint16_t *y) {

  TouchPoint touch = touchscreen.getTouch();

  bool pressed = false;

  if (touch.zRaw != 0) {
    pressed = true;
    *x = touch.x;
    *y = touch.y;
  }
  return pressed;


}
