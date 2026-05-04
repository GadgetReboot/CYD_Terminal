/*
 * terminal.cpp - UART terminal implementation with UTF-8 and Cyrillic support
 */

#include "terminal.h"
#include "display.h"
#include "utf8.h"
#include "sdcard.h"

// Forward declarations
void terminalRedraw();
void drawCursor(bool visible);
void scrollUp();
void putChar(uint32_t codepoint);
void processEscSequence();
void drawScrollbar(int maxY);

// Terminal state
static int currentBaudRate = 115200;
static int currentMode = 0; // 0 = USB, 1 = External
static HardwareSerial* terminalSerial = nullptr;

// Screen buffer - stores Unicode codepoints with scrollback.
// uint16_t covers the full Basic Multilingual Plane (U+0000..U+FFFF),
// including ASCII, Latin, and Cyrillic. Half the RAM of uint32_t,
// making a larger scrollback practical at no other cost.
static uint16_t screenBuffer[TERMINAL_BUFFER_ROWS][TERMINAL_COLS];
static int cursorX = 0;
static int cursorY = 0;
static int scrollOffset = 0;  // Current scroll position (0 = bottom)
static int totalLines = 0;    // Total lines written

// Colors
static uint16_t fgColor = TFT_GREEN;
static uint16_t bgColor = TFT_BLACK;

// ESC sequence parser state
static char escBuffer[32];
static int escIndex = 0;
static bool inEscSequence = false;

// UTF-8 decoder
static UTF8Decoder utf8Decoder;

// Baud rates array
const int baudRates[] = {9600, 19200, 38400, 57600, 115200, 230400};

void terminalInit(int baudRateIndex, int mode) {
  currentMode = mode;
  currentBaudRate = baudRates[baudRateIndex];
  
  // Initialize UTF-8 decoder
  utf8Init(&utf8Decoder);
  
  // Clear screen buffer
  for (int y = 0; y < TERMINAL_BUFFER_ROWS; y++) {
    for (int x = 0; x < TERMINAL_COLS; x++) {
      screenBuffer[y][x] = ' ';
    }
  }
  
  cursorX = 0;
  cursorY = 0;
  scrollOffset = 0;
  totalLines = 1;  // Row 0 exists as the initial cursor line
  
  // Initialize UART
  // Set a large software RX ring buffer BEFORE begin() so burst data
  // (e.g. plotter sine-wave at 115200 baud) never overflows.
  // The ESP32 hardware FIFO is only 128 bytes; the default software buffer
  // is 256 bytes – both far too small for back-to-back float strings.
  if (currentMode == 0) {
    // USB UART (Serial)
    Serial.setRxBufferSize(2048);
    Serial.begin(currentBaudRate);
    terminalSerial = &Serial;
  } else {
    // External UART on GPIO3/1
    Serial2.setRxBufferSize(2048);
    Serial2.begin(currentBaudRate, SERIAL_8N1, UART_RX, UART_TX);
    terminalSerial = &Serial2;
  }
  
  // Draw initial terminal screen
  terminalRedraw();
}

void terminalRedraw() {
  // Redraw visible portion of buffer with scroll offset
  tft.setTextColor(fgColor, bgColor);
  tft.setTextFont(1);
  tft.setTextSize(1);
  
  // Check if keyboard is visible (external variable from main)
  extern bool keyboardVisible;
  int maxY = keyboardVisible ? KEYBOARD_Y_POS : (SCREEN_HEIGHT);
  int visibleRows = (maxY - TERMINAL_START_Y) / 8;
  if (visibleRows > TERMINAL_ROWS) visibleRows = TERMINAL_ROWS;
  
  // When keyboard is visible, show only 5 rows to ensure cursor line (6th) is fully visible and higher up
  if (keyboardVisible && visibleRows > 5) {
    visibleRows = 5;
  }
  
  // Calculate which lines to show
  int firstLineToShow = totalLines - visibleRows - scrollOffset;
  if (firstLineToShow < 0) firstLineToShow = 0;
  
  // Draw visible lines
  for (int y = 0; y < visibleRows; y++) {
    int lineNumber = firstLineToShow + y;
    if (lineNumber >= totalLines) break; // Don't show lines that don't exist yet
    
    // Map line number to buffer position (handle circular buffer)
    int bufferLine;
    if (totalLines <= TERMINAL_BUFFER_ROWS) {
      // Buffer not full yet, direct mapping
      bufferLine = lineNumber;
    } else {
      // Buffer is circular
      // Oldest line in buffer is at position: totalLines % TERMINAL_BUFFER_ROWS
      int oldestLineNumber = totalLines - TERMINAL_BUFFER_ROWS;
      if (lineNumber < oldestLineNumber) {
        // Line is too old, was overwritten
        continue;
      }
      bufferLine = lineNumber % TERMINAL_BUFFER_ROWS;
    }
    
    // Draw this line
    for (int x = 0; x < TERMINAL_COLS; x++) {
      drawUnicodeChar(screenBuffer[bufferLine][x], x * 6, TERMINAL_START_Y + y * 8, fgColor, bgColor, 1);
    }
  }
  
  // When keyboard is visible, handle cursor line and clear artifacts
  extern bool keyboardVisible;
  if (keyboardVisible) {
    // Calculate cursor's absolute line number
    int cursorLineNumber;
    if (totalLines <= TERMINAL_BUFFER_ROWS) {
      cursorLineNumber = cursorY;
    } else {
      int newestLinePos = (totalLines - 1) % TERMINAL_BUFFER_ROWS;
      int offset = (newestLinePos - cursorY + TERMINAL_BUFFER_ROWS) % TERMINAL_BUFFER_ROWS;
      cursorLineNumber = totalLines - 1 - offset;
    }
    
    // Check if cursor line is beyond the visible rows (the 6th line when showing 5)
    if (cursorLineNumber == firstLineToShow + visibleRows && cursorLineNumber <= totalLines - 1) {
      int bufferLine;
      if (totalLines <= TERMINAL_BUFFER_ROWS) {
        bufferLine = cursorLineNumber;
      } else {
        bufferLine = cursorLineNumber % TERMINAL_BUFFER_ROWS;
      }
      
      // Draw cursor line at screen position visibleRows (6th line)
      int screenY = TERMINAL_START_Y + visibleRows * 8;
      if (screenY + 8 <= maxY) {
        // First, clear the line area to remove old content
        tft.fillRect(0, screenY, SCREEN_WIDTH - 4, 8, bgColor);
        
        // Then draw the content from buffer
        for (int x = 0; x < TERMINAL_COLS; x++) {
          drawUnicodeChar(screenBuffer[bufferLine][x], x * 6, screenY, fgColor, bgColor, 1);
        }
        
        // Clear area below cursor line up to keyboard (remove artifacts)
        int clearStartY = screenY + 8;  // Start below cursor line
        int clearHeight = maxY - clearStartY;
        if (clearHeight > 0) {
          tft.fillRect(0, clearStartY, SCREEN_WIDTH, clearHeight, bgColor);
        }
      }
    } else {
      // Cursor line is within visibleRows, just clear area below last visible row
      int lastRowY = TERMINAL_START_Y + visibleRows * 8;
      int clearHeight = maxY - lastRowY;
      if (clearHeight > 0) {
        tft.fillRect(0, lastRowY, SCREEN_WIDTH, clearHeight, bgColor);
      }
    }
  }

  
  // Clear scrollbar area first (before deciding whether to draw it)
  const int scrollbarX = SCREEN_WIDTH - 4;
  const int scrollbarWidth = 3;
  tft.fillRect(scrollbarX, TERMINAL_START_Y, scrollbarWidth, maxY - TERMINAL_START_Y, bgColor);
  
  // Draw scrollbar if there's content to scroll (AFTER clearing area)
  if (totalLines > visibleRows) {
    drawScrollbar(maxY);
  }
  
  // Draw cursor if cursor line is visible in current view
  // Calculate cursor's absolute line number
  int cursorLineNumber;
  if (totalLines <= TERMINAL_BUFFER_ROWS) {
    cursorLineNumber = cursorY;
  } else {
    // In circular buffer, find absolute line number of cursor
    int newestLinePos = (totalLines - 1) % TERMINAL_BUFFER_ROWS;
    int offset = (newestLinePos - cursorY + TERMINAL_BUFFER_ROWS) % TERMINAL_BUFFER_ROWS;
    cursorLineNumber = totalLines - 1 - offset;
  }
  
  // Check if cursor line is in visible range
  // When keyboard is visible, we show 5 rows but allow cursor on 6th row (line index 5)
  int maxCursorLine = firstLineToShow + visibleRows;  // Allow one extra line for cursor
  if (cursorLineNumber >= firstLineToShow && cursorLineNumber <= maxCursorLine) {
    int screenY = TERMINAL_START_Y + (cursorLineNumber - firstLineToShow) * 8;
    // Allow cursor even if line touches bottom boundary
    if (screenY < maxY) {
      // Draw cursor at calculated position
      int screenX = cursorX * 6;
      tft.fillRect(screenX, screenY + 7, 6, 1, fgColor);
    }
  }
}

void drawCursor(bool visible) {
  int screenX = cursorX * 6;
  int screenY = TERMINAL_START_Y + cursorY * 8;
  
  if (visible) {
    tft.fillRect(screenX, screenY + 7, 6, 1, fgColor);
  } else {
    tft.fillRect(screenX, screenY + 7, 6, 1, bgColor);
  }
}

void ensureCursorVisible() {
  extern bool keyboardVisible;
  if (!keyboardVisible) {
    // Keyboard not visible - reset to bottom
    if (scrollOffset != 0) {
      scrollOffset = 0;
      terminalRedraw();
    }
    return;
  }
  
  // Calculate how many rows are actually visible with keyboard
  int visibleRows = (KEYBOARD_Y_POS - TERMINAL_START_Y) / 8;
  
  // Show only 5 rows when keyboard is visible to ensure cursor line (6th) is fully visible and higher
  if (visibleRows > 5) {
    visibleRows = 5;
  }
  
  // Calculate absolute line number of cursor
  int cursorAbsoluteLine;
  if (totalLines <= TERMINAL_BUFFER_ROWS) {
    cursorAbsoluteLine = cursorY;
  } else {
    int newestLinePos = (totalLines - 1) % TERMINAL_BUFFER_ROWS;
    int offset = (newestLinePos - cursorY + TERMINAL_BUFFER_ROWS) % TERMINAL_BUFFER_ROWS;
    cursorAbsoluteLine = totalLines - 1 - offset;
  }
  
  // We want cursor on the 6th line (index 5 = visibleRows)
  // We show 5 lines (0-4), cursor should be on line 5
  int targetCursorScreenPos = visibleRows;  // Cursor on 6th line (index 5)
  
  // Calculate what firstLineToShow should be to achieve this
  int targetFirstLine = cursorAbsoluteLine - targetCursorScreenPos;
  
  // Don't go negative
  if (targetFirstLine < 0) targetFirstLine = 0;
  
  // Calculate scrollOffset for this firstLineToShow
  // firstLineToShow = totalLines - visibleRows - scrollOffset
  // scrollOffset = totalLines - visibleRows - firstLineToShow
  int newScrollOffset = totalLines - visibleRows - targetFirstLine;
  
  // Constrain
  if (newScrollOffset < 0) newScrollOffset = 0;
  int maxScroll = totalLines - visibleRows;
  if (maxScroll < 0) maxScroll = 0;
  if (newScrollOffset > maxScroll) newScrollOffset = maxScroll;
  
  // Always update
  scrollOffset = newScrollOffset;
  terminalRedraw();
}

void scrollUp() {
  // In circular buffer mode, we don't move lines
  // Just increment totalLines and move cursor to the new line position
  
  // Clear the line we're about to write to (which was the oldest line)
  int nextLine = totalLines % TERMINAL_BUFFER_ROWS;
  for (int x = 0; x < TERMINAL_COLS; x++) {
    screenBuffer[nextLine][x] = ' ';
  }
  
  // Move cursor to the new line position in the circular buffer
  cursorY = nextLine;
  totalLines++;
  
  // Don't redraw here - caller will handle it
}

void putChar(uint32_t codepoint) {
  if (codepoint == '\r') {
    cursorX = 0;
  } else if (codepoint == '\n') {
    cursorX = 0;  // Reset to start of line first

    // Check if we need to scroll (circular buffer wrap)
    if (cursorY >= TERMINAL_BUFFER_ROWS - 1) {
      // Buffer is full - use scrollUp() which advances totalLines and
      // moves cursorY to the next circular slot.
      scrollUp(); // clears next line, sets cursorY, increments totalLines
    } else {
      cursorY++;

      // Clear the new line we just moved to
      for (int x = 0; x < TERMINAL_COLS; x++) {
        screenBuffer[cursorY][x] = ' ';
      }

      // Always keep totalLines up to date so terminalRedraw() shows the
      // correct window.  This must happen in BOTH the pre-full and the
      // post-full (circular) phases.
      totalLines++;
    }

    // Ensure cursor is visible (handles keyboard-visible scroll adjustment).
    // ensureCursorVisible() calls terminalRedraw() only when the keyboard is
    // open and the scroll offset needs changing, so we still need to call
    // terminalRedraw() ourselves for the normal (no-keyboard) case.
    ensureCursorVisible();

    // If no keyboard, redraw to show the new line
    extern bool keyboardVisible;
    if (!keyboardVisible) {
      terminalRedraw();
    }
  } else if (codepoint == '\b') {
    if (cursorX > 0) {
      cursorX--;
      screenBuffer[cursorY][cursorX] = ' ';
      
      // Redraw character if cursor line is visible
      extern bool keyboardVisible;
      int maxY = keyboardVisible ? KEYBOARD_Y_POS : SCREEN_HEIGHT;
      int visibleRows = (maxY - TERMINAL_START_Y) / 8;
      if (visibleRows > TERMINAL_ROWS) visibleRows = TERMINAL_ROWS;
      
      // Show only 5 rows when keyboard is visible
      if (keyboardVisible && visibleRows > 5) {
        visibleRows = 5;
      }
      
      // Calculate cursor's absolute line number
      int cursorLineNumber;
      if (totalLines <= TERMINAL_BUFFER_ROWS) {
        cursorLineNumber = cursorY;
      } else {
        int newestLinePos = (totalLines - 1) % TERMINAL_BUFFER_ROWS;
        int offset = (newestLinePos - cursorY + TERMINAL_BUFFER_ROWS) % TERMINAL_BUFFER_ROWS;
        cursorLineNumber = totalLines - 1 - offset;
      }
      
      int firstLineToShow = totalLines - visibleRows - scrollOffset;
      if (firstLineToShow < 0) firstLineToShow = 0;
      
      // Allow cursor line to be drawn beyond visibleRows
      if (cursorLineNumber >= firstLineToShow && cursorLineNumber <= firstLineToShow + visibleRows) {
        int screenY = TERMINAL_START_Y + (cursorLineNumber - firstLineToShow) * 8;
        if (screenY < maxY) {
          drawUnicodeChar(' ', cursorX * 6, screenY, fgColor, bgColor, 1);
        }
      }
    }
  } else if (codepoint >= 32) {
    // Printable character (ASCII or Unicode)
    screenBuffer[cursorY][cursorX] = (uint16_t)codepoint; // safe: BMP only (U+0000..FFFF)
    
    // Draw character if cursor line is visible
    extern bool keyboardVisible;
    int maxY = keyboardVisible ? KEYBOARD_Y_POS : SCREEN_HEIGHT;
    int visibleRows = (maxY - TERMINAL_START_Y) / 8;
    if (visibleRows > TERMINAL_ROWS) visibleRows = TERMINAL_ROWS;
    
    // Show only 5 rows when keyboard is visible
    if (keyboardVisible && visibleRows > 5) {
      visibleRows = 5;
    }
    
    // Calculate cursor's absolute line number
    int cursorLineNumber;
    if (totalLines <= TERMINAL_BUFFER_ROWS) {
      cursorLineNumber = cursorY;
    } else {
      int newestLinePos = (totalLines - 1) % TERMINAL_BUFFER_ROWS;
      int offset = (newestLinePos - cursorY + TERMINAL_BUFFER_ROWS) % TERMINAL_BUFFER_ROWS;
      cursorLineNumber = totalLines - 1 - offset;
    }
    
    // Calculate first line shown
    int firstLineToShow = totalLines - visibleRows - scrollOffset;
    if (firstLineToShow < 0) firstLineToShow = 0;
    
    // Check if cursor line is visible (allow cursor line to be drawn beyond visibleRows)
    // We show 5 rows, but cursor can be on 6th row (index 5)
    if (cursorLineNumber >= firstLineToShow && cursorLineNumber <= firstLineToShow + visibleRows) {
      int screenY = TERMINAL_START_Y + (cursorLineNumber - firstLineToShow) * 8;
      if (screenY < maxY) {
        drawUnicodeChar(codepoint, cursorX * 6, screenY, fgColor, bgColor, 1);
      }
    }
    
    cursorX++;
    
    // After moving cursor, ensure it's still visible when keyboard is open
    extern bool keyboardVisible;
    if (keyboardVisible) {
      terminalRedraw();  // Redraw to show cursor at new position
    }
    
    if (cursorX >= TERMINAL_COLS) {
      cursorX = 0;

      if (cursorY >= TERMINAL_BUFFER_ROWS - 1) {
        // Circular buffer is full - same path as '\n' overflow
        scrollUp(); // clears next slot, advances cursorY and totalLines
      } else {
        cursorY++;

        // Clear the new line
        for (int x = 0; x < TERMINAL_COLS; x++) {
          screenBuffer[cursorY][x] = ' ';
        }

        totalLines++;
      }

      ensureCursorVisible();

      extern bool keyboardVisible;
      if (!keyboardVisible) {
        terminalRedraw();
      }
    }
  }
}

void processEscSequence() {
  escBuffer[escIndex] = '\0';
  
  // Parse ESC sequence
  if (escIndex >= 2 && escBuffer[0] == '[') {
    char cmd = escBuffer[escIndex - 1];
    
    // Extract parameters
    int params[4] = {0, 0, 0, 0};
    int paramCount = 0;
    int num = 0;
    bool hasNum = false;
    
    for (int i = 1; i < escIndex - 1; i++) {
      if (escBuffer[i] >= '0' && escBuffer[i] <= '9') {
        num = num * 10 + (escBuffer[i] - '0');
        hasNum = true;
      } else if (escBuffer[i] == ';') {
        if (hasNum && paramCount < 4) {
          params[paramCount++] = num;
        }
        num = 0;
        hasNum = false;
      }
    }
    if (hasNum && paramCount < 4) {
      params[paramCount++] = num;
    }
    
    // Process command
    switch (cmd) {
      case 'H': // Cursor position
      case 'f':
        cursorY = (paramCount > 0 && params[0] > 0) ? params[0] - 1 : 0;
        cursorX = (paramCount > 1 && params[1] > 0) ? params[1] - 1 : 0;
        cursorX = constrain(cursorX, 0, TERMINAL_COLS - 1);
        cursorY = constrain(cursorY, 0, TERMINAL_ROWS - 1);
        break;
        
      case 'J': // Clear screen
        if (params[0] == 2) {
          terminalClear();
        }
        break;
        
      case 'K': // Clear line
        for (int x = cursorX; x < TERMINAL_COLS; x++) {
          screenBuffer[cursorY][x] = ' ';
        }
        terminalRedraw();
        break;
        
      case 'm': // Graphics mode (colors)
        if (paramCount == 0 || params[0] == 0) {
          // Reset
          fgColor = TFT_GREEN;
          bgColor = TFT_BLACK;
        } else {
          // Basic color support
          for (int i = 0; i < paramCount; i++) {
            if (params[i] >= 30 && params[i] <= 37) {
              // Foreground color
              switch (params[i]) {
                case 30: fgColor = TFT_BLACK; break;
                case 31: fgColor = TFT_RED; break;
                case 32: fgColor = TFT_GREEN; break;
                case 33: fgColor = TFT_YELLOW; break;
                case 34: fgColor = TFT_BLUE; break;
                case 35: fgColor = TFT_MAGENTA; break;
                case 36: fgColor = TFT_CYAN; break;
                case 37: fgColor = TFT_WHITE; break;
              }
            }
          }
        }
        break;
        
      case 'A': // Cursor up
        if (params[0] == 0) params[0] = 1;
        cursorY -= params[0];
        if (cursorY < 0) cursorY = 0;
        break;
        
      case 'B': // Cursor down
        if (params[0] == 0) params[0] = 1;
        cursorY += params[0];
        if (cursorY >= TERMINAL_ROWS) cursorY = TERMINAL_ROWS - 1;
        break;
        
      case 'C': // Cursor forward
        if (params[0] == 0) params[0] = 1;
        cursorX += params[0];
        if (cursorX >= TERMINAL_COLS) cursorX = TERMINAL_COLS - 1;
        break;
        
      case 'D': // Cursor back
        if (params[0] == 0) params[0] = 1;
        cursorX -= params[0];
        if (cursorX < 0) cursorX = 0;
        break;
    }
  }
  
  // Reset ESC state
  inEscSequence = false;
  escIndex = 0;
}

void terminalUpdate() {
  if (!terminalSerial) return;

  // Process up to MAX_BYTES_PER_UPDATE bytes per call.
  // Draining multiple bytes per loop iteration keeps the display updating
  // smoothly at high baud rates without starving the rest of loop().
  // At 115200 baud bytes arrive at ~11.5/ms; processing 64 per call at a
  // typical loop rate of a few hundred Hz is more than enough to keep up.
  const int MAX_BYTES_PER_UPDATE = 64;
  int processed = 0;

  while (processed < MAX_BYTES_PER_UPDATE && terminalSerial->available()) {
    // Mark RX activity (external variable from main)
    extern unsigned long lastRxTime;
    lastRxTime = millis();

    uint8_t byte = terminalSerial->read();
    processed++;

    if (inEscSequence) {
      // Collecting ESC sequence
      if (escIndex < sizeof(escBuffer) - 1) {
        escBuffer[escIndex++] = byte;

        // Check if sequence is complete
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')) {
          processEscSequence();
        }
      } else {
        // Buffer overflow, reset
        inEscSequence = false;
        escIndex = 0;
      }
    } else if (byte == 0x1B) {
      // ESC character - start sequence
      inEscSequence = true;
      escIndex = 0;
    } else {
      // Normal character - decode UTF-8
      if (utf8Decode(&utf8Decoder, byte)) {
        uint32_t codepoint = utf8GetCodepoint(&utf8Decoder);
        // Serial.printf("Received codepoint: U+%04X\n", codepoint);

        // Log to SD after successful UTF-8 decoding
        sdLogRXCodepoint(codepoint);

        putChar(codepoint);
        utf8Init(&utf8Decoder); // Reset for next character
      }
    }
  }
}

void terminalSendText(const char* text) {
 // Serial.print("SendText: '");
 // Serial.print(text);
 // Serial.println("'");
  
  if (terminalSerial) {
    // Mark TX activity (external variable from main)
    extern unsigned long lastTxTime;
    lastTxTime = millis();
    
    // Log to SD card if recording
    sdLogTX(text, strlen(text));
    
    // Send to UART
    terminalSerial->print(text);
    
    // Local echo - decode UTF-8 properly
    UTF8Decoder localDecoder;
    utf8Init(&localDecoder);
    
    for (int i = 0; text[i] != '\0'; i++) {
      if (utf8Decode(&localDecoder, (uint8_t)text[i])) {
        uint32_t codepoint = utf8GetCodepoint(&localDecoder);
        putChar(codepoint);
        utf8Init(&localDecoder); // Reset for next character
      }
    }
  } else {
    Serial.println("ERROR: terminalSerial is NULL!");
  }
}

// Send text to UART without local echo (for text already displayed on screen)
void terminalSendTextNoEcho(const char* text) {
  if (terminalSerial) {
    // Mark TX activity
    extern unsigned long lastTxTime;
    lastTxTime = millis();
    
    // Log to SD card if recording
    sdLogTX(text, strlen(text));
    
    // Send to UART (no local echo - text already on screen)
    terminalSerial->print(text);
  } else {
    Serial.println("ERROR: terminalSerial is NULL!");
  }
}

void terminalSendChar(char c) {
  //Serial.print("SendChar: '");
  //Serial.print(c);
  //Serial.print("' (0x");
  //Serial.print(c, HEX);
  //Serial.println(")");
  
  if (terminalSerial) {
    // Mark TX activity (external variable from main)
    extern unsigned long lastTxTime;
    lastTxTime = millis();
    
    terminalSerial->write(c);
    
    // Local echo - display on CYD screen
    putChar(c);
  } else {
    Serial.println("ERROR: terminalSerial is NULL!");
  }
}

// Local echo only - display on screen without sending to UART
void terminalLocalEcho(char c) {
  putChar(c);
}

// Local echo text - display on screen without sending to UART
void terminalLocalEchoText(const char* text) {
  UTF8Decoder localDecoder;
  utf8Init(&localDecoder);
  
  for (int i = 0; text[i] != '\0'; i++) {
    if (utf8Decode(&localDecoder, (uint8_t)text[i])) {
      uint32_t codepoint = utf8GetCodepoint(&localDecoder);
      putChar(codepoint);
      utf8Init(&localDecoder); // Reset for next character
    }
  }
}

void terminalClear() {
  // Clear buffer
  for (int y = 0; y < TERMINAL_BUFFER_ROWS; y++) {
    for (int x = 0; x < TERMINAL_COLS; x++) {
      screenBuffer[y][x] = ' ';
    }
  }
  cursorX = 0;
  cursorY = 0;
  scrollOffset = 0;
  totalLines = 1;  // Row 0 exists as the initial cursor line
  
  // Clear screen
  tft.fillRect(0, TERMINAL_START_Y, SCREEN_WIDTH, SCREEN_HEIGHT - TERMINAL_START_Y, bgColor);
  drawCursor(true);
}

void terminalReset() {
  terminalClear();
  fgColor = TFT_GREEN;
  bgColor = TFT_BLACK;
}

// Returns the number of lines actually held in the circular buffer.
// totalLines grows without bound (it is the global write counter), but only
// TERMINAL_BUFFER_ROWS slots are stored.  Every scroll calculation must use
// this capped value so the scroll range and scrollbar thumb match reality.
static inline int bufferedLines() {
  return (totalLines < TERMINAL_BUFFER_ROWS) ? totalLines : TERMINAL_BUFFER_ROWS;
}

void drawScrollbar(int maxY) {
  // Scrollbar on right side of screen
  const int scrollbarX = SCREEN_WIDTH - 4;
  const int scrollbarWidth = 3;
  const int scrollbarHeight = maxY - TERMINAL_START_Y;
  
  // Background track
  tft.fillRect(scrollbarX, TERMINAL_START_Y, scrollbarWidth, scrollbarHeight, TFT_DARKGREY);
  
  // Calculate thumb size and position based on actually-buffered lines only.
  // bufferedLines() is capped at TERMINAL_BUFFER_ROWS so the thumb never
  // implies more history than we can actually scroll back through.
  int bufLines = bufferedLines();
  int visibleRows = scrollbarHeight / 8;       // how many text rows fit on screen
  int totalContentHeight = bufLines * 8;
  int visibleHeight = scrollbarHeight;

  if (totalContentHeight > visibleHeight) {
    // Thumb height proportional to (visible / total) buffered content
    int thumbHeight = (visibleHeight * visibleHeight) / totalContentHeight;
    if (thumbHeight < 10) thumbHeight = 10;

    // maxScroll: how many lines we can scroll back (same cap)
    int maxScroll = bufLines - visibleRows;
    if (maxScroll < 1) maxScroll = 1;

    int thumbRange = visibleHeight - thumbHeight;

    // Invert: scrollOffset=0 → thumb at bottom; scrollOffset=maxScroll → thumb at top
    int thumbY = TERMINAL_START_Y + thumbRange - (thumbRange * scrollOffset) / maxScroll;

    tft.fillRect(scrollbarX, thumbY, scrollbarWidth, thumbHeight, TFT_GREEN);
  }
}

void terminalScroll(int delta) {
  scrollOffset += delta;

  // Cap scroll to the lines actually held in the buffer, not the unbounded
  // totalLines counter.  This prevents swiping past real history.
  int maxScroll = bufferedLines() - TERMINAL_ROWS;
  if (maxScroll < 0) maxScroll = 0;

  if (scrollOffset < 0) scrollOffset = 0;
  if (scrollOffset > maxScroll) scrollOffset = maxScroll;

  terminalRedraw();
}

int terminalGetScrollOffset() {
  return scrollOffset;
}

int terminalGetMaxScroll() {
  int maxScroll = bufferedLines() - TERMINAL_ROWS;
  return maxScroll > 0 ? maxScroll : 0;
}

void terminalScrollToBottom() {
  scrollOffset = 0;
  terminalRedraw();
}

int terminalGetCursorY() {
  return cursorY;
}

HardwareSerial* terminalGetSerial() {
  return terminalSerial;
}

void terminalScrollForKeyboard(bool keyboardVisible) {
  if (keyboardVisible) {
    // Клавиатура открывается
    // Рассчитываем сколько строк видно с клавиатурой
    int visibleRows = (KEYBOARD_Y_POS - TERMINAL_START_Y) / 8;
    
    // Вычисляем абсолютный номер строки курсора в истории
    int cursorAbsoluteLine;
    if (totalLines <= TERMINAL_BUFFER_ROWS) {
      // Буфер еще не заполнен, прямое соответствие
      cursorAbsoluteLine = cursorY;
    } else {
      // Буфер циклический
      // Самая новая строка имеет абсолютный номер totalLines - 1
      // Она находится в позиции (totalLines - 1) % TERMINAL_BUFFER_ROWS
      int newestLinePos = (totalLines - 1) % TERMINAL_BUFFER_ROWS;
      
      // Смещение от самой новой строки до курсора (в кольцевом буфере)
      int offset = (newestLinePos - cursorY + TERMINAL_BUFFER_ROWS) % TERMINAL_BUFFER_ROWS;
      
      // Абсолютная позиция курсора
      cursorAbsoluteLine = totalLines - 1 - offset;
    }
    
    // Хотим показать курсор на 3-й строке сверху (индекс 2)
    int targetFirstLine = cursorAbsoluteLine - 2;
    if (targetFirstLine < 0) targetFirstLine = 0;
    
    // Вычисляем scrollOffset
    // scrollOffset = сколько строк назад от конца мы прокручены
    scrollOffset = totalLines - visibleRows - targetFirstLine;
    
    // Ограничения
    if (scrollOffset < 0) scrollOffset = 0;
    int maxScroll = totalLines - visibleRows;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
  } else {
    // Клавиатура закрывается - вернуться к низу
    scrollOffset = 0;
  }
  
  terminalRedraw();
}
