/*
 * config.h - Hardware configuration for CYD
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Boot button
#define KEY_PIN 0

// Battery monitoring
#define BAT_ADC_PIN 34

// RGB LED pins
#define RED_PIN 22
#define GREEN_PIN 16
#define BLUE_PIN 17
#define LEDC_RESOLUTION 8
#define LEDC_FREQ 2000
#define LEDC_RED_CHANNEL 0
#define LEDC_GREEN_CHANNEL 1
#define LEDC_BLUE_CHANNEL 2

// Display resolution
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Touch pins (XPT2046)
#define TOUCH_DOUT 39  // T_DO
#define TOUCH_DIN  32  // T_DIN
#define TOUCH_CS   33  // T_CS
#define TOUCH_CLK  25  // T_CLK


// UART pins
#define UART_RX 3
#define UART_TX 1

// Terminal settings
#define TERMINAL_COLS 53  // Characters per line (6px font)
#define TERMINAL_ROWS 27  // Lines visible on screen (8px font)
#define TERMINAL_BUFFER_ROWS 300  // Scrollback buffer (uint16_t = 31 KB for 300 rows)
#define TERMINAL_START_Y 22  // Start below status bar
#define TERMINAL_BUFFER_SIZE 2048

// Keyboard settings
#define KEYBOARD_Y_POS 80   // Keyboard starts at Y=80
#define KEYBOARD_HEIGHT 160 // Keyboard takes 160px

// SD Card settings
#define SD_AUTO_RECORD false  // Auto-start recording on boot (can be changed in setup)

// SD Logger settings
#define SD_MAX_SESSIONS 50  // Keep last 50 sessions
#define SD_AUTO_RECORD false // Auto-start recording on boot (can be changed in setup)

#endif
