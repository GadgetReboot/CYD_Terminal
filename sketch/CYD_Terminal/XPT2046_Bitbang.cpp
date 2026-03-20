/*
This XPT2046 touch library was implemented from original source at
https://github.com/ddxfish/XPT2046_Bitbang_Arduino_Library/
*/

#include "XPT2046_Bitbang.h"

// set RERUN_CALIBRATE to true or false as needed to force touch coordinate calibration
// eg.  on first run, set to true and calibrate touch coordinates upon power up
//      by following instructions shown in the serial monitor. 
//      Then set to false and re-upload sketch.  
//      The touch calibration will have been stored in SPIFFS
#define RERUN_CALIBRATE false

//#define _mosiPin 32
//#define _misoPin 39
//#define _clkPin  25
//#define _csPin   33
//#define CMD_READ_Y  0x90 // Command for XPT2046 to read Y position
//#define CMD_READ_X  0xD0 // Command for XPT2046 to read X position

XPT2046_Bitbang::XPT2046_Bitbang(uint8_t mosiPin, uint8_t misoPin, uint8_t clkPin, uint8_t csPin) : 
    _mosiPin(mosiPin), _misoPin(misoPin), _clkPin(clkPin), _csPin(csPin) {
    // other initializations, if required
}

void XPT2046_Bitbang::begin() {
    pinMode(_mosiPin, OUTPUT);
    pinMode(_misoPin, INPUT);
    pinMode(_clkPin, OUTPUT);
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
    digitalWrite(_clkPin, LOW); 

    if((!SPIFFS.begin(true)) || (!loadCalibration() || (RERUN_CALIBRATE))) {
        calibrate();
        saveCalibration();
    }
}

void XPT2046_Bitbang::writeSPI(byte command) {
    for(int i = 7; i >= 0; i--) {
        digitalWrite(_mosiPin, command & (1 << i));
        digitalWrite(_clkPin, LOW);
        delayMicroseconds(DELAY);
        digitalWrite(_clkPin, HIGH);
        delayMicroseconds(DELAY);
    }
    digitalWrite(_mosiPin, LOW);
    digitalWrite(_clkPin, LOW);
}

uint16_t XPT2046_Bitbang::readSPI(byte command) {
    writeSPI(command);

    uint16_t result = 0;

    for(int i = 15; i >= 0; i--) {
        digitalWrite(_clkPin, HIGH);
        delayMicroseconds(DELAY);
        digitalWrite(_clkPin, LOW);
        delayMicroseconds(DELAY);
        result |= (digitalRead(_misoPin) << i);
    }

    return result >> 4;
}

void XPT2046_Bitbang::calibrate() {
    Serial.println("Calibration starting...");
    Serial.println("Touch the top-left corner, hold it down until the next message...");
    delay(3000);
    digitalWrite(_csPin, LOW);
    cal.xMin = readSPI(CMD_READ_X);
    cal.yMin = readSPI(CMD_READ_Y);
    digitalWrite(_csPin, HIGH);

    Serial.println("Touch the bottom-right corner, hold it down until the next message...");
    delay(3000);
    digitalWrite(_csPin, LOW);
    cal.xMax = readSPI(CMD_READ_X);
    cal.yMax = readSPI(CMD_READ_Y);
    digitalWrite(_csPin, HIGH);

    Serial.println("Calibration done!");
}

bool XPT2046_Bitbang::loadCalibration() {
    File calFile = SPIFFS.open("/calxpt2040.txt", "r");
    if (!calFile) {
        return false;
    }
    cal.xMin = calFile.parseInt();
    cal.yMin = calFile.parseInt();
    cal.xMax = calFile.parseInt();
    cal.yMax = calFile.parseInt();
    calFile.close();
    return true;
}

void XPT2046_Bitbang::saveCalibration() {
    File calFile = SPIFFS.open("/calxpt2040.txt", "w");
    if (!calFile) {
        return;
    }
    calFile.println(cal.xMin);
    calFile.println(cal.yMin);
    calFile.println(cal.xMax);
    calFile.println(cal.yMax);
    calFile.flush();
    calFile.close();
}

void XPT2046_Bitbang::setCalibration(int xMin, int yMin, int xMax, int yMax) {
    cal.xMin = xMin;
    cal.yMin = yMin;
    cal.xMax = xMax;
    cal.yMax = yMax;
}


TouchPoint XPT2046_Bitbang::getTouch() {
    digitalWrite(_csPin, LOW);

    uint16_t z1 = readSPI(CMD_READ_Z1);
    uint16_t z = z1 + 4095;
    uint16_t z2 = readSPI(CMD_READ_Z2);
    z -= z2;

    if(z < 100) {
        return TouchPoint{0, 0, 0, 0, 0};
    }

    uint16_t xRaw = readSPI(CMD_READ_X);
    uint16_t yRaw = readSPI(CMD_READ_Y);
    //uint16_t yRaw = readSPI(CMD_READ_Y & ~((byte)1));
    digitalWrite(_csPin, HIGH);

    // Rotate and map
    /*  The conventions of x, y, height, width etc may make no sense but
        this is what ended up hacking and slashing into alignment to function 
        as expected in the end
    */
    uint16_t x = map(xRaw, cal.xMin, cal.xMax, 0, TFT_HEIGHT);
    uint16_t y = map(yRaw, cal.yMin, cal.yMax, 0, TFT_WIDTH);

    if (x > TFT_HEIGHT){
        x = TFT_HEIGHT;
    }
    if (x < 0) {
        x = 0;
    }
    if (y > TFT_WIDTH) {
        y = TFT_WIDTH;
    }
    if (y < 0) {
        y = 0;
    }
    /*
    uint16_t val = x;
    x=y;
    y=val;
    */
    return TouchPoint{x, y, xRaw, yRaw, z};
}
