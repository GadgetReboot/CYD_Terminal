/*
 * utf8.cpp - UTF-8 decoder and Cyrillic font implementation
 */

#include "utf8.h"
#include "display.h"

// UTF-8 decoder implementation
void utf8Init(UTF8Decoder* decoder) {
  decoder->state = 0;
  decoder->codepoint = 0;
  decoder->bytesNeeded = 0;
  decoder->bytesReceived = 0;
}

bool utf8Decode(UTF8Decoder* decoder, uint8_t byte) {
  if (decoder->bytesNeeded == 0) {
    // Start of new character
    if ((byte & 0x80) == 0) {
      // ASCII (1 byte)
      decoder->codepoint = byte;
      return true;
    } else if ((byte & 0xE0) == 0xC0) {
      // 2-byte sequence
      decoder->codepoint = byte & 0x1F;
      decoder->bytesNeeded = 1;
      decoder->bytesReceived = 0;
    } else if ((byte & 0xF0) == 0xE0) {
      // 3-byte sequence
      decoder->codepoint = byte & 0x0F;
      decoder->bytesNeeded = 2;
      decoder->bytesReceived = 0;
    } else if ((byte & 0xF8) == 0xF0) {
      // 4-byte sequence
      decoder->codepoint = byte & 0x07;
      decoder->bytesNeeded = 3;
      decoder->bytesReceived = 0;
    } else {
      // Invalid UTF-8 start byte
      utf8Init(decoder);
      return false;
    }
  } else {
    // Continuation byte
    if ((byte & 0xC0) == 0x80) {
      decoder->codepoint = (decoder->codepoint << 6) | (byte & 0x3F);
      decoder->bytesReceived++;
      
      if (decoder->bytesReceived >= decoder->bytesNeeded) {
        decoder->bytesNeeded = 0;
        return true;
      }
    } else {
      // Invalid continuation byte
      utf8Init(decoder);
      return false;
    }
  }
  
  return false;
}

uint32_t utf8GetCodepoint(UTF8Decoder* decoder) {
  return decoder->codepoint;
}

bool isCyrillic(uint32_t codepoint) {
  // Cyrillic Unicode ranges:
  // U+0400-U+04FF - Cyrillic
  // U+0500-U+052F - Cyrillic Supplement
  return (codepoint >= 0x0400 && codepoint <= 0x052F);
}

uint16_t unicodeToFontIndex(uint32_t codepoint) {
  // ASCII range (0x00-0x7F)
  if (codepoint < 0x80) {
    return codepoint;
  }
  
  // Cyrillic range (U+0400-U+04FF)
  // Map to positions starting after ASCII (128+)
  if (codepoint >= 0x0400 && codepoint <= 0x04FF) {
    return 128 + (codepoint - 0x0400);
  }
  
  // Unknown character - return '?'
  return '?';
}

// Cyrillic font 6x8 bitmap (basic Russian alphabet)
// А-Я (uppercase U+0410-042F), а-я (lowercase U+0430-044F)
// Ё/ё handled separately
const uint8_t cyrillicFont6x8[][6] PROGMEM = {
  // А (U+0410) index 0
  {0x7C, 0x12, 0x11, 0x12, 0x7C, 0x00},
  // Б (U+0411) index 1 - FIXED
  {0x7F, 0x49, 0x49, 0x49, 0x30, 0x00},
  // В (U+0412) index 2
  {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00},
  // Г (U+0413) index 3
  {0x7F, 0x01, 0x01, 0x01, 0x01, 0x00},
  // Д (U+0414) index 4
  {0xC0, 0x7E, 0x41, 0x7F, 0xC0, 0x00},
  // Е (U+0415) index 5
  {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00},
  // Ж (U+0416) index 6 - FIXED (clearer three verticals)
  {0x63, 0x14, 0x7F, 0x14, 0x63, 0x00},
  // З (U+0417) index 7
  {0x22, 0x41, 0x49, 0x49, 0x36, 0x00},
  // И (U+0418) index 8
  {0x7F, 0x20, 0x10, 0x08, 0x7F, 0x00},
  // Й (U+0419) index 9
  {0x7F, 0x20, 0x13, 0x08, 0x7F, 0x00},
  // К (U+041A) index 10
  {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00},
  // Л (U+041B) index 11 - FIXED (upside-down V)
  {0x78, 0x04, 0x02, 0x01, 0x7F, 0x00},
  // М (U+041C) index 12
  {0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00},
  // Н (U+041D) index 13
  {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00},
  // О (U+041E) index 14
  {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00},
  // П (U+041F) index 15
  {0x7F, 0x01, 0x01, 0x01, 0x7F, 0x00},
  // Р (U+0420) index 16
  {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00},
  // С (U+0421) index 17
  {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00},
  // Т (U+0422) index 18
  {0x01, 0x01, 0x7F, 0x01, 0x01, 0x00},
  // У (U+0423) index 19
  {0x07, 0x48, 0x48, 0x48, 0x3F, 0x00},
  // Ф (U+0424) index 20 - FIXED
  {0x0E, 0x11, 0x7F, 0x11, 0x0E, 0x00},
  // Х (U+0425) index 21
  {0x63, 0x14, 0x08, 0x14, 0x63, 0x00},
  // Ц (U+0426) index 22 - FIXED with better tail
  {0x7F, 0x40, 0x40, 0x7F, 0xC0, 0x00},
  // Ч (U+0427) index 23
  {0x07, 0x08, 0x08, 0x08, 0x7F, 0x00},
  // Ш (U+0428) index 24
  {0x7F, 0x40, 0x7F, 0x40, 0x7F, 0x00},
  // Щ (U+0429) index 25
  {0x7F, 0x40, 0x7F, 0x40, 0xFF, 0x00},
  // Ъ (U+042A) index 26
  {0x01, 0x7F, 0x48, 0x48, 0x30, 0x00},
  // Ы (U+042B) index 27
  {0x7F, 0x48, 0x30, 0x00, 0x7F, 0x00},
  // Ь (U+042C) index 28
  {0x7F, 0x48, 0x48, 0x48, 0x30, 0x00},
  // Э (U+042D) index 29
  {0x22, 0x41, 0x49, 0x49, 0x3E, 0x00},
  // Ю (U+042E) index 30
  {0x7F, 0x08, 0x3E, 0x41, 0x3E, 0x00},
  // Я (U+042F) index 31
  {0x46, 0x29, 0x19, 0x09, 0x7F, 0x00},
  // а (U+0430) index 32 - lowercase starts here
  {0x20, 0x54, 0x54, 0x54, 0x78, 0x00},
  // б (U+0431) index 33 - FIXED (like uppercase Б)
  {0x3C, 0x4A, 0x4A, 0x4A, 0x30, 0x00},
  // в (U+0432) index 34
  {0x7C, 0x54, 0x54, 0x54, 0x28, 0x00},
  // г (U+0433) index 35
  {0x7C, 0x04, 0x04, 0x04, 0x00, 0x00},
  // д (U+0434) index 36 - FIXED (rectangle with two legs)
  {0xC0, 0x78, 0x44, 0x7C, 0xC0, 0x00},
  // е (U+0435) index 37
  {0x38, 0x54, 0x54, 0x54, 0x18, 0x00},
  // ж (U+0436) index 38 - FIXED (three verticals with X, lowercase)
  {0x44, 0x28, 0x7C, 0x28, 0x44, 0x00},
  // з (U+0437) index 39
  {0x28, 0x44, 0x54, 0x54, 0x28, 0x00},
  // и (U+0438) index 40
  {0x7C, 0x20, 0x10, 0x08, 0x7C, 0x00},
  // й (U+0439) index 41
  {0x7C, 0x20, 0x16, 0x08, 0x7C, 0x00},
  // к (U+043A) index 42
  {0x7C, 0x10, 0x28, 0x44, 0x00, 0x00},
  // л (U+043B) index 43 - FIXED (vertically flipped)
  {0x70, 0x08, 0x04, 0x04, 0x7C, 0x00},
  // м (U+043C) index 44
  {0x7C, 0x04, 0x18, 0x04, 0x7C, 0x00},
  // н (U+043D) index 45 - FIXED (crossbar lowered)
  {0x7C, 0x10, 0x10, 0x10, 0x7C, 0x00},
  // о (U+043E) index 46
  {0x38, 0x44, 0x44, 0x44, 0x38, 0x00},
  // п (U+043F) index 47
  {0x7C, 0x04, 0x04, 0x04, 0x7C, 0x00},
  // р (U+0440) index 48
  {0xFC, 0x24, 0x24, 0x24, 0x18, 0x00},
  // с (U+0441) index 49
  {0x38, 0x44, 0x44, 0x44, 0x28, 0x00},
  // т (U+0442) index 50
  {0x04, 0x04, 0x7C, 0x04, 0x04, 0x00},
  // у (U+0443) index 51
  {0x0C, 0x50, 0x50, 0x50, 0x3C, 0x00},
  // ф (U+0444) index 52 - FIXED (circle with vertical line, correct orientation)
  {0x38, 0x44, 0xFE, 0x44, 0x38, 0x00},
  // х (U+0445) index 53
  {0x44, 0x28, 0x10, 0x28, 0x44, 0x00},
  // ц (U+0446) index 54 - FIXED with clearer second vertical
  {0x7C, 0x40, 0x40, 0x7C, 0xC0, 0x00},
  // ч (U+0447) index 55
  {0x0C, 0x10, 0x10, 0x10, 0x7C, 0x00},
  // ш (U+0448) index 56
  {0x7C, 0x40, 0x7C, 0x40, 0x7C, 0x00},
  // щ (U+0449) index 57
  {0x7C, 0x40, 0x7C, 0x40, 0xFC, 0x00},
  // ъ (U+044A) index 58
  {0x04, 0x7C, 0x50, 0x50, 0x20, 0x00},
  // ы (U+044B) index 59
  {0x7C, 0x50, 0x20, 0x00, 0x7C, 0x00},
  // ь (U+044C) index 60
  {0x7C, 0x50, 0x50, 0x50, 0x20, 0x00},
  // э (U+044D) index 61
  {0x28, 0x44, 0x54, 0x54, 0x38, 0x00},
  // ю (U+044E) index 62
  {0x7C, 0x10, 0x38, 0x44, 0x38, 0x00},
  // я (U+044F) index 63 - FIXED (like uppercase Я)
  {0x48, 0x34, 0x14, 0x14, 0x7C, 0x00},
};

void drawUnicodeChar(uint32_t codepoint, int x, int y, uint16_t fgColor, uint16_t bgColor, int scale) {
  const uint8_t* fontData = nullptr;
  
  // ASCII characters - use built-in font
  if (codepoint < 128) {
    tft.setCursor(x, y);
    tft.setTextColor(fgColor, bgColor);
    tft.setTextSize(scale);
    tft.print((char)codepoint);
    tft.setTextSize(1); // Reset
    return;
  }
  
  // Cyrillic characters
  // Array structure:
  // Indices 0-31: А-Я (U+0410-042F) - 32 uppercase letters
  // Indices 32-63: а-я (U+0430-044F) - 32 lowercase letters
  int index = -1;
  
  if (codepoint >= 0x0410 && codepoint <= 0x042F) {
    // Uppercase А-Я
    index = codepoint - 0x0410;
  } else if (codepoint == 0x0401) {
    // Ё (uppercase) -> use Е glyph
    index = 5; // Е at index 5
  } else if (codepoint >= 0x0430 && codepoint <= 0x044F) {
    // Lowercase а-я
    index = 32 + (codepoint - 0x0430);
  } else if (codepoint == 0x0451) {
    // ё (lowercase) -> use е glyph
    index = 32 + 5; // е at index 37
  }
  
  if (index >= 0 && index < (sizeof(cyrillicFont6x8) / 6)) {
    fontData = cyrillicFont6x8[index];
    // Serial.printf("Draw U+%04X -> index %d (scale=%d)\n", codepoint, index, scale);
  } else {
    Serial.printf("Draw U+%04X -> NO FONT (index=%d)\n", codepoint, index);
  }
  
  // Draw character bitmap with scaling
  if (fontData != nullptr) {
    for (int col = 0; col < 6; col++) {
      uint8_t line = pgm_read_byte(&fontData[col]);
      for (int row = 0; row < 8; row++) {
        uint16_t color = (line & (1 << row)) ? fgColor : bgColor;
        // Draw scaled pixel block
        tft.fillRect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  } else {
    // Unknown character - draw '?'
    tft.setCursor(x, y);
    tft.setTextColor(fgColor, bgColor);
    tft.setTextSize(scale);
    tft.print('?');
    tft.setTextSize(1); // Reset
  }
}
