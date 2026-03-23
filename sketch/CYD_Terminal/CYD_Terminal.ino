/*
This project was modified from the original at https://github.com/CheshirCa/CYD_terminal
It was changed as needed to use the ILI9341 display instead of ST7789 and to
allow different SPI pins for the display driver and the touch interface controller
The original project assumed both were using the same SPI bus

Tested in Arduino IDE 2.3.4 with 
  Espressif ESP32 board file v3.2.1
  TFT_eSPI library v2.5.43


 * CYD Terminal - UART Terminal with ESC sequences support
 * Board: ESP32 CYD (Cheap Yellow Display) 
 * Display: ILI9341
 * 
 * Features:
 * - UART Terminal with basic VT100/ANSI escape sequences
 * - Touch-based baud rate selection
 * - On-screen keyboard (BOOT button to toggle)
 * - USB or External UART selection

 */

#include "config.h"
#include "display.h"
#include "terminal.h"
#include "keyboard.h"
#include "utf8.h"
#include "sdcard.h"
#include <Preferences.h>

Preferences preferences;

// Current mode
bool keyboardVisible = false;
bool inSetupMode = true;
int selectedBaudRate = 4;            // Default 115200
int uartMode = 0;                    // 0 = USB, 1 = External
bool sdAutoRecord = SD_AUTO_RECORD;  // Auto-start recording

// Button debounce
unsigned long lastBootPress = 0;
const unsigned long debounceDelay = 300;

// Touch state for scrolling
int lastTouchY = -1;
bool isDragging = false;

// RX/TX activity tracking
unsigned long lastRxTime = 0;
unsigned long lastTxTime = 0;
const unsigned long activityBlinkDuration = 100;  // ms

// Battery monitoring
float batteryVoltage = 0.0;
int batteryPercent = 0;
unsigned long lastBatteryUpdate = 0;
const unsigned long batteryUpdateInterval = 5000;  // Update every 5 seconds

void setup() {
  // CRITICAL: Initialize Serial FIRST for debugging
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== CYD Terminal Starting ===");

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An error has occurred while mounting SPIFFS");
    return;
  }

  // Initialize the touchscreen
  touchscreen.begin();

  // Check for existing calibration data
  if (!touchscreen.loadCalibration()) {
    Serial.println("Failed to load calibration data from SPIFFS.");
  }

  // calibrate touch screen.  comment out if not wanted on every boot
  /*
  touchscreen.calibrate();
  touchscreen.saveCalibration();
  */

  // Initialize preferences
  Serial.println("Loading preferences...");
  preferences.begin("cyd-terminal", false);

  // Load saved settings
  selectedBaudRate = preferences.getInt("baudrate", 4);
  uartMode = preferences.getInt("uartmode", 0);
  Serial.printf("Loaded: BaudRate=%d, Mode=%d\n", selectedBaudRate, uartMode);

  // Setup boot button
  pinMode(KEY_PIN, INPUT_PULLUP);
  Serial.println("Button configured");

  // Setup battery ADC
  pinMode(BAT_ADC_PIN, INPUT);
  updateBattery();  // Initial read
  // Serial.printf("Battery: %.2fV (%d%%)\n", batteryVoltage, batteryPercent);

  // Setup RGB LED (for status indication)
  Serial.println("Setting up RGB LED...");
  setupRGBLED();
  setLEDColor(0, 0, 255);  // Blue - starting
  Serial.println("LED: BLUE (starting)");

  // Initialize display
  Serial.println("Initializing display...");
  displayInit();
  Serial.println("Display initialized");

  // Initialize SD card
  Serial.println("Initializing SD card...");
  if (sdInit()) {
    Serial.println("SD card initialized successfully");
  } else {
    Serial.println("SD card not present or error");
  }

  // Load SD auto-record setting
  sdAutoRecord = preferences.getBool("sd_autorec", SD_AUTO_RECORD);
  Serial.printf("SD Auto-record: %s\n", sdAutoRecord ? "ON" : "OFF");

  // Show startup screen
  Serial.println("Showing startup screen...");
  showStartupScreen();
  delay(1500);

  // Show initial setup menu
  Serial.println("Showing menu...");
  showInitialMenu();
  Serial.println("=== Setup complete ===\n");
}

void loop() {
  // Feed watchdog to prevent resets
  yield();

  // debug while-loop to check touch screen
  /*
  while (1) {
    TouchPoint touch = touchscreen.getTouch();

  // Display touches that have a pressure value (Z)
  if (touch.zRaw != 0) {
    Serial.print("Touch at X: ");
    Serial.print(touch.x);
    Serial.print(", Y: ");
    Serial.println(touch.y);
  }
  delay(100);
  }
*/


  // Check boot button
  if (digitalRead(KEY_PIN) == LOW) {
    unsigned long currentTime = millis();
    if (currentTime - lastBootPress > debounceDelay) {
      lastBootPress = currentTime;
      // Serial.println("BOOT button pressed");

      if (inSetupMode) {
        // In setup mode - BOOT starts terminal
        startTerminal();
      } else {
        // In terminal mode - BOOT toggles keyboard
        toggleKeyboard();
      }
    }
    // Wait for button release
    while (digitalRead(KEY_PIN) == LOW) {
      delay(10);
    }
    delay(100);  // Debounce after release
  }

  // Handle touch in setup mode (separate from BOOT button)
  if (inSetupMode) {
    static unsigned long lastTouchTime = 0;
    if (millis() - lastTouchTime > 200) {  // Debounce touch
      handleSetupTouch();
      lastTouchTime = millis();
    }
  }

  // Terminal operation mode
  if (!inSetupMode) {
    // Update battery status periodically
    if (millis() - lastBatteryUpdate > batteryUpdateInterval) {
      updateBattery();
      drawStatusBar();  // Refresh status bar with new battery level and RX/TX
      lastBatteryUpdate = millis();
    }

    // Refresh RX/TX indicators more frequently (without redrawing entire status bar)
    static unsigned long lastRxTxUpdate = 0;
    if (millis() - lastRxTxUpdate > 100) {  // Update 10 times per second
      drawRxTxIndicators(80, 6);
      lastRxTxUpdate = millis();
    }

    // Handle incoming UART data
    terminalUpdate();

    // Flush SD card buffer periodically
    sdFlush();

    // Handle touch input
    // First check for keyboard icon tap (works regardless of keyboard state)
    handleKeyboardIconTouch();

    // Check for REC icon tap (works regardless of keyboard state)
    handleRecIconTouch();

    // Then handle keyboard or terminal touches
    if (keyboardVisible) {
      handleKeyboardTouch();
    } else {
      // When keyboard is hidden, handle terminal area touch for scrolling
      handleTerminalScrollTouch();
    }
  }

  delay(10);
}



void showStartupScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(60, 80);
  tft.println("CYD Terminal");
  tft.setTextSize(1);
  tft.setCursor(80, 120);
  tft.println("ESP32 UART Terminal");
  tft.setCursor(70, 140);
  tft.println("with ESC sequences");
}

void showInitialMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Initial Setup");

  // Draw mode selection
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.println("Select UART Mode:");

  drawModeButton(0);
  drawModeButton(1);

  // Draw baud rate selection with hint on same line
  tft.setCursor(10, 105);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print("Select Baud Rate:");
  tft.setCursor(155, 105);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print("Touch buttons to configure");

  for (int i = 0; i < 6; i++) {
    drawBaudButton(i);
  }

  // Start button
  drawStartButton();
}

void drawModeButton(int mode) {
  int x = 10 + mode * 155;
  int y = 55;  // Ниже чем было
  int w = 145;
  int h = 30;  // Уменьшено с 40 до 30

  uint16_t color = (mode == uartMode) ? TFT_GREEN : TFT_DARKGREY;

  tft.fillRoundRect(x, y, w, h, 5, color);
  tft.drawRoundRect(x, y, w, h, 5, TFT_WHITE);

  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, color);
  int textX = x + (mode == 0 ? 25 : 10);
  tft.setCursor(textX, y + 10);  // Центрируем по высоте
  tft.println(mode == 0 ? "USB UART" : "External UART");
}

void drawBaudButton(int index) {
  const int baudRates[] = { 9600, 19200, 38400, 57600, 115200, 230400 };
  int x = 10 + (index % 3) * 102;
  int y = 120 + (index / 3) * 38;  // Подняли выше (было 145)
  int w = 97;
  int h = 33;

  uint16_t color = (index == selectedBaudRate) ? TFT_GREEN : TFT_DARKGREY;

  tft.fillRoundRect(x, y, w, h, 5, color);
  tft.drawRoundRect(x, y, w, h, 5, TFT_WHITE);

  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK, color);

  // Center text based on length
  String baudStr = String(baudRates[index]);
  int textX = x + (w - baudStr.length() * 6) / 2;
  tft.setCursor(textX, y + 12);
  tft.println(baudRates[index]);
}

void drawStartButton() {
  int x = 100;
  int y = 200;  // Опустили ниже (было 205, но теперь baud кнопки выше)
  int w = 120;
  int h = 30;

  tft.fillRoundRect(x, y, w, h, 5, TFT_BLUE);
  tft.drawRoundRect(x, y, w, h, 5, TFT_WHITE);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setCursor(x + 27, y + 8);
  tft.println("START");
}

void handleSetupTouch() {
  static unsigned long lastMenuTouch = 0;

  // Debounce
  if (millis() - lastMenuTouch < 300) {
    return;
  }


  uint16_t touchX, touchY;
  if (getTouch(&touchX, &touchY)) {
    lastMenuTouch = millis();

    // Debug: print touch coordinates
    //Serial.print("Touch: X=");
    //Serial.print(touchX);
    //Serial.print(" Y=");
    //Serial.println(touchY);

    // Check mode buttons (y=55, h=30)
    for (int i = 0; i < 2; i++) {
      int x = 10 + i * 155;
      int y = 55;
      int w = 145;
      int h = 30;

      if (touchX >= x && touchX <= x + w && touchY >= y && touchY <= y + h) {
        //Serial.printf("Mode button %d pressed\n", i);
        uartMode = i;
        preferences.putInt("uartmode", uartMode);
        showInitialMenu();
        delay(300);
        return;
      }
    }

    // Check baud rate buttons (y=120, spacing=38)
    for (int i = 0; i < 6; i++) {
      int x = 10 + (i % 3) * 102;
      int y = 120 + (i / 3) * 38;
      int w = 97;
      int h = 33;

      if (touchX >= x && touchX <= x + w && touchY >= y && touchY <= y + h) {
        //Serial.printf("Baud button %d pressed\n", i);
        selectedBaudRate = i;
        preferences.putInt("baudrate", selectedBaudRate);
        showInitialMenu();
        delay(300);
        return;
      }
    }

    // Check start button (y=200)
    if (touchX >= 100 && touchX <= 220 && touchY >= 200 && touchY <= 230) {
      //Serial.println("START button pressed");
      startTerminal();
      delay(300);
    }
  }
}

void startTerminal() {
  inSetupMode = false;

  // Initialize terminal
  terminalInit(selectedBaudRate, uartMode);

  setLEDColor(0, 255, 0);  // Green - running

  // Clear screen and show terminal
  tft.fillScreen(TFT_BLACK);

  // Show status bar
  drawStatusBar();

  // Auto-start SD recording if enabled
  if (sdAutoRecord && sdGetStatus() == SD_READY) {
    sdStartRecording();
    drawStatusBar();  // Update to show red REC icon
  }
}

void drawStatusBar() {
  const int baudRates[] = { 9600, 19200, 38400, 57600, 115200, 230400 };

  tft.fillRect(0, 0, 320, 20, TFT_NAVY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_NAVY);
  tft.setCursor(5, 6);
  tft.print(uartMode == 0 ? "USB" : "EXT");
  tft.print(" ");
  tft.print(baudRates[selectedBaudRate]);

  // RX/TX indicators (after baud rate)
  drawRxTxIndicators(80, 6);

  // REC icon (between RX/TX and keyboard)
  drawRecIcon(155, 4);

  // Keyboard icon (right side)
  drawKeyboardIcon(230, 4);

  // Battery indicator (far right)
  drawBatteryIcon(260, 4);

  tft.setCursor(285, 6);
  tft.print(batteryPercent);
  tft.print("%");
}

void drawBatteryIcon(int x, int y) {
  // Battery outline
  tft.drawRect(x, y, 18, 10, TFT_WHITE);
  tft.fillRect(x + 18, y + 3, 2, 4, TFT_WHITE);  // Battery tip

  // Fill level based on percentage
  int fillWidth = (batteryPercent * 16) / 100;
  uint16_t fillColor;

  if (batteryPercent > 60) {
    fillColor = TFT_GREEN;
  } else if (batteryPercent > 20) {
    fillColor = TFT_YELLOW;
  } else {
    fillColor = TFT_RED;
  }

  if (fillWidth > 0) {
    tft.fillRect(x + 1, y + 1, fillWidth, 8, fillColor);
  }
}

void updateBattery() {
  // Read battery voltage from ADC
  // CYD has voltage divider: Vbat -> 2x 100K resistors -> ADC
  // ADC range: 0-4095 for 0-3.3V
  // Actual battery voltage = ADC * (3.3V / 4095) * 2

  int adcValue = analogRead(BAT_ADC_PIN);
  batteryVoltage = (adcValue / 4095.0) * 3.3 * 2.0;

  // Debug output
  //Serial.printf("Battery: ADC=%d, Voltage=%.2fV", adcValue, batteryVoltage);

  // Check if battery is actually connected
  // Without battery, ADC floats around 3.5-4.0V (artifact)
  // Real LiPo range: 3.0V (empty) to 4.2V (full), USB charging: >4.5V
  if (batteryVoltage > 4.5) {
    // USB connected with battery charging
    batteryPercent = 100;
    //Serial.println(" (Charging)");
  } else if (batteryVoltage < 2.5 || (batteryVoltage > 3.5 && batteryVoltage < 4.3 && adcValue > 2300 && adcValue < 2400)) {
    // No battery connected (floating ADC in 3.7-3.9V range with specific ADC values)
    // OR voltage too low (disconnected)
    batteryPercent = 0;
    //Serial.println(" (No Battery)");
  } else {
    // Battery connected, calculate percentage
    if (batteryVoltage >= 4.2) {
      batteryPercent = 100;
    } else if (batteryVoltage >= 3.9) {
      batteryPercent = 70 + (int)((batteryVoltage - 3.9) * 100);
    } else if (batteryVoltage >= 3.6) {
      batteryPercent = 30 + (int)((batteryVoltage - 3.6) * 133);
    } else if (batteryVoltage >= 3.3) {
      batteryPercent = 10 + (int)((batteryVoltage - 3.3) * 67);
    } else if (batteryVoltage >= 3.0) {
      batteryPercent = (int)((batteryVoltage - 3.0) * 33);
    } else {
      batteryPercent = 0;
    }

    batteryPercent = constrain(batteryPercent, 0, 100);
    //Serial.printf(" -> %d%%\n", batteryPercent);
  }
}

void drawKeyboardIcon(int x, int y) {
  // Small keyboard icon
  uint16_t color = keyboardVisible ? TFT_GREEN : TFT_LIGHTGREY;

  // Keyboard outline
  tft.drawRect(x, y, 22, 12, color);

  // Keys (3 rows of dots)
  for (int row = 0; row < 3; row++) {
    for (int col = 0; col < 5; col++) {
      tft.fillRect(x + 2 + col * 4, y + 2 + row * 3, 2, 2, color);
    }
  }
}

void drawRxTxIndicators(int x, int y) {
  // RX indicator (green when active)
  unsigned long now = millis();
  bool rxActive = (now - lastRxTime) < activityBlinkDuration;
  bool txActive = (now - lastTxTime) < activityBlinkDuration;

  // RX label
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(x, y);
  tft.print("R");

  // RX LED
  uint16_t rxColor = rxActive ? TFT_GREEN : TFT_DARKGREY;
  tft.fillCircle(x + 8, y + 4, 3, rxColor);

  // TX label
  tft.setCursor(x + 14, y);
  tft.print("T");

  // TX LED
  uint16_t txColor = txActive ? TFT_RED : TFT_DARKGREY;
  tft.fillCircle(x + 22, y + 4, 3, txColor);
}

void drawRecIcon(int x, int y) {
  // REC icon - only show if SD card present
  SDStatus sdStatus = sdGetStatus();

  if (sdStatus == SD_NOT_PRESENT) {
    // Don't show icon if no SD card
    return;
  }

  // Determine color based on status
  uint16_t color;
  if (sdStatus == SD_RECORDING) {
    color = TFT_RED;  // Red when recording
  } else if (sdStatus == SD_READY) {
    color = TFT_LIGHTGREY;  // Grey when ready
  } else {
    color = TFT_DARKGREY;  // Dark grey on error
  }

  // Draw filled circle (REC button)
  tft.fillCircle(x + 6, y + 6, 5, color);

  // Draw "REC" text
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(x + 14, y + 2);
  tft.print("REC");
}

void handleKeyboardIconTouch() {
  static unsigned long lastKeyboardIconTouch = 0;
  static int iconTouchStartY = -1;
  static int iconTouchStartX = -1;
  static int iconLastTouchY = -1;
  uint16_t touchX, touchY;

  if (getTouch(&touchX, &touchY)) {
    // Record initial touch position if just started
    if (iconLastTouchY == -1) {
      iconTouchStartY = touchY;
      iconTouchStartX = touchX;
      iconLastTouchY = touchY;
    }
  } else {
    // Touch released
    if (iconLastTouchY != -1) {
      // Check if this was a tap on keyboard icon (status bar, no big movement)
      // Icon is at X=230, width=22, so range is 230-252
      if (iconTouchStartY >= 0 && iconTouchStartY <= 20 && iconTouchStartX >= 230 && iconTouchStartX <= 252 && abs(iconLastTouchY - iconTouchStartY) < 5) {
        // Tapped keyboard icon
        if (millis() - lastKeyboardIconTouch > 200) {
          lastKeyboardIconTouch = millis();
          toggleKeyboard();
        }
      }
    }

    // Reset touch state
    iconLastTouchY = -1;
    iconTouchStartY = -1;
    iconTouchStartX = -1;
  }
}

void handleRecIconTouch() {
  static unsigned long lastRecIconTouch = 0;
  static int recTouchStartY = -1;
  static int recTouchStartX = -1;
  static int recLastTouchY = -1;
  uint16_t touchX, touchY;

  // Only handle if SD card is present
  if (sdGetStatus() == SD_NOT_PRESENT) {
    return;
  }

  if (getTouch(&touchX, &touchY)) {
    // Record initial touch position if just started
    if (recLastTouchY == -1) {
      recTouchStartY = touchY;
      recTouchStartX = touchX;
      recLastTouchY = touchY;
    }
  } else {
    // Touch released
    if (recLastTouchY != -1) {
      // Check if this was a tap on REC icon (status bar, no big movement)
      // Icon is at X=155, width=30, so range is 155-185
      if (recTouchStartY >= 0 && recTouchStartY <= 20 && recTouchStartX >= 155 && recTouchStartX <= 185 && abs(recLastTouchY - recTouchStartY) < 5) {
        // Tapped REC icon
        if (millis() - lastRecIconTouch > 200) {
          lastRecIconTouch = millis();

          // Toggle recording
          if (sdIsRecording()) {
            sdStopRecording();
          } else {
            sdStartRecording();
          }

          // Redraw status bar to update icon
          drawStatusBar();
        }
      }
    }

    // Reset touch state
    recLastTouchY = -1;
    recTouchStartY = -1;
    recTouchStartX = -1;
  }
}

void handleTerminalScrollTouch() {
  static int touchStartY = -1;
  static int touchStartX = -1;
  uint16_t touchX, touchY;

  if (getTouch(&touchX, &touchY)) {
    // Record initial touch position if just started
    if (!isDragging && lastTouchY == -1) {
      touchStartY = touchY;
      touchStartX = touchX;
      lastTouchY = touchY;
    }

    // Ignore touches on status bar
    if (touchStartY <= 20) {
      return;
    }

    // Check if touch is in terminal area for scrolling
    if (touchY >= TERMINAL_START_Y && touchY < SCREEN_HEIGHT && touchStartY >= TERMINAL_START_Y) {
      // Check movement
      int delta = lastTouchY - touchY;

      // Check if user moved enough to consider it a drag (not a tap)
      if (abs(touchY - touchStartY) > 3) {  // More sensitive - reduced from 5
        isDragging = true;
      }

      // Perform scroll if dragging
      if (isDragging && abs(delta) >= 3) {  // More sensitive - reduced from 4
        int linesDelta = delta / 3;         // 3 pixels per line for better responsiveness
        if (linesDelta != 0) {
          terminalScroll(linesDelta);
          lastTouchY = touchY;
        }
      } else {
        lastTouchY = touchY;
      }
    }
  } else {
    // Touch released - reset state
    isDragging = false;
    lastTouchY = -1;
    touchStartY = -1;
    touchStartX = -1;
  }
}

void toggleKeyboard() {
  keyboardVisible = !keyboardVisible;

  if (keyboardVisible) {
    terminalScrollForKeyboard(true);  // This scrolls and redraws terminal first
    showKeyboard();                   // Then draw keyboard on top
    setLEDColor(255, 255, 0);         // Yellow - keyboard mode
  } else {
    hideKeyboard();                    // Clear keyboard area first
    terminalScrollForKeyboard(false);  // Then redraw terminal to fill the space
    setLEDColor(0, 255, 0);            // Green - normal mode
  }

  // Refresh status bar to update keyboard icon color
  drawStatusBar();
}

void setupRGBLED() {
  // ESP32 Arduino Core 3.x compatible API
  ledcAttach(RED_PIN, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttach(GREEN_PIN, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttach(BLUE_PIN, LEDC_FREQ, LEDC_RESOLUTION);
}

void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(RED_PIN, r);
  ledcWrite(GREEN_PIN, g);
  ledcWrite(BLUE_PIN, b);
}
