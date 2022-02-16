# LEDclock
Big DIY clock based on LED filaments
# Introduction
LEDclock project is a simple, yet very big and bright clock based on chinesium 3V LED filaments (designed to look like a incadescent bulb tungsten filament... from the distance, I guess).
# Features
- 24 hour clock,
- time setting via two buttons (one for minutes, one for hours),
- long press button to set time faster,
- blinking after power loss to indicate that the time is incorrect,
- RTC calibration with 1 ppm precision (+- 999 ppms),
- slow, gradual enabling/disabling changed screen segments (PWM),
- brightness setting,
- watchdog,
- calibration and brighness storage on eeprom.
# How to
To increment minutes press upper button, to increment hours press lower button. Long press for fast change
Press both buttons long to change mode. Available modes:
- RTC calibration,
- brightness setting,
- normal operation (clock mode).
After 5 seconds of buttons not being pressed display will return to clock mode.
## RTC calibration
Display: Cxxx for positive (making clock faster), Exxx for negative calibration. Press upper button to increase calibration value, lower button to decrease. Allows for calibration from -999 to 999 ppm.
## Brighness
Display: b  x. Press upper button to increase brightness, lower to decrease. Brightness levels from 0 to 8 are available.
# I want to build one!
That's great! I am providing everything you need to make one yourself.
## Making PCB
Send gerber files to the PCB manufactuer of your choice. Make sure to get 28 milled slots done - it is essential, as LEDs are mounted on the bottom side and are vieved from the top side.
## Collecting elements
Please see bom.csv file with all needed components. You can get LED filaments from here https://pl.aliexpress.com/item/4000478265055.html. Make sure to get 3V version!
## Soldering
This project uses pretty much only SMD elements, but all of them can be mounted using basic soldering iron.
**Dot LEDs can interfere with ISP interface (depending on the programmer)!** Mount R12 and R13 resistors *after* downloading FW to the board.
## Downloading FW
My suggestion is to use USBasp programmer and avrdude software tool. I've provided needed commands in Makefile.
First we need to set fuse byte to change default 1 MHz CPU clock to 8 MHz:
*make fuse*
than we can download FW:
*make install*
Done! Clock should now be ready to be used.
