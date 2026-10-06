# freertos_hello

## Overview

The Hello World project is a simple demonstration program that uses the SDK UART and GPIO drivers in
combination with FreeRTOS. The purpose of this demo is to show how to use the debug console, how to
blink an LED from an RTOS task, and to provide a simple project for debugging and further development.

The example application creates two tasks:

| Task             | Priority                       | Stack (words)                        | Behaviour                                                              |
|------------------|--------------------------------|--------------------------------------|------------------------------------------------------------------------|
| `hello_task`     | `configMAX_PRIORITIES - 1`     | `configMINIMAL_STACK_SIZE + 100`     | Prints "Hello world." via the debug console, then suspends itself.     |
| `led_task`       | `configMAX_PRIORITIES - 2`     | `configMINIMAL_STACK_SIZE + 64`      | Toggles the board LED every `LED_TOGGLE_PERIOD_MS` (500 ms).           |

The LED GPIO is initialized in `main()` before the scheduler starts. `led_task` uses
`vTaskDelayUntil()` so the blink period stays fixed and does not drift; unlike the bare-metal
`led_blinky` example, it does not busy-wait, so the CPU is free for other tasks between toggles.

The LED pin is selected by `BOARD_LED_PORT` / `BOARD_LED_PIN` in `board/app.h`, and the blink
period by `LED_TOGGLE_PERIOD_MS` in `source/freertos_hello.c`.

## Project structure

| Folder                                                                    | Content                                                                                |
|---------------------------------------------------------------------------|----------------------------------------------------------------------------------------|
| `source/`                                                                 | Application (`freertos_hello.c`) and FreeRTOS configuration                            |
| `board/`                                                                  | Board support: pin mux, clocks, `app.h` (LED selection), `hardware_init.c`             |
| `scripts/`                                                                | `build.ps1` / `flash.ps1` - build and flash from PowerShell / VS Code terminal         |
| `doc/`                                                                    | This guide and the board-specific readme                                               |
| `freertos/`                                                               | FreeRTOS kernel                                                                        |
| `drivers/`, `device/`, `CMSIS/`, `startup/`, `component/`, `utilities/`   | MCUXpresso SDK files                                                                   |
| `Debug/`                                                                  | Build output (`.axf`) - created by the build, not stored in git                        |

## Build and flash

### Requirements

- MCUXpresso IDE (to build the project)
- NXP LinkServer installed in `C:\nxp\LinkServer_*` (installed together with MCUXpresso IDE)
- LPCXpresso55S16 board connected to the PC through the debug USB port (J1)

### 1. Build

Either build in the IDE: import the project (*File > Import > Existing Projects into Workspace*,
without copying it into the workspace) and run *Project > Build Project*.

Or build from a VS Code terminal or PowerShell in the project folder, no IDE window needed:

```powershell
.\scripts\build.ps1                        # incremental Debug build
.\scripts\build.ps1 -Clean                 # full rebuild
.\scripts\build.ps1 -Config Release        # Release build
.\scripts\build.ps1 -Flash                 # build, then flash the board (calls flash.ps1)
.\scripts\build.ps1 -Clean -Flash          # full rebuild + flash
Get-Help .\scripts\build.ps1 -Detailed     # all options
```

`build.ps1` runs the MCUXpresso IDE command-line builder (`mcuxpressoidec.exe`, newest
`C:\nxp\MCUXpressoIDE_*`) with the same build settings as the IDE. It uses its own private
workspace under `%LOCALAPPDATA%\mcux-headless`, so it also works while the IDE is open. A full
build takes about 10-20 s and ends with:

```text
Build Finished. 0 errors, 0 warnings.
...
Build OK (built) in 10 s: ...\Debug\lpcxpresso55s16_freertos_hello.axf
```

Either way the firmware is written to `Debug\lpcxpresso55s16_freertos_hello.axf`.

### 2. Flash

Either use the IDE (*Debug* or *GUI Flash Tool*), or run the flash script from a VS Code terminal or
PowerShell in the project folder:

```powershell
.\scripts\flash.ps1                        # flash Debug\lpcxpresso55s16_freertos_hello.axf
.\scripts\flash.ps1 -Config Release        # flash the Release build
.\scripts\flash.ps1 -EraseAll              # mass-erase the flash before programming
.\scripts\flash.ps1 -Probe <serial>        # pick a probe when several are connected
.\scripts\flash.ps1 -NoReset               # program only, do not start the firmware
.\scripts\flash.ps1 -Image <file.axf>      # flash another image (.axf/.elf/.hex/.srec)
Get-Help .\scripts\flash.ps1 -Detailed     # all options
```

The script finds LinkServer automatically, prints the image and its build time, programs the flash
and resets the board. A successful run ends with:

```text
Pb: (100) Finished writing Flash successfully.
...
Flashing done.
```

Both scripts return exit code 0 on success and non-zero on failure, so they can also be used from
other scripts or a VS Code task.

### Troubleshooting

| Message                                                        | Fix                                                                         |
|----------------------------------------------------------------|-----------------------------------------------------------------------------|
| `Firmware image not found ...`                                 | Build first: `.\scripts\build.ps1` (the `Debug/` folder is not in git).     |
| `Build FAILED ...`                                             | Fix the compiler errors printed above it, then build again.                 |
| `mcuxpressoidec.exe not found`                                 | Install MCUXpresso IDE, or pass `-McuxIde <path to mcuxpressoidec.exe>`.    |
| `LinkServer.exe not found`                                     | Install NXP LinkServer, or pass `-LinkServer <path to LinkServer.exe>`.     |
| `Flashing FAILED` / no probe found                             | Check the USB cable on J1 and close any running debug session in the IDE.   |
| `... cannot be loaded because running scripts is disabled`     | Run once: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.            |
| `BlankCheck ... rc 105` warning                                | Harmless: the flash held an older program; it is erased and rewritten.      |

## Running the demo

After the board is flashed the terminal will print the "Hello world." message once, and the board
LED starts blinking (500 ms on, 500 ms off, i.e. 1 Hz).

The debug probe provides a virtual COM port named *LPC-LinkII UCom Port* (see Device Manager for the
COM number). Open it at 115200 8N1, then press RESET on the board to see the message.

Example output:
Hello world.

## Supported Boards

- [LPCXpresso55S16](example_board_readme.md)

Note: The LED task in this project is specific to the LPCXpresso55S16 board. The original SDK
example (UART output only) is available for many other boards in the MCUXpresso SDK.
