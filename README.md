# CYD_Terminal
Cheap Yellow Display (CYD) Serial Terminal<br><br>
<br><br>![PCB](CYD_Front.jpg)<BR><BR>
<br><br>![PCB](CYD_Back.jpg)<BR><BR>
This project was modified from the original at https://github.com/CheshirCa/CYD_terminal <br>
It was changed as needed to use the ILI9341 display instead of ST7789 and to
allow different SPI pins for the display driver and the touch interface controller.<br>
The original project assumed both were using the same SPI bus
<br><br>

There are several different versions of the CYD.<br>
The one used in this project has a 2.8" display and uses the ILI9341 display driver. <br><br>

To use:<br><br>

Make sure the TFT_eSPI library is installed in the Arduino IDE.<br><br>

Copy the file User_Setup_CYD_ILI9341.h from the sketch folder to the Arduino library folder where TFT_eSPI is installed.<br>
This will tell the TFT_eSPI library which pins and other configurations to use.<br>
Edit the User_Setup_Select.h file in the TFT_eSPI library folder and add the line <br>
   #include <User_Setup_CYD_ILI9341.h>  <br>
while commenting out any other #include lines for other User_Setup files (only one user setup file should be included). <br><br>

To calibrate the touch screen coordinates before initial use, <br>
edit the file XPT2046_Bitbang.cpp and change<br>
#define RERUN_CALIBRATE false <br>
to <br>
#define RERUN_CALIBRATE true  <br>
This will force touch calibration upon power up and the settings will be saved on the module.<br>
Instructions will appear in the serial monitor, instructing the user to tap and hold the upper left corner, then the lower right corner of the display to get the calibration data.<br><br>
Once calibrated, change back to <br>
#define RERUN_CALIBRATE false <br>
and upload the sketch again so it will continue using the saved touch calibration data without forcing re-calibration upon power up.<br><br>

If the display has inverted colors, edit display.cpp and look for the line <br>
 tft.invertDisplay( true ); <br>
 Comment or uncomment the line as needed to get the colors correct.<br><br>
