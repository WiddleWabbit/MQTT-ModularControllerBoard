# Programming reference

[Home](../home.md) · Guide: [Programming](../programming.md)

Source: `lib/Logic/Programming/Programming.h`, `ProgrammingSession.h`, `IspProgrammer.h`, `UpdiProgrammer.h`. The latch adapter is `lib/Drivers/Esp32ProgrammingLatch.h`. The UPDI UART adapter is `lib/Drivers/Esp32HalfDuplexUart.h`.

## Purpose

The controller is the programmer. Sensor and solenoid modules are an ATmega328PB with a 2×3 ISP header (MISO, VCC, SCK, MOSI, RESET, GND). The pump module is an ATtiny1614 with a UPDI header. Neither header is the slot connector, so the board under test sits off the motherboard and is jumpered from one empty firmware slot.

ISP uses the shared SPI pins plus the selected slot's CS pin as RESET. Remove the other modules, because SPI is one bus. UPDI uses only that slot's CS pin. SPI is not started.

Accepted command lines and `ERR program` are in the [configuration reference](configuration.md).

## Classes

`Programming::begin(slot, method)` selects CS for that slot. ISP calls `IspProgrammer::start`, which drives that pin high as an output. UPDI calls `UpdiProgrammer::start`, which leaves the pin as an input with a pull-up until a break. Both then call `ProgrammingSession::begin`, which quiesces `ModuleBus` and does not drive the pin. A slot outside 1..4 returns before that, so the bus keeps polling. A second `begin` while `active()` is true returns without pulsing reset or attaching the UART.

`update()` services the active programmer until `ProgrammingSession` ends the session. `active()` is what `loop()` branches on. `STK_LEAVE` and jtagice leave do not end the session.

`IspProgrammer` speaks the STK500v1 subset. `UpdiProgrammer` speaks jtag2updi for NVM controller version 1. `ProgrammingSession` owns the silence timer and the unplug rule.

## Pins

GPIO numbers come from `kProgrammingPins` in `src/main.cpp`.

ISP, any slot. The shared nets are the same for every slot. RESET is that slot's CS pin.

| ISP signal | Controller net |
| --- | --- |
| MOSI | GPIO11 |
| MISO | GPIO13 |
| SCK | GPIO12 |
| RESET | Slot 1 GPIO6, slot 2 GPIO7, slot 3 GPIO15, slot 4 GPIO16 |
| VCC | Slot 3.3 V |
| GND | Slot GND |

The console prints that mapping. Slot 3 is:

```text
OK programming
ISP slot 3: MOSI GPIO11, MISO GPIO13, SCK GPIO12, RESET GPIO15, 3V3, GND
```

UPDI, any slot. The single wire is that slot's CS pin. Slot 4 is:

```text
OK programming
UPDI slot 4: UPDI GPIO16, 3V3, GND
```

The slot header is the schematic connector. There is no silkscreen slot number. Physical left-to-right on the board is schematic slot 4 toward slot 1. SPI to every slot is the same bus, with series resistors on the `_R` nets. The ISP clock is 125 kHz, which is slow enough for those resistors.

Power is 3.3 V only. The sensor runs from its internal 8 MHz oscillator, so the ISP clock must stay at or below 2 MHz. This firmware always clocks SPI at 125 kHz. avrdude's `-B5` is accepted and does not change that clock.

Other slots' CS pins stay as they were. Shutdown restores only the claimed CS pin to an input with a pull-up.

## Commands

The host tools, pointed at this controller's COM port:

```text
avrdude -c stk500v1 -P <COM> -b 19200 -B5 -p m328pb
avrdude -c jtag2updi -P <COM> -b 115200 -p t1614
```

Both baud rates are ignored on this USB CDC port. `HWCDC::baudRate()` is fixed at 115200, and USB Serial/JTAG does not expose line coding or break. The double break that enables UPDI is done by this firmware, on the selected CS pin, when avrdude sends the device descriptor. Close the monitor before upload. If the port stays open, avrdude cannot take it.

Build the image first, send the program line, wait until status text stops, close the monitor, and upload within 60 seconds.

## MQTT

Programming subscribes to nothing and publishes nothing. The topics the other modules use are in the [MQTT reference](mqtt.md).

While a session is active, `loop()` still runs `network.update()`, so a command that arrives is recorded by the module that owns it. Slot status and sensor, solenoid, and pump results are published when those modules run again after the session.

## Session

The console prints `OK programming` and the pin line, `loop()` stores the RTC record, and the USB byte stream goes to the programmer. While the session is active, `loop()` updates Wi-Fi, NTP, and MQTT, then the programmer, and returns. It does not read the console, publish slot status, update the sensor, solenoid, or pump modules, or print the heap lines. Desired-state messages can still arrive. They are applied when those modules run again after the session.

Absence timers keep counting. A session longer than `kSolenoidCommandTimeoutMs` (15 minutes) or `kPumpCommandTimeoutMs` (3 minutes) makes the next module update turn those outputs off. A shorter session leaves outputs as they were.

The session ends 60 seconds after the last protocol byte, or 60 seconds after it started when none arrive (`kProgrammingIdleTimeoutMs`). A protocol byte is an STK500 byte or a jtagice frame byte.

After the USB link has been present in this session, it also ends once that link has stayed absent for 1 second (`kProgrammingUnplugTimeoutMs`). The unplug timer arms on the first `update()` while unplugged after the port was seen. Time before that update does not count. A session that has not seen the link yet stays up until the silence timer. A brief re-enumeration gap does not end it.

When the session ends, the claimed CS pin returns to an input with a pull-up. ISP releases SPI. UPDI detaches the UART. The record is cleared, and the normal loop resumes. `loop()` clears the latch and, if `startController()` was skipped, calls it. That prints `Powered on, Initialising..`, the PSRAM line, and `Beginning I2C Communication.` Those lines appear only when a prior reset skipped init. If the command was sent and the chip never reset before the session ended, init already ran and they are not printed again. Ending the session does not reset the chip.

### RTC record

The record is one uninitialized struct in `Esp32ProgrammingLatch`, `RTC_NOINIT_ATTR` in `.rtc_noinit` (NOLOAD) at `0x50000000`. The first field is the magic `0x50524732`. Then a slot byte (1..4), a method byte (1 is ISP, 2 is UPDI), and two reserved bytes. That section is not reloaded from the firmware image, so a USB-open reset or a software restart keeps it. `RTC_DATA_ATTR` is not used: on this SDK it is copied back from its flash image on every restart except deep sleep.

`isSet()` is true only when the magic matches and the slot and method are in range. The older single-word marker `0x49535031` does not match, so a board that still holds that word boots normally.

The reset button pulls `CHIP_PU` low, and a power cycle removes RTC power, so both clear the record. After those, `status` is answered again.

`setup()` enters the stored slot and method when `isSet()` is true, and then returns without the 2 second delay or `startController()`, so that boot prints nothing. Method 2 is UPDI. Method 1 is ISP.

Desktop tests do not cover the `RTC_NOINIT` attribute.

## USB-open restart

avrdude opens the port with DTR/RTS. On this board that reset line is the USB Serial/JTAG controller, so the ESP32 may restart, or the ROM download mode may take the port. The firmware does not add a second USB stack. If the application restarts, setup sees the record, skips the banner, and starts the same programmer before Wi-Fi, NVS, and the banner. The command has already printed `OK programming` before any reset.

If the ROM download mode captures the port, avrdude will not get a sync answer. That has to be checked on the board. Desktop tests do not cover it.

`HWCDC::isPlugged` tracks host start-of-frame, not an open COM application. The plugged confirm is 5 ms. avrdude sync bytes that spell `Free Heap` mean the normal loop is running. Heap lines print every 30 seconds only while the session is inactive.

## ISP sequence

SPI starts first, mode 0, MSB first, SCK idle low, at 125 kHz. Reset (the selected slot's CS pin) is then driven low. After 20 ms the programmer sends `AC 53 00 00`. Success is the third response byte equal to `0x53`. It tries three times. Between tries it pulses reset high, then low, and waits 20 ms again. After three failures it replies STK500 failed and drives reset high as an output.

A second enter, while already programming, replies OK and does not pulse reset. Leave programming ends SPI and drives reset high. The input pull-up is restored when the session ends, not on leave. `ModuleHost::begin()` configures CS only once, so the session restores the pin itself.

Page writes wait 5 ms. Each EEPROM byte waits 10 ms. Chip erase waits 10 ms, including a universal `AC 80 00 00`. The signature is read from the chip. Fuse writes are passed through. A wrong clock fuse can stop ISP until a high-voltage programmer is used.

The spoken protocol is sync, sign-on `AVR ISP`, parameters, device setup, programming enable, universal, paged flash and EEPROM read/write, signature, and chip erase. Unknown commands answer `STK_UNKNOWN` when the next byte is the end marker.

A failed programming enable yields `STK_FAILED` to avrdude with no human-readable line. The physical check is avrdude reporting the device initialized and signature `0x1E9516` (ATmega328PB).

## UPDI sequence

The host frame is jtagice mkII: start `0x1B`, a little-endian sequence, a 4-byte little-endian body size, token `0x0E`, the body, and the AVR067 CRC-16 little-endian. That CRC starts at `0xFFFF` and uses reflected polynomial `0xA001`. The check value of `123456789` is `0x4B37`. The CRC covers the start byte through the last body byte. A bad CRC replies `RSP_FAILED` (`0xA0`) with the received sequence, and the session stays active. An unknown command replies `RSP_ILLEGAL_COMMAND` (`0xAA`). A bad token or a body longer than 512 bytes is dropped with no reply.

`SET_DEVICE_DESCRIPTOR` always starts a double break, including when the link is already up. The selected CS pin is driven open-drain low for 25 ms, high for 1 ms, then low for 25 ms. One phase advances per `update()`. After the second low, the pin is released high and UART1 attaches on that GPIO at 115200, 8 data bits, even parity, 2 stop bits. UART0 is not used: GPIO43 and GPIO44 are MOD3 and SENSE3. The programmer then writes UPDI `Control_A` `0x06`.

`ENTER_PROGMODE` starts the same break when the link is not up yet. Host bytes wait until the break finishes. The status that follows the break selects the NVM key path. A locked part replies `RSP_ILLEGAL_MCU_STATE`. NVM version is 1 when the flash page in the descriptor is under 256 bytes. The ATtiny1614 page is 64. A page of 256 or more is recorded as version 2, and memory commands then fail. Flash addresses are used as avrdude sends them.

The target wire is one byte out and the local echo back before the next byte. A missing echo or a missing target byte fails the command. Leave programming stops driving the line and leaves the UART attached. `shutdown()`, when the session ends, detaches UART1 and restores the CS pull-up.

The physical check is avrdude reporting signature `0x1E9422` (ATtiny1614).

## Configuration

| Constant in `src/main.cpp` | Value | Role |
| --- | --- | --- |
| `kProgrammingIdleTimeoutMs` | 60 seconds | No protocol byte ends the session |
| `kProgrammingUnplugTimeoutMs` | 1 second | Absent frames after the link has been seen |
| `kProgrammingPins` | GPIO11, GPIO13, GPIO12, CS GPIO6 / GPIO7 / GPIO15 / GPIO16 | Pin line and the UPDI GPIO |

ISP SPI is fixed at 125 kHz. UPDI is fixed at 115200 8E2. avrdude's baud parameter does not change either.

## Tests

`test/test_desktop/test_isp.cpp` covers the STK500 subset, the reset-before-SPI order, the fixed 125 kHz clock, the session idle path, an unplug before the port has been seen, a sub-second unplug that leaves the session running, and a full one-second unplug. A second `begin()` after that confirmed unplug starts a new seen-port count.

`test/test_desktop/test_updi.cpp` covers the AVR067 CRC check value, the jtagice sign-on body, the 25/1/25 ms break and `Control_A`, a signature read of `1E 94 22`, chip erase and a 64-byte flash page, a bad CRC, an unknown command, leave versus shutdown, and that the UPDI programmer object does not own an SPI master.

`test/test_desktop/test_deep_modules.cpp` covers slot 3 ISP leaving the other CS pins alone, slot 2 UPDI attaching GPIO7 at 115200 without starting SPI, a second `begin` that does not pulse or reattach, 60 s silence and a seen-then-absent 1 s unplug for both methods, and `begin(0)` / `begin(5)` leaving the bus polling. `test/test_desktop/test_configuration.cpp` covers the accepted lines and `ERR program`.

The `.rtc_noinit` record is ESP32-only. The first check on the board, after a `custom-esp32` build, is that avrdude syncs after the USB-open restart. A 328PB returns `0x1E9516`. An ATtiny1614 returns `0x1E9422`. The controller was not flashed as part of writing this page.

## Errors

| Condition | Result |
| --- | --- |
| Line is not `program N isp` or `program N updi` with N in 1..4 | `ERR program`, no session |
| `begin` with a slot outside 1..4 | Inactive, bus not quiesced |
| Second `begin` while active | No extra reset pulse, no UART reattach |
| ISP programming enable fails three times | `STK_FAILED`, reset left high |
| UPDI CRC mismatch | `RSP_FAILED` (`0xA0`), session stays active |
| UPDI unknown command | `RSP_ILLEGAL_COMMAND` (`0xAA`), session stays active |
| UPDI not in programming mode, or NVM version 2 | Memory command fails, session stays active |
| Missing UPDI echo or target byte | That command fails |
| 60 s with no protocol byte | Session ends, claimed CS returns to pull-up |
| Link seen, then absent for 1 s | Session ends, claimed CS returns to pull-up |
