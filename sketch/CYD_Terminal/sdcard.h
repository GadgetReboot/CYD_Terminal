#ifndef SDCARD_H
#define SDCARD_H

#include <Arduino.h>

// SD card status
enum SDStatus {
  SD_NOT_PRESENT,
  SD_ERROR,
  SD_READY,
  SD_RECORDING
};

// Initialize SD card
bool sdInit();

// Check if SD card is present and working
SDStatus sdGetStatus();

// Start recording session
bool sdStartRecording();

// Stop recording session
void sdStopRecording();

// Check if currently recording
bool sdIsRecording();

// Log received data (RX)
void sdLogRX(const char* data, size_t len);

// Log transmitted data (TX)
void sdLogTX(const char* data, size_t len);

// Log a single character (RX)
void sdLogRXChar(char c);

// Log a single character (TX)
void sdLogTXChar(char c);

// Log a codepoint (converts to UTF-8)
void sdLogRXCodepoint(uint32_t codepoint);
void sdLogTXCodepoint(uint32_t codepoint);

// Flush buffer to SD card (call periodically)
void sdFlush();

// Get current session number
int sdGetSessionNumber();

// Clean old sessions (keep last 50)
void sdCleanOldSessions();

#endif
