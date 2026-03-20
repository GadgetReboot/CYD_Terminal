# CYD_Terminal
Cheap Yellow Display Serial Terminal<br><br>
This project was modified from the original at https://github.com/CheshirCa/CYD_terminal <br>
It was changed as needed to use the ILI9341 display instead of ST7789 and to
allow different SPI pins for the display driver and the touch interface controller.<br>
The original project assumed both were using the same SPI bus
<br><br>
To use:<br><br>

Copy the file User_Setup_CYD_ILI9341.h to the Arduino library folder where TFT_eSPI is installed.<br>
This will tell the TFT_eSPI library which pins and other configurations to use.<br>
Edit the User_Setup_Select.h file in the TFT_eSPI library folder and add the line <br>
   #include <User_Setup_CYD_ILI9341.h>  <br>
while commenting out any other #include lines for other User_Setup files (only one user setup file should be included).
