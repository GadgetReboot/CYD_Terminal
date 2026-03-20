/*
 * utf8.h - UTF-8 decoder and Cyrillic font support
 */

#ifndef UTF8_H
#define UTF8_H

#include <Arduino.h>

// UTF-8 decoder state
struct UTF8Decoder {
  uint8_t state;
  uint32_t codepoint;
  uint8_t bytesNeeded;
  uint8_t bytesReceived;
};

// Initialize UTF-8 decoder
void utf8Init(UTF8Decoder* decoder);

// Decode one byte, returns true when complete character is decoded
bool utf8Decode(UTF8Decoder* decoder, uint8_t byte);

// Get decoded codepoint
uint32_t utf8GetCodepoint(UTF8Decoder* decoder);

// Check if character is Cyrillic
bool isCyrillic(uint32_t codepoint);

// Convert Unicode codepoint to internal font index
uint16_t unicodeToFontIndex(uint32_t codepoint);

// Draw Unicode character at position
void drawUnicodeChar(uint32_t codepoint, int x, int y, uint16_t fgColor, uint16_t bgColor, int scale = 2);

#endif
