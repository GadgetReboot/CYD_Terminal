#ifndef XPT2046_Bitbang_h
#define XPT2046_Bitbang_h

#include "Arduino.h"
#include <SPIFFS.h>

#define CMD_READ_X   0x91 // Command for XPT2046 to read X position
#define CMD_READ_Y   0xD1 // Command for XPT2046 to read Y position
#define CMD_READ_Z1  0xB1 // Command for XPT2046 to read Z1 position
#define CMD_READ_Z2  0xC1 // Command for XPT2046 to read Z2 position

#define DELAY 5

struct TouchPoint {
    uint16_t x;
    uint16_t y;
    uint16_t xRaw;
    uint16_t yRaw;
    uint16_t zRaw;
};


#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif

#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif


class XPT2046_Bitbang {
public:
    XPT2046_Bitbang(uint8_t mosiPin, uint8_t misoPin, uint8_t clkPin, uint8_t csPin);
    void begin();
    TouchPoint getTouch();
    void calibrate();
    bool loadCalibration();
    void saveCalibration();
    void setCalibration(int xMin, int yMin, int xMax, int yMax);

private:
    uint8_t _mosiPin;
    uint8_t _misoPin;
    uint8_t _clkPin;
    uint8_t _csPin;
    void writeSPI(byte command);
    uint16_t readSPI(byte command);
    struct Calibration {
        int xMin;
        int xMax;
        int yMin;
        int yMax;
    } cal;
};

#endif
