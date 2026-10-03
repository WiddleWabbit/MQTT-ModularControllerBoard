# Programming reference

[Home](../home.md) · Guide: [Programming](../programming.md)

Source: `lib/Logic/Programming/Programming.h`, `ProgrammingSession.h`, `IspProgrammer.h`. The latch adapter is `lib/Drivers/Esp32ProgrammingLatch.h`.

## Purpose

The controller acts as an Arduino-as-ISP programmer for an ATmega328PB. The sensor and solenoid modules have a separate 2×3 ISP header (MISO, VCC, SCK, MOSI, RESET, GND). That header is not the slot connector, so the module under test sits off the motherboard and is jumpered from an empty firmware slot 1.

`program updi` is reserved. The pump module is an ATtiny1614 and is not programmed by this firmware.

## Classes

`Programming::begin()` quiesces `ModuleBus` and holds reset idle-high. A second `begin()` while already active does not pulse reset. `update()` services STK500 until `ProgrammingSession` ends the session. `active()` is what `loop()` branches on.

`IspProgrammer` speaks the STK500v1 subset. `ProgrammingSession` owns the silence timer and the unplug rule. The session does not end on `STK_LEAVE`.

## Pins

Jumper firmware slot 1 to the module ISP header:

| ISP signal | Controller net |
| --- | --- |
| MOSI | GPIO11 (`MOSI_R` on the slot header) |
| MISO | GPIO13 (`MISO_R`) |
| SCK | GPIO12 (`SCK_R`) |
| RESET | Slot 1 CS, GPIO6 |
| VCC | Slot 3.3 V |
| GND | Slot GND |

The slot header is the schematic connector. There is no silkscreen slot number. Physical left-to-right on the board is schematic slot 4 toward slot 1, so this plug is the right-hand header. SPI to every slot is the same bus, with series resistors on the `_R` nets. Program with the other modules removed. The ISP clock is 125 kHz, which is slow enough for those resistors.

Power is 3.3 V only. The sensor runs from its internal 8 MHz oscillator, so the ISP clock must stay at or below 2 MHz. This firmware always clocks SPI at 125 kHz. avrdude's `-B5` is accepted and does not change that clock.

## Commands

While USB is plugged in and no session is active, `program` and `program isp` both reply:

```text
OK programming
ISP slot 1: MOSI GPIO11, MISO GPIO13, SCK GPIO12, RESET GPIO6, 3V3, GND
```

Any other `program ...` line, including `program updi`, replies `ERR program` and does not start a session.

The sensor project's Upload_ISP environment uses `upload_protocol = stk500v1`, `upload_speed = 19200`, and `-B5`. Point `upload_port` at this controller's COM port. 19200 baud is ignored on this USB CDC port. Close the monitor before upload. If the port stays open, avrdude cannot take it. If `OK programming` and the pin line never appear, the flashed firmware does not implement the command.

Build the sensor image first, send `program`, wait until status text stops, close the monitor, and upload within 60 seconds.

## Session

`program` prints those two lines, stores the RTC marker, and gives the USB byte stream to STK500. While the session is active, `loop()` updates Wi-Fi, NTP, and MQTT, then the programmer, and returns. It does not read the console, publish slot status, update the sensor, solenoid, or pump modules, or print the heap lines. Desired-state messages can still arrive. They are applied when those modules run again after the session.

Absence timers keep counting. A session longer than `kSolenoidCommandTimeoutMs` (15 minutes) or `kPumpCommandTimeoutMs` (3 minutes) makes the next module update turn those outputs off. A shorter session leaves outputs as they were.

The session ends 60 seconds after the last STK500 byte, or 60 seconds after it started when none arrive (`kProgrammingIdleTimeoutMs`).

After the USB link has been present in this session, it also ends once that link has stayed absent for 1 second (`kProgrammingUnplugTimeoutMs`). The unplug timer arms on the first `update()` while unplugged after the port was seen. Time before that update does not count. A session that has not seen the link yet stays up until the silence timer. A brief re-enumeration gap does not end it.

When the session ends, slot 1 CS returns to an input with a pull-up, SPI is released, the marker is cleared, and the normal loop resumes. `loop()` clears the latch and, if `startController()` was skipped, calls it. That prints `Powered on, Initialising..`, the PSRAM line, and `Beginning I2C Communication.` Those lines appear only when a prior reset skipped init. If `program` was sent and the chip never reset before the session ended, init already ran and they are not printed again. Ending the session does not reset the chip.

### RTC marker

The marker is one uninitialized word, magic `0x49535031`, in `Esp32ProgrammingLatch`. It is `RTC_NOINIT_ATTR` in `.rtc_noinit` (NOLOAD) at `0x50000000`. That section is not reloaded from the firmware image, so a USB-open reset or a software restart keeps it. `RTC_DATA_ATTR` is not used: on this SDK it is copied back from its flash image on every restart except deep sleep.

The reset button pulls `CHIP_PU` low, and a power cycle removes RTC power, so both clear the word. After those, `status` is answered again.

`setup()` enters STK500 only if the word still matches, and then returns without the 2 second delay or `startController()`, so that boot prints nothing.

Desktop tests do not cover the `RTC_NOINIT` attribute.

## USB-open restart

avrdude opens the port with DTR/RTS. On this board that reset line is the USB Serial/JTAG controller, so the ESP32 may restart, or the ROM download mode may take the port. The firmware does not add a second USB stack. If the application restarts, setup sees the marker, skips the banner, and starts STK500 before Wi-Fi, NVS, and the banner. The `program` command has already printed `OK programming` before any reset.

If the ROM download mode captures the port, avrdude will not get a sync answer. That has to be checked on the board. Desktop tests do not cover it.

`HWCDC::isPlugged` tracks host start-of-frame, not an open COM application. The plugged confirm is 5 ms. avrdude sync bytes that spell `Free Heap` mean the normal loop is running, not STK500. Heap lines print every 30 seconds only while the session is inactive.

A failed programming enable yields `STK_FAILED` to avrdude with no human-readable line. The physical check is avrdude reporting the device initialized and signature `0x1E9516` (ATmega328PB).

## ISP sequence

SPI starts first, mode 0, MSB first, SCK idle low. Reset (slot 1 CS) is then driven low. After 20 ms the programmer sends `AC 53 00 00`. Success is the third response byte equal to `0x53`. It tries three times. Between tries it pulses reset high, then low, and waits 20 ms again. After three failures it replies STK500 failed and drives reset high as an output.

A second enter, while already programming, replies OK and does not pulse reset. Leave programming ends SPI and drives reset high. The input pull-up is restored when the session ends, not on leave. `ModuleHost::begin()` configures CS only once, so the session restores the pin itself.

Page writes wait 5 ms. Each EEPROM byte waits 10 ms. Chip erase waits 10 ms, including a universal `AC 80 00 00`. The signature is read from the chip. Fuse writes are passed through. A wrong clock fuse can stop ISP until a high-voltage programmer is used.

The spoken protocol is sync, sign-on `AVR ISP`, parameters, device setup, programming enable, universal, paged flash and EEPROM read/write, signature, and chip erase. Unknown commands answer `STK_UNKNOWN` when the next byte is the end marker.

## Later hardware

A later board can add a 2×3 header wired the same way, plus one spare GPIO for UPDI. GPIO9, GPIO10, GPIO14, GPIO17, and GPIO18 are free on this MCU and are not routed for that. This firmware does not drive them.

## Configuration

| Constant in `src/main.cpp` | Value | Role |
| --- | --- | --- |
| `kProgrammingIdleTimeoutMs` | 60 seconds | No STK500 byte ends the session |
| `kProgrammingUnplugTimeoutMs` | 1 second | Absent frames after the link has been seen |

SPI is fixed at 125 kHz.

## Tests

`test/test_desktop/test_isp.cpp` covers the STK500 subset, the reset-before-SPI order, the fixed 125 kHz clock, the session idle path, an unplug before the port has been seen, a sub-second unplug that leaves STK500 running, and a full one-second unplug. A second `begin()` after that confirmed unplug starts a new seen-port count. `test_modules.cpp` covers host quiesce. `test_configuration.cpp` covers `program`, `program isp`, and `program updi`. The `.rtc_noinit` marker is ESP32-only.

The first check on the board, after a `custom-esp32` build, is that avrdude syncs after the USB-open restart and a 328PB returns its signature. The controller was not flashed as part of writing this page.
