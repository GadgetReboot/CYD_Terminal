/*
 * keyboard.cpp - On-screen keyboard implementation
 */

#include "keyboard.h"
#include "display.h"
#include "terminal.h"
#include "utf8.h"

// Keyboard layouts
enum KeyboardLayout {
  LAYOUT_EN,
  LAYOUT_RU,
  LAYOUT_SYM,
  LAYOUT_NAV  // Navigation and editing
};

// English layout
const char* keyboardEN[] = {
  "qwertyuiop",
  "asdfghjkl",
  "zxcvbnm"
};

// Russian layout (UTF-8 will be handled separately)
const char* keyboardRU[] = {
  "йцукенгшщзхъ",  // 12 keys
  "фывапролджэ",   // 11 keys
  "ячсмитьбю"      // 9 keys
};

// Symbols and numbers layout
const char* keyboardSYM[] = {
  "1234567890",
  "!@#$%^&*()",
  "-_=+[]{}\\|",
  ";:'\"<>,./?~`"
};

const int keyWidth = 30;
const int keyHeight = 30;
const int keySpacing = 2;

bool shiftPressed = false;
KeyboardLayout currentLayout = LAYOUT_EN;
static unsigned long lastTouchTime = 0;
const unsigned long touchDebounce = 250;

// Command history (last 10 commands)
#define MAX_HISTORY 10
static String commandHistory[MAX_HISTORY];
static int historyCount = 0;

// Input buffer for accumulating characters before sending
#define INPUT_BUFFER_SIZE 256
static char inputBuffer[INPUT_BUFFER_SIZE];
static int inputBufferPos = 0;
static int currentHistoryIndex = -1;  // Current position in history (-1 = new command, 0 = most recent)
static String savedNewCommand = "";   // Save current unfinished command when browsing history

void saveCommandToHistory(const String& command) {
  if (command.length() == 0) return;
  
  // Shift history down
  for (int i = MAX_HISTORY - 1; i > 0; i--) {
    commandHistory[i] = commandHistory[i-1];
  }
  
  // Add new command at index 0
  commandHistory[0] = command;
  
  if (historyCount < MAX_HISTORY) {
    historyCount++;
  }
}

String getPreviousCommand() {
  if (historyCount > 0) {
    return commandHistory[0];
  }
  return "";
}

bool hasPreviousCommand() {
  return (historyCount > 0);
}

void clearCommandHistory() {
  historyCount = 0;
  currentHistoryIndex = -1;
}

// Clear current input line on screen
void clearInputLine() {
  // Send backspaces to clear current line  
  for (int i = 0; i < inputBufferPos; i++) {
    terminalLocalEcho('\b');
  }
}

// Load command into input buffer and display it
void loadCommandToBuffer(const String& cmd) {
  // Clear current input
  clearInputLine();
  
  // Copy command to buffer
  inputBufferPos = 0;
  for (size_t i = 0; i < cmd.length() && inputBufferPos < INPUT_BUFFER_SIZE - 1; i++) {
    inputBuffer[inputBufferPos++] = cmd[i];
  }
  
  // Display on screen (local echo only, don't send to UART)
  terminalLocalEchoText(cmd.c_str());
}

// Navigate history UP (older commands)
void historyUp() {
  if (historyCount == 0) return;
  
  // First time pressing UP - save current unfinished command
  if (currentHistoryIndex == -1) {
    inputBuffer[inputBufferPos] = '\0';
    savedNewCommand = String(inputBuffer);
    currentHistoryIndex = 0;
  } else if (currentHistoryIndex < historyCount - 1) {
    currentHistoryIndex++;
  } else {
    return; // Already at oldest command
  }
  
  // Load command from history
  loadCommandToBuffer(commandHistory[currentHistoryIndex]);
}

// Navigate history DOWN (newer commands)
void historyDown() {
  if (currentHistoryIndex == -1) return; // Not browsing history
  
  currentHistoryIndex--;
  
  if (currentHistoryIndex == -1) {
    // Back to the command we were typing
    loadCommandToBuffer(savedNewCommand);
    savedNewCommand = "";
  } else {
    // Load command from history
    loadCommandToBuffer(commandHistory[currentHistoryIndex]);
  }
}

void drawKey(int row, int col, const char* key, int keyW = keyWidth) {
  // Adjust key width for Russian layout (smaller to fit 12 keys)
  if (currentLayout == LAYOUT_RU) {
    keyW = 24; // Narrower for Russian (12 keys in first row)
  }
  
  // Calculate offsets based on layout
  int rowOffset;
  if (currentLayout == LAYOUT_SYM) {
    rowOffset = 5; // Symbols centered
  } else if (currentLayout == LAYOUT_RU) {
    // Russian - tighter spacing, different offsets per row
    int rowOffsets[] = {5, 10, 30}; // Row 1: 12 keys, Row 2: 11 keys, Row 3: 9 keys
    rowOffset = rowOffsets[row];
  } else {
    // EN layout - adjusted offsets to fit on screen, shifted more left
    int rowOffsets[] = {2, 10, 25}; // Row 1: 10 keys, Row 2: 9 keys, Row 3: 7 keys
    rowOffset = rowOffsets[row];
  }
  
  int x = rowOffset + col * (keyW + keySpacing);
  int y = KEYBOARD_Y_POS + row * (keyHeight + keySpacing);
  
  // Draw key background
  tft.fillRoundRect(x, y, keyW, keyHeight, 3, TFT_DARKGREY);
  tft.drawRoundRect(x, y, keyW, keyHeight, 3, TFT_WHITE);
  
  // Draw key label
  if (currentLayout == LAYOUT_RU) {
    // Russian - use UTF-8 drawing with 2x scale
    // Decode UTF-8 to get codepoint
    uint32_t codepoint = 0;
    uint8_t b1 = (uint8_t)key[0];
    uint8_t b2 = (uint8_t)key[1];
    
    if ((b1 & 0xE0) == 0xC0 && (b2 & 0xC0) == 0x80) {
      // 2-byte UTF-8
      codepoint = ((b1 & 0x1F) << 6) | (b2 & 0x3F);
    }
    
    // Apply shift for Russian uppercase
    if (shiftPressed && codepoint >= 0x0430 && codepoint <= 0x044F) {
      // Convert Russian lowercase to uppercase
      codepoint -= 0x20; // а-я → А-Я
    }
    
    // Draw using UTF-8 font - centered (2x scale = 12x16)
    // Key is 28px wide, font is 12px wide, so (28-12)/2 = 8px offset
    drawUnicodeChar(codepoint, x + 8, y + 7, TFT_WHITE, TFT_DARKGREY, 2);
  } else {
    // English/Symbols - ASCII
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    
    char displayChar = key[0];
    
    // Apply shift for English lowercase
    if (currentLayout == LAYOUT_EN && shiftPressed && displayChar >= 'a' && displayChar <= 'z') {
      displayChar = displayChar - 32; // Uppercase
    }
    
    int textX = x + 9;
    int textY = y + 8;
    
    tft.setCursor(textX, textY);
    tft.print(displayChar);
  }
}

void drawSpecialKey(const char* label, int x, int y, int w) {
  tft.fillRoundRect(x, y, w, keyHeight, 3, TFT_BLUE);
  tft.drawRoundRect(x, y, w, keyHeight, 3, TFT_WHITE);
  
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  
  int textX = x + (w - strlen(label) * 6) / 2;
  int textY = y + 11;
  tft.setCursor(textX, textY);
  tft.print(label);
}

void showKeyboard() {
  // Clear keyboard area
  tft.fillRect(0, KEYBOARD_Y_POS, SCREEN_WIDTH, KEYBOARD_HEIGHT, TFT_BLACK);
  
  // Draw keys based on current layout
  if (currentLayout == LAYOUT_EN) {
    // English layout - 3 rows
    for (int row = 0; row < 3; row++) {
      int len = strlen(keyboardEN[row]);
      for (int col = 0; col < len; col++) {
        char keyStr[2] = {keyboardEN[row][col], 0};
        drawKey(row, col, keyStr);
      }
    }
  } else if (currentLayout == LAYOUT_RU) {
    // Russian layout - 3 rows
    for (int row = 0; row < 3; row++) {
      const char* rowStr = keyboardRU[row];
      int col = 0;
      for (int i = 0; rowStr[i] != 0; ) {
        // UTF-8: Russian chars are 2 bytes
        char keyStr[3] = {rowStr[i], rowStr[i+1], 0};
        drawKey(row, col, keyStr);
        col++;
        i += 2; // Skip 2 bytes for UTF-8 Cyrillic
      }
    }
  } else if (currentLayout == LAYOUT_SYM) {
    // Symbols layout - 4 rows
    for (int row = 0; row < 4; row++) {
      int len = strlen(keyboardSYM[row]);
      for (int col = 0; col < len; col++) {
        char keyStr[2] = {keyboardSYM[row][col], 0};
        drawKey(row, col, keyStr);
      }
    }
  } else if (currentLayout == LAYOUT_NAV) {
    // Navigation layout - arrows and control keys
    int baseY = KEYBOARD_Y_POS;
    
    // Row 0: UP arrow (centered)
    drawSpecialKey("UP", 135, baseY, 50);
    
    // Row 1: LEFT DOWN RIGHT arrows
    baseY += (keyHeight + keySpacing);
    drawSpecialKey("LEFT", 85, baseY, 50);
    drawSpecialKey("DOWN", 140, baseY, 50);
    drawSpecialKey("RIGHT", 195, baseY, 50);
    
    // Row 2: ESC, TAB, DEL
    baseY += (keyHeight + keySpacing);
    drawSpecialKey("ESC", 5, baseY, 45);
    drawSpecialKey("TAB", 55, baseY, 45);
    drawSpecialKey("DEL", 105, baseY, 45);
    drawSpecialKey("HOME", 155, baseY, 50);
    drawSpecialKey("END", 210, baseY, 50);
    
    // Row 3: F1, F2, F3, F4
    baseY += (keyHeight + keySpacing);
    drawSpecialKey("F1", 5, baseY, 40);
    drawSpecialKey("F2", 50, baseY, 40);
    drawSpecialKey("F3", 95, baseY, 40);
    drawSpecialKey("F4", 140, baseY, 40);
    drawSpecialKey("PgUp", 185, baseY, 50);
    drawSpecialKey("PgDn", 240, baseY, 50);
  }
  
  // Draw special keys row
  int bottomY = KEYBOARD_Y_POS + 4 * (keyHeight + keySpacing);
  
  // SHIFT key
  drawSpecialKey(shiftPressed ? "SHIFT*" : "SHIFT", 5, bottomY, 55);
  
  // LANG key (EN/RU)
  const char* langLabel = (currentLayout == LAYOUT_EN) ? "EN" : 
                          (currentLayout == LAYOUT_RU) ? "RU" : 
                          (currentLayout == LAYOUT_NAV) ? "NAV" : "EN";
  drawSpecialKey(langLabel, 65, bottomY, 40);
  
  // SYM key (switch to symbols)
  drawSpecialKey(currentLayout == LAYOUT_SYM ? "ABC" : "SYM", 110, bottomY, 40);
  
  // SPACE
  drawSpecialKey("SPACE", 155, bottomY, 60);
  
  // BKSP
  drawSpecialKey("BKSP", 220, bottomY, 45);
  
  // ENTER
  drawSpecialKey("ENTER", 270, bottomY, 45);
}

void hideKeyboard() {
  // Clear keyboard area
  tft.fillRect(0, KEYBOARD_Y_POS, SCREEN_WIDTH, KEYBOARD_HEIGHT, TFT_BLACK);
  
  // Redraw terminal content in that area
  // Note: This will be called from toggleKeyboard which then calls terminalScrollForKeyboard
  // Don't call terminalRedraw here to avoid flicker
}

void handleKeyboardTouch() {
  unsigned long currentTime = millis();
  if (currentTime - lastTouchTime < touchDebounce) {
    return;
  }
  
  uint16_t touchX, touchY;
  if (!getTouch(&touchX, &touchY)) {
    return;
  }
  
  lastTouchTime = currentTime;
  
 // Serial.print("Keyboard touch: X=");
 // Serial.print(touchX);
 // Serial.print(" Y=");
 // Serial.println(touchY);
  
  if (touchY < KEYBOARD_Y_POS) {
    return; // Above keyboard
  }
  
  // Check special keys row
  int bottomY = KEYBOARD_Y_POS + 4 * (keyHeight + keySpacing);
  if (touchY >= bottomY && touchY <= bottomY + keyHeight) {
    // SHIFT (5, 60)
    if (touchX >= 5 && touchX <= 60) {
      shiftPressed = !shiftPressed;
      showKeyboard();
      return;
    }
    // LANG (65, 40) - cycle through EN -> RU -> NAV -> EN
    else if (touchX >= 65 && touchX <= 105) {
      if (currentLayout == LAYOUT_EN) {
        currentLayout = LAYOUT_RU;
      } else if (currentLayout == LAYOUT_RU) {
        currentLayout = LAYOUT_NAV;
      } else if (currentLayout == LAYOUT_NAV) {
        currentLayout = LAYOUT_EN;
      } else {
        currentLayout = LAYOUT_EN;
      }
      shiftPressed = false;
      showKeyboard();
      return;
    }
    // SYM (110, 40)
    else if (touchX >= 110 && touchX <= 150) {
      if (currentLayout == LAYOUT_SYM) {
        currentLayout = LAYOUT_EN;
      } else {
        currentLayout = LAYOUT_SYM;
      }
      shiftPressed = false;
      showKeyboard();
      return;
    }
    // SPACE (155, 60)
    else if (touchX >= 155 && touchX <= 215) {
      // Add to input buffer
      if (inputBufferPos < INPUT_BUFFER_SIZE - 1) {
        inputBuffer[inputBufferPos++] = ' ';
      }
      // Local echo only
      terminalLocalEcho(' ');
      return;
    }
    // BKSP (220, 45)
    else if (touchX >= 220 && touchX <= 265) {
      // Remove last byte from input buffer
      if (inputBufferPos > 0) {
        inputBufferPos--;
        // Check if we're in the middle of UTF-8 sequence
        // UTF-8 continuation bytes start with 10xxxxxx (0x80-0xBF)
        while (inputBufferPos > 0 && (inputBuffer[inputBufferPos] & 0xC0) == 0x80) {
          inputBufferPos--;
        }
      }
      // Local echo only
      terminalLocalEcho('\b');
      return;
    }
    // ENTER (270, 45)
    else if (touchX >= 270 && touchX <= 315) {
      // Send accumulated input buffer
      if (inputBufferPos > 0) {
        inputBuffer[inputBufferPos] = '\0'; // Null-terminate
        
        // Save to command history
        saveCommandToHistory(String(inputBuffer));
        
        // Send to UART WITHOUT local echo (text already displayed)
        terminalSendTextNoEcho(inputBuffer);
        
        // Clear buffer and reset history browsing
        inputBufferPos = 0;
        currentHistoryIndex = -1;
        savedNewCommand = "";
      }
      
      // Send newline (with local echo for newline itself)
      terminalSendText("\r\n");
      return;
    }
  }
  
  // Check regular keys
  if (currentLayout == LAYOUT_EN) {
    // English layout - 3 rows
    for (int row = 0; row < 3; row++) {
      int rowOffsets[] = {2, 10, 25}; // Match drawing offsets
      int y = KEYBOARD_Y_POS + row * (keyHeight + keySpacing);
      
      if (touchY >= y && touchY <= y + keyHeight) {
        int len = strlen(keyboardEN[row]);
        for (int col = 0; col < len; col++) {
          int x = rowOffsets[row] + col * (keyWidth + keySpacing);
          
          if (touchX >= x && touchX <= x + keyWidth) {
            char key = keyboardEN[row][col];
            if (shiftPressed) {
              key = key - 32; // Uppercase
              shiftPressed = false;
              showKeyboard();
            }
            
            // Add to input buffer
            if (inputBufferPos < INPUT_BUFFER_SIZE - 1) {
              inputBuffer[inputBufferPos++] = key;
            }
            
            // Local echo only
            terminalLocalEcho(key);
            return;
          }
        }
      }
    }
  } else if (currentLayout == LAYOUT_RU) {
    // Russian layout - narrower keys (24px)
    for (int row = 0; row < 3; row++) {
      int rowOffsets[] = {5, 10, 30};
      int y = KEYBOARD_Y_POS + row * (keyHeight + keySpacing);
      
      if (touchY >= y && touchY <= y + keyHeight) {
        const char* rowStr = keyboardRU[row];
        int col = 0;
        int keyW = 24; // Narrower keys
        
        for (int i = 0; rowStr[i] != 0; i += 2) {
          int x = rowOffsets[row] + col * (keyW + keySpacing);
          
          if (touchX >= x && touchX <= x + keyW) {
            // Decode UTF-8 to get codepoint
            uint8_t b1 = (uint8_t)rowStr[i];
            uint8_t b2 = (uint8_t)rowStr[i+1];
            uint32_t codepoint = 0;
            
            if ((b1 & 0xE0) == 0xC0 && (b2 & 0xC0) == 0x80) {
              codepoint = ((b1 & 0x1F) << 6) | (b2 & 0x3F);
            }
            
            // Serial.printf("RU key: codepoint=U+%04X\n", codepoint);
            
            // Apply shift for Russian
            if (shiftPressed && codepoint >= 0x0430 && codepoint <= 0x044F) {
              // Convert lowercase to uppercase
              codepoint -= 0x20; // а-я → А-Я
              shiftPressed = false;
              showKeyboard();
            }
            
            // Encode back to UTF-8 and send
            char utf8char[4];
            int utf8len = 0;
            if (codepoint < 0x80) {
              utf8char[0] = codepoint;
              utf8char[1] = 0;
              utf8len = 1;
            } else if (codepoint < 0x800) {
              utf8char[0] = 0xC0 | (codepoint >> 6);
              utf8char[1] = 0x80 | (codepoint & 0x3F);
              utf8char[2] = 0;
              utf8len = 2;
            } else {
              utf8char[0] = rowStr[i];
              utf8char[1] = rowStr[i+1];
              utf8char[2] = 0;
              utf8len = 2;
            }
            
            // Add to input buffer
            for (int j = 0; j < utf8len && inputBufferPos < INPUT_BUFFER_SIZE - 1; j++) {
              inputBuffer[inputBufferPos++] = utf8char[j];
            }
            
            // Serial.printf("Sending UTF-8: 0x%02X 0x%02X\n", (uint8_t)utf8char[0], (uint8_t)utf8char[1]);
            // Local echo only
            terminalLocalEchoText(utf8char);
            return;
          }
          col++;
        }
      }
    }
  } else if (currentLayout == LAYOUT_SYM) {
    // Symbols layout - 4 rows
    for (int row = 0; row < 4; row++) {
      int y = KEYBOARD_Y_POS + row * (keyHeight + keySpacing);
      
      if (touchY >= y && touchY <= y + keyHeight) {
        int len = strlen(keyboardSYM[row]);
        for (int col = 0; col < len; col++) {
          int x = 5 + col * (keyWidth + keySpacing);
          
          if (touchX >= x && touchX <= x + keyWidth) {
            char key = keyboardSYM[row][col];
            
            // Add to input buffer
            if (inputBufferPos < INPUT_BUFFER_SIZE - 1) {
              inputBuffer[inputBufferPos++] = key;
            }
            
            // Local echo only
            terminalLocalEcho(key);
            return;
          }
        }
      }
    }
  } else if (currentLayout == LAYOUT_NAV) {
    // Navigation layout
    int baseY = KEYBOARD_Y_POS;
    
    // Row 0: UP arrow
    if (touchY >= baseY && touchY <= baseY + keyHeight) {
      if (touchX >= 135 && touchX <= 185) {
        historyUp();
        return;
      }
    }
    
    // Row 1: LEFT DOWN RIGHT
    baseY += (keyHeight + keySpacing);
    if (touchY >= baseY && touchY <= baseY + keyHeight) {
      if (touchX >= 85 && touchX <= 135) {
        // LEFT - future: move cursor left
        return;
      } else if (touchX >= 140 && touchX <= 190) {
        historyDown();
        return;
      } else if (touchX >= 195 && touchX <= 245) {
        // RIGHT - future: move cursor right
        return;
      }
    }
    
    // Row 2: ESC TAB DEL HOME END
    baseY += (keyHeight + keySpacing);
    if (touchY >= baseY && touchY <= baseY + keyHeight) {
      if (touchX >= 5 && touchX <= 50) {
        // ESC
        terminalSendText("\x1B");
        return;
      } else if (touchX >= 55 && touchX <= 100) {
        // TAB
        if (inputBufferPos < INPUT_BUFFER_SIZE - 1) {
          inputBuffer[inputBufferPos++] = '\t';
        }
        terminalLocalEcho('\t');
        return;
      } else if (touchX >= 105 && touchX <= 150) {
        // DEL - delete character at cursor (same as backspace for now)
        if (inputBufferPos > 0) {
          inputBufferPos--;
          while (inputBufferPos > 0 && (inputBuffer[inputBufferPos] & 0xC0) == 0x80) {
            inputBufferPos--;
          }
        }
        terminalLocalEcho('\b');
        return;
      } else if (touchX >= 155 && touchX <= 205) {
        // HOME - move cursor to start of line (future)
        return;
      } else if (touchX >= 210 && touchX <= 260) {
        // END - move cursor to end of line (future)
        return;
      }
    }
    
    // Row 3: F1 F2 F3 F4 PgUp PgDn
    baseY += (keyHeight + keySpacing);
    if (touchY >= baseY && touchY <= baseY + keyHeight) {
      if (touchX >= 5 && touchX <= 45) {
        // F1
        terminalSendText("\x1BOP");
        return;
      } else if (touchX >= 50 && touchX <= 90) {
        // F2
        terminalSendText("\x1BOQ");
        return;
      } else if (touchX >= 95 && touchX <= 135) {
        // F3
        terminalSendText("\x1BOR");
        return;
      } else if (touchX >= 140 && touchX <= 180) {
        // F4
        terminalSendText("\x1BOS");
        return;
      } else if (touchX >= 185 && touchX <= 235) {
        // PgUp - scroll terminal up (future)
        return;
      } else if (touchX >= 240 && touchX <= 290) {
        // PgDn - scroll terminal down (future)
        return;
      }
    }
  }
}
