Hardware requirements
=====================
- Mini/micro USB cable
- LPCXpresso55S16 board
- Personal Computer

Board settings
============
Connect a USB2COM between the PC host and the board UART pins
boards           -               USB2COM
J14-Pin26                        Tx
J14-Pin28                        Rx
J14-Pin1                         GND

The jumper setting:
    Default jumpers configuration does not work,  you will need to add JP20 and JP21 (JP22 optional for ADC use)

LED used by the demo:
    The led_task blinks the blue LED of the on-board RGB LED, connected to PIO1_6.
    The LED is active-low (driving the pin low turns it on). The pin is configured as GPIO output
    in main() and starts with the LED off.
    To use another color, change BOARD_LED_PORT / BOARD_LED_PIN in board/app.h, e.g.
    BOARD_LED_RED_GPIO_PORT / BOARD_LED_RED_GPIO_PIN (PIO1_4) or
    BOARD_LED_GREEN_GPIO_PORT / BOARD_LED_GREEN_GPIO_PIN (PIO1_7).

Prepare the Demo
===============
1.  Connect a micro USB cable between the PC host and the CMSIS DAP USB port (J1) on the board
2.  Open a serial terminal with the following settings (See Appendix A in Getting started guide for description how to determine serial port number):
    - 115200 baud rate
    - 8 data bits
    - No parity
    - One stop bit
    - No flow control
3.  Download the program to the target board, either from the IDE or with the flash script
    (see "Build and flash from the command line" below):
        .\scripts\flash.ps1
4.  Either press the reset button on your board or launch the debugger in your IDE to begin running the demo.

Expected result:
    - "Hello world." is printed once on the serial terminal.
    - The blue LED blinks with a 500 ms on / 500 ms off period.

Build and flash from the command line
=====================================
The firmware can be built and flashed from a VS Code terminal or PowerShell with the scripts in the
scripts folder (build.ps1 uses MCUXpresso IDE from C:\nxp\MCUXpressoIDE_* in headless mode,
flash.ps1 uses NXP LinkServer from C:\nxp\LinkServer_*). See readme.md for all options.

    cd <path>\lpcxpresso55s16_freertos_hello
    .\scripts\build.ps1 -Flash                 # build, then flash the board
    .\scripts\build.ps1 -Clean                 # full rebuild only
    .\scripts\flash.ps1                        # flash Debug\lpcxpresso55s16_freertos_hello.axf
    .\scripts\flash.ps1 -Config Release        # flash the Release build
    .\scripts\flash.ps1 -EraseAll              # mass-erase before programming
    .\scripts\flash.ps1 -Probe CSAYBQOQ        # pick a probe when several are connected
    Get-Help .\scripts\flash.ps1 -Detailed     # all options

Close any running debug session in the IDE before flashing, since it holds the probe.
