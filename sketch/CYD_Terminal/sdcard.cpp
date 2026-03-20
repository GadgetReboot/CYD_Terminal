#include "sdcard.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>

// SD card pins (ESP32 CYD)
#define SD_CS   5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18

// Buffer settings
#define BUFFER_SIZE 512
#define FLUSH_INTERVAL 5000  // Flush every 5 seconds
#define LINE_BUFFER_SIZE 256 // Buffer for accumulating line before logging

// State
static SDStatus currentStatus = SD_NOT_PRESENT;
static bool isRecording = false;
static int sessionNumber = 0;
static File sessionFile;
static char writeBuffer[BUFFER_SIZE];
static int bufferPos = 0;
static unsigned long lastFlushTime = 0;

// Line buffers for accumulating text until newline
static char rxLineBuffer[LINE_BUFFER_SIZE];
static int rxLinePos = 0;
static char txLineBuffer[LINE_BUFFER_SIZE];
static int txLinePos = 0;

// Forward declarations
static int findNextSessionNumber();
static void flushBuffer();
static void writeToBuffer(const char* data, size_t len);

bool sdInit() {
  // Initialize SPI for SD card
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  // Try to mount SD card
  if (!SD.begin(SD_CS)) {
    currentStatus = SD_NOT_PRESENT;
    return false;
  }
  
  // Check card type
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    currentStatus = SD_NOT_PRESENT;
    return false;
  }
  
  // Create LOGS directory if it doesn't exist
  if (!SD.exists("/LOGS")) {
    SD.mkdir("/LOGS");
  }
  
  currentStatus = SD_READY;
  return true;
}

SDStatus sdGetStatus() {
  return currentStatus;
}

bool sdStartRecording() {
  if (currentStatus != SD_READY && currentStatus != SD_RECORDING) {
    return false;
  }
  
  if (isRecording) {
    return true;  // Already recording
  }
  
  // Find next session number
  sessionNumber = findNextSessionNumber();
  
  // Create filename
  String filename = "/LOGS/session_";
  if (sessionNumber < 100) filename += "0";
  if (sessionNumber < 10) filename += "0";
  filename += String(sessionNumber);
  filename += ".txt";
  
  // Open file for writing
  sessionFile = SD.open(filename, FILE_WRITE);
  if (!sessionFile) {
    currentStatus = SD_ERROR;
    return false;
  }
  
  // Write session header
  sessionFile.print("=== Session ");
  sessionFile.print(sessionNumber);
  sessionFile.println(" Start ===");
  sessionFile.flush();
  
  isRecording = true;
  currentStatus = SD_RECORDING;
  bufferPos = 0;
  rxLinePos = 0;  // Clear RX line buffer
  txLinePos = 0;  // Clear TX line buffer
  lastFlushTime = millis();
  
  return true;
}

void sdStopRecording() {
  if (!isRecording) {
    return;
  }
  
  // Flush any remaining line data
  if (rxLinePos > 0) {
    writeToBuffer("<< ", 3);
    writeToBuffer(rxLineBuffer, rxLinePos);
    writeToBuffer("\n", 1);
    rxLinePos = 0;
  }
  
  if (txLinePos > 0) {
    writeToBuffer(">> ", 3);
    writeToBuffer(txLineBuffer, txLinePos);
    writeToBuffer("\n", 1);
    txLinePos = 0;
  }
  
  // Flush remaining data
  flushBuffer();
  
  // Write session footer
  if (sessionFile) {
    sessionFile.print("\n=== Session ");
    sessionFile.print(sessionNumber);
    sessionFile.println(" End ===");
    sessionFile.close();
  }
  
  isRecording = false;
  currentStatus = SD_READY;
  
  // Clean old sessions
  sdCleanOldSessions();
}

bool sdIsRecording() {
  return isRecording;
}

void sdLogRX(const char* data, size_t len) {
  if (!isRecording || len == 0) return;
  
  writeToBuffer("<< ", 3);
  writeToBuffer(data, len);
}

void sdLogTX(const char* data, size_t len) {
  if (!isRecording || len == 0) return;
  
  // Process each byte, accumulating until newline
  for (size_t i = 0; i < len; i++) {
    if (data[i] == '\n' || data[i] == '\r') {
      // End of line - write accumulated line with prefix
      if (txLinePos > 0) {
        writeToBuffer(">> ", 3);
        writeToBuffer(txLineBuffer, txLinePos);
        writeToBuffer("\n", 1);
        txLinePos = 0;
      }
    } else {
      // Accumulate character
      if (txLinePos < LINE_BUFFER_SIZE - 1) {
        txLineBuffer[txLinePos++] = data[i];
      }
    }
  }
}

void sdLogRXChar(char c) {
  if (!isRecording) return;
  
  char buf[4];
  buf[0] = '<';
  buf[1] = '<';
  buf[2] = ' ';
  buf[3] = c;
  writeToBuffer(buf, 4);
}

void sdLogTXChar(char c) {
  if (!isRecording) return;
  
  char buf[4];
  buf[0] = '>';
  buf[1] = '>';
  buf[2] = ' ';
  buf[3] = c;
  writeToBuffer(buf, 4);
}

void sdLogRXCodepoint(uint32_t codepoint) {
  if (!isRecording) return;
  
  // Convert codepoint to UTF-8 bytes
  char utf8[5];
  int len = 0;
  
  if (codepoint < 0x80) {
    utf8[len++] = (char)codepoint;
  } else if (codepoint < 0x800) {
    utf8[len++] = (char)(0xC0 | (codepoint >> 6));
    utf8[len++] = (char)(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x10000) {
    utf8[len++] = (char)(0xE0 | (codepoint >> 12));
    utf8[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    utf8[len++] = (char)(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x110000) {
    utf8[len++] = (char)(0xF0 | (codepoint >> 18));
    utf8[len++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    utf8[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    utf8[len++] = (char)(0x80 | (codepoint & 0x3F));
  }
  
  // Add to line buffer
  for (int i = 0; i < len; i++) {
    if (utf8[i] == '\n' || utf8[i] == '\r') {
      // End of line - write accumulated line with prefix
      if (rxLinePos > 0) {
        writeToBuffer("<< ", 3);
        writeToBuffer(rxLineBuffer, rxLinePos);
        writeToBuffer("\n", 1);
        rxLinePos = 0;
      }
    } else {
      // Accumulate character
      if (rxLinePos < LINE_BUFFER_SIZE - 1) {
        rxLineBuffer[rxLinePos++] = utf8[i];
      }
    }
  }
}

void sdLogTXCodepoint(uint32_t codepoint) {
  if (!isRecording) return;
  
  // Convert codepoint to UTF-8 bytes
  char utf8[5];
  int len = 0;
  
  if (codepoint < 0x80) {
    utf8[len++] = (char)codepoint;
  } else if (codepoint < 0x800) {
    utf8[len++] = (char)(0xC0 | (codepoint >> 6));
    utf8[len++] = (char)(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x10000) {
    utf8[len++] = (char)(0xE0 | (codepoint >> 12));
    utf8[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    utf8[len++] = (char)(0x80 | (codepoint & 0x3F));
  } else if (codepoint < 0x110000) {
    utf8[len++] = (char)(0xF0 | (codepoint >> 18));
    utf8[len++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    utf8[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    utf8[len++] = (char)(0x80 | (codepoint & 0x3F));
  }
  
  // Add to line buffer
  for (int i = 0; i < len; i++) {
    if (utf8[i] == '\n' || utf8[i] == '\r') {
      // End of line - write accumulated line with prefix
      if (txLinePos > 0) {
        writeToBuffer(">> ", 3);
        writeToBuffer(txLineBuffer, txLinePos);
        writeToBuffer("\n", 1);
        txLinePos = 0;
      }
    } else {
      // Accumulate character
      if (txLinePos < LINE_BUFFER_SIZE - 1) {
        txLineBuffer[txLinePos++] = utf8[i];
      }
    }
  }
}

void sdFlush() {
  if (!isRecording) return;
  
  unsigned long now = millis();
  if (now - lastFlushTime >= FLUSH_INTERVAL) {
    flushBuffer();
    lastFlushTime = now;
  }
}

int sdGetSessionNumber() {
  return sessionNumber;
}

void sdCleanOldSessions() {
  const int MAX_SESSIONS = 50;
  
  // Count session files
  File root = SD.open("/LOGS");
  if (!root) return;
  
  int fileCount = 0;
  File entry = root.openNextFile();
  while (entry) {
    String name = entry.name();
    if (name.startsWith("session_") && name.endsWith(".txt")) {
      fileCount++;
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  
  // Delete oldest if we exceed limit
  if (fileCount > MAX_SESSIONS) {
    int toDelete = fileCount - MAX_SESSIONS;
    
    // Delete session_001.txt, session_002.txt, etc.
    for (int i = 1; i <= toDelete; i++) {
      String filename = "/LOGS/session_";
      if (i < 100) filename += "0";
      if (i < 10) filename += "0";
      filename += String(i);
      filename += ".txt";
      
      if (SD.exists(filename)) {
        SD.remove(filename);
      }
    }
  }
}

// Private functions

static int findNextSessionNumber() {
  int maxNum = 0;
  
  File root = SD.open("/LOGS");
  if (!root) return 1;
  
  File entry = root.openNextFile();
  while (entry) {
    String name = entry.name();
    if (name.startsWith("session_") && name.endsWith(".txt")) {
      // Extract number from session_NNN.txt
      int start = 8;  // After "session_"
      int end = name.indexOf('.');
      if (end > start) {
        String numStr = name.substring(start, end);
        int num = numStr.toInt();
        if (num > maxNum) {
          maxNum = num;
        }
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  
  return maxNum + 1;
}

static void flushBuffer() {
  if (bufferPos == 0 || !isRecording || !sessionFile) return;
  
  sessionFile.write((uint8_t*)writeBuffer, bufferPos);
  sessionFile.flush();
  bufferPos = 0;
}

static void writeToBuffer(const char* data, size_t len) {
  if (!isRecording) return;
  
  for (size_t i = 0; i < len; i++) {
    writeBuffer[bufferPos++] = data[i];
    
    // Flush if buffer full or newline
    if (bufferPos >= BUFFER_SIZE - 1 || data[i] == '\n') {
      flushBuffer();
    }
  }
}
