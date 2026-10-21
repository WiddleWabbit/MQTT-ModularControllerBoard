# Programming reference

[Home](../home.md) · Guide: [Programming](../programming.md)

Source: `lib/Logic/Programming/Programming.h`, `ProgrammingSession.h`, `IspProgrammer.h`, `UpdiProgrammer.h`, `UpdiProgrammer.cpp` (`kNvmWaitMs`). The latch adapter is `lib/Drivers/Esp32ProgrammingLatch.h`. The UPDI UART adapter is `lib/Drivers/Esp32HalfDuplexUart.cpp` (`kReadTimeoutMs`).

## Purpose

The controller is the programmer. Sensor and solenoid modules are an ATmega328PB with a 2×3 ISP header (MISO, VCC, SCK, MOSI, RESET, GND). The pump module is an ATtiny1614 with a UPDI header. Neither header is the slot connector, so the board under test sits off the motherboard and is jumpered from one empty firmware slot.

ISP uses the shared SPI pins plus the selected slot's CS pin as RESET. Remove the other modules, because SPI is one bus. UPDI uses that same CS pin. SPI is not started. Each CS net has a 10 kΩ pull-up on the controller. The MOD nets do not.

Accepted command lines and `ERR program` are in the [configuration reference](configuration.md).

## Classes

`Programming::begin(slot, method)` selects the pin for that method. ISP calls `IspProgrammer::start` on the slot CS pin, which drives it high as an output. UPDI calls `UpdiProgrammer::start` on that same CS pin, which leaves it as an input with a pull-up until a break. Both then call `ProgrammingSession::begin`, which quiesces `ModuleBus` and does not drive the pin. A slot outside 1..4 returns before that, so the bus keeps polling. A second `begin` while `active()` is true returns without pulsing reset or attaching the UART.

`update()` services the active programmer until `ProgrammingSession` ends the session. `active()` is what `loop()` branches on. `STK_LEAVE` and jtagice leave do not end the session. `startUpdiCheck` and `pollUpdiCheck` measure one CS pin without a session. That sequence is below.

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

| UPDI signal | Controller net |
| --- | --- |
| UPDI | Slot 1 GPIO6, slot 2 GPIO7, slot 3 GPIO15, slot 4 GPIO16 |
| VCC | Slot 3.3 V |
| GND | Slot GND |

The byte stream uses UART1. Each CS net has a 10 kΩ pull-up to 3.3 V. MOD stays the enumeration select.

The slot header is the schematic connector. There is no silkscreen slot number. Physical left-to-right on the board is schematic slot 4 toward slot 1. SPI to every slot is the same bus, with series resistors on the `_R` nets. The ISP clock is 125 kHz, which is slow enough for those resistors.

Power is 3.3 V only. The sensor runs from its internal 8 MHz oscillator, so the ISP clock must stay at or below 2 MHz. This firmware always clocks SPI at 125 kHz. avrdude's `-B5` is accepted and does not change that clock.

Other slots' CS pins and every MOD pin stay as they were. ISP shutdown restores only the claimed CS pin to an input with a pull-up. UPDI shutdown restores that same CS pin to an input with a pull-up.

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

When the session ends, ISP returns the claimed CS pin to an input with a pull-up and releases SPI. UPDI detaches the UART and returns that same CS pin to an input with a pull-up. The record is cleared, and the normal loop resumes. `loop()` clears the latch and, if `startController()` was skipped, calls it. That prints `Powered on, Initialising..`, the PSRAM line, and `Beginning I2C Communication.` Those lines appear only when a prior reset skipped init. If the command was sent and the chip never reset before the session ended, init already ran and they are not printed again. Ending the session does not reset the chip.

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

The host frame is jtagice mkII: start `0x1B`, a little-endian sequence, a 4-byte little-endian body size, token `0x0E`, the body, and the AVR067 CRC-16 little-endian. That CRC starts at `0xFFFF` and uses reflected polynomial `0x8408`. The check value of `123456789` is `0x6F91`. The CRC covers the start byte through the last body byte. A bad CRC replies `RSP_FAILED` (`0xA0`) with the received sequence, and the session stays active. An unknown command replies `RSP_ILLEGAL_COMMAND` (`0xAA`). A bad token or a body longer than 512 bytes is dropped with no reply.

`SET_DEVICE_DESCRIPTOR` for this part is a 309-byte frame. `kUsbByteBufferBytes` in `src/main.cpp` is 1024, applied to both USB queues before `Serial.begin()`. The CDC driver drops a received byte once the RX queue is full. The 256-byte default dropped the end of that frame, the parser waited, and avrdude reported the resulting `RSP_FAILED` against a later command.

`SET_DEVICE_DESCRIPTOR` always starts a double break, including when the link is already up. UART1 is installed at 115200, 8 data bits, even parity, 2 stop bits, before the line is held low, and the pad stays a GPIO during the break. The selected CS pin is driven open-drain low for 25 ms, switched to an input with the pull-up on for 3 ms, then open-drain low for 25 ms. One phase advances per `update()`. The 3 ms gap is an input pull-up, in parallel with the 10 kΩ on that net. Open-drain high only releases the driver. That gap is one stop bit between the two breaks at the default 4 MHz UPDI clock. The completing `update()` releases the pin, claims it for UART1, and writes UPDI `Control_A` `0x00` (`55 C2 00`) in that same call. That is the register reset value, so the guard stays 128 bit times, about 1.1 ms at 115200. The internal pull-up stays on until the reply read starts, and a guard of two bit times (`0x06`) lets the target pulse before the pull-up is off. The 10 kΩ stays connected. The driver is not installed again there. A fuse-enabled UPDI pin disables itself if SYNCH does not start within about 14 ms of the break release. UART1 carries the bytes.

`ENTER_PROGMODE` starts the same break when the link is not up yet. Host bytes wait until the break finishes. The status that follows the break selects the NVM key path. A locked part replies `RSP_ILLEGAL_MCU_STATE`. On the running-chip path, reset is held (`55 C8 59`) and released (`55 C8 00`). ASI_SYS_STATUS is then polled until bit 1, 2, or 3 is set. Those bits are BOOTDONE, user-row programming, and NVM programming. `UpdiProgrammer::kResetWaitMs` is 200. A clear status byte is read again. A missing status byte is read again, and a byte already sitting in the UART is dropped before the next load. The window is one wait inside that command. When it ends with those bits still clear, the reply is `RSP_NO_TARGET_POWER` (`0xAB`). An echo mismatch fails the command on that instruction. NVM version is 1 when the flash page in the descriptor is under 256 bytes. The ATtiny1614 page is 64. A page of 256 or more is recorded as version 2, and memory commands then fail. Flash addresses are used as avrdude sends them.

A successful enter stores Control A `0x03` (`55 C2 03`) once, after the page-buffer clear and before the progmode flag. That is 16 bit times, about 139 µs at 115200. A failed enter does not store it. `55 C2 06` is not sent. Leave does not write Control A back. The reset that leave applies restores the register. The bits 1–3 poll is enter and leave (`_cpuReset`). Chip erase does not call it.

Chip erase is command `0x34`, kind `0`. It writes `55 E0` plus `65 73 61 72 45 4D 56 4E`, then loads ASI_KEY_STATUS (`55 87`). Reset (`55 C8 59`, then `55 C8 00`) is pulsed only when bit 3 of that byte is set. It then polls `55 8B` until LOCKSTATUS (bit 0) is clear, for up to `UpdiProgrammer::kResetWaitMs` (200). Bit 0 clear, including status `0x00`, is `RSP_OK`. A missing status byte is read again, and a late byte is dropped. An echo mismatch fails that load immediately. The NVM key is not sent, and programming mode is not entered. A key echo mismatch, a key-status byte without bit 3, or a failed reset store is `RSP_NO_TARGET_POWER`, and the progmode flag is left as it was. LOCKSTATUS still set at the end of the window is the same reply, and the flag is cleared. Erase does not set the progmode flag. After both reset stores have been sent, clearing the flag keeps the closing GO from writing `55 C3 04`.

After that `RSP_OK`, avrdude 8.1 reads the signature without sending enter again. The read is `RSP_ILLEGAL_MCU_STATE`. avrdude prints `could not end programming, aborting` and a leave error, then `avrdude done. Thank you.`, and exits 0. `chip erase failed` is printed only when the erase command itself was not `RSP_OK`.

Before a repeated store, NVM status at `0x1002` is polled until bits 0 and 1 are clear or 20 ms has elapsed. That budget is not `kResetWaitMs`. An idle status returns on the first read.

GO (`0x08`) and sign-off store Control B `0x04` (`UPDIDIS`, wire bytes `55 C3 04`) only after enter has reached NVM programming. That store disables the UPDI pin until the next chip reset, so a later `check` is silent until the pump is reset. A close that never entered programming mode leaves the pin enabled and still answers `RSP_OK`.

Each UPDI instruction is one UART write. The ESP32 adapter drives that write push-pull and releases the pad when the shifter is empty. The echoes are already in the FIFO, and the programmer reads them before any target byte. A repeated store sends the later data bytes as their own writes, because an ack sits between them. A missing echo fails the command. During the reset wait, a missing status byte is read again until `kResetWaitMs`. Any other missing target byte fails that command. The UART read wait is 20 ms, which covers the 16.4 ms maximum reply at the default 4 MHz UPDI clock. The wait for a byte that is not buffered releases UART1 TX, removes the pull-up, and leaves RX connected. The target drives a short pulse at the start of each reply bit, and the pull-up would lift that pulse before the UART samples the middle of the bit. When the wait ends, TX and the pull-up are connected again. Leave programming stops driving the line and leaves the UART claimed. `shutdown()`, when the session ends, detaches UART1, including a break that has not claimed the pad yet, and restores that CS pin to an input with a pull-up.

The physical check is avrdude reporting signature `0x1E9422` (ATtiny1614).

## UPDI link check

`check N` does not call `ProgrammingSession::begin`. `active()` stays false, the bus is not quiesced, SPI is not started, and the RTC latch is not written. `startUpdiCheck` refuses a slot outside 1..4, a session that is already active, and a second check while one is running. `begin` releases a check that has already started, then opens the session.

UART1 is installed at 115200 8E2 before the line is held low, and the pad stays a GPIO during the break. The selected CS pin is driven open-drain low for 25 ms. That driven level is `drive low`. The pin then becomes an input with the pull-up on for 3 ms. The level read at the end of that gap is `release`. It is then open-drain low for 25 ms. One phase advances per `pollUpdiCheck`. `loop()` returns before the bus and before `delay(10)` on each of those passes, so that delay does not stretch the 3 ms gap. The poll in the same pass as the start sees about 0 ms elapsed and leaves the pin low.

The completing poll releases the pin, claims it for UART1, and loads `STATUSA` (register 0) with one two-byte write `55 80`. The driver is not installed again there. A fuse-enabled UPDI pin disables itself if SYNCH does not start within about 14 ms of the break release. The status write is push-pull, and the pad is released when the shifter is empty. The pull-up is off only while the status byte is read. The UART is then detached and the pin returns to an input with a pull-up. The other CS pins, and every MOD pin, are left as they were. A `program` command on a pass is taken first, and a pending check is discarded. A low reading at the end of the 3 ms gap still runs the second low and the status write.

`drive low: 0` means that GPIO went low while the first low was driven. `release: 1` means the pad read high at the end of the 3 ms pull-up. `release: 0` means it was still low then. `listen: high` means the pin stayed high through the reply wait. `listen: low` means it was pulled down during that wait, including a falling edge short enough to be a start bit. A low also prints `pulse: <at> us in, <width> us wide`, `edges: <count>`, `widths:` for the first four lows, and `uart: none`, `frame`, `parity`, `break`, or `glitch`. The first pulse number is microseconds from the start of the wait until the line fell. Zero means it was already low. The second number is how long that first low lasted. `edges: 1` is a single dip. Several widths near 2 microseconds are a fast character. Several UART flags at once keep break, then frame, then parity, then glitch. A low that is already there and only a few microseconds wide is the transmitter letting go. A later low about 9 microseconds wide is a 115200 start bit. `status: no echo` means the instruction bytes were not read back. `status: no reply` means those echoes matched and no status byte arrived. A status byte means the chip is on the wire. The printed lines are in the [configuration reference](configuration.md).

## Configuration

Session limits live in `src/main.cpp`. SPI timings live on `IspProgrammer`. UPDI timings live on `UpdiProgrammer`, except `kNvmWaitMs`, which is in `UpdiProgrammer.cpp`, and the UART read wait, which is in `Esp32HalfDuplexUart.cpp`. avrdude's baud parameter does not change the SPI clock or the UPDI baud.

| Constant in `src/main.cpp` | Value | What it sets |
| --- | --- | --- |
| `kProgrammingIdleTimeoutMs` | 60 seconds | Silence with no STK500 or jtagice byte ends the session |
| `kProgrammingUnplugTimeoutMs` | 1 second | After the USB link has been seen, this long absent ends the session |
| `kProgrammingPins` | MOSI GPIO11, MISO GPIO13, SCK GPIO12; CS GPIO6 / GPIO7 / GPIO15 / GPIO16 | Shared SPI, and the CS pin used as ISP RESET and as the UPDI wire, by slot |
| `kUsbByteBufferBytes` | 1024 | USB CDC RX and TX queues, set before `Serial.begin()` |

### SPI

`Esp32SpiMaster` opens mode 0, MSB first, SCK idle low, at `IspProgrammer::kSpiClockHz`.

| Constant | Value | What it sets |
| --- | --- | --- |
| `kSpiClockHz` | 125 kHz | SPI clock for the whole session. A host speed request is ignored |
| `kResetSettleMs` | 20 ms | Reset is driven low, then this wait, then programming enable `AC 53 00 00`. The same wait follows each reset pulse between tries |
| Programming enable | Third response byte `0x53`, three tries | A miss pulses reset high then low. The third miss replies `STK_FAILED` and leaves reset high |
| `kFlashPageWriteMs` | 5 ms | Wait after a flash page is committed, before the next host byte is read |
| `kEepromWriteMs` | 10 ms | Wait after each EEPROM byte |
| `kChipEraseMs` | 10 ms | Wait after chip erase, including universal `AC 80 00 00` |
| `kMaxPageBytes` | 256 | Largest STK500 page body accepted |
| Signature | `0x1E9516` | ATmega328PB, read from the chip. The programmer does not substitute it |

### UPDI

UART1 is 8 data bits, even parity, 2 stop bits. The pad stays a GPIO through the break and is claimed on the update that releases it.

| Constant or store | Value | What it sets |
| --- | --- | --- |
| `kBaud` | 115200 | UART1 rate. The USB baud is ignored |
| `kBreakLowMs` | 25 ms | Each open-drain low of the double break. One phase advances per `update()`, so this is a minimum |
| `kBreakHighMs` | 3 ms | Input pull-up between the two lows, in parallel with the 10 kΩ on the CS net |
| `kMaxBody` | 512 | Largest jtagice body accepted. A longer body is dropped |
| Control A after the break | `0x00`, wire `55 C2 00` | Guard of 128 bit times, about 1.1 ms at 115200. This is the register reset value |
| Control A after a successful enter | `0x03`, wire `55 C2 03` | Guard of 16 bit times, about 139 µs. Stored once, after the page-buffer clear. A failed enter does not store it. `0x06` (2 bit times) is not sent |
| `kResetWaitMs` | 200 ms | Enter and leave poll ASI_SYS_STATUS until bit 1, 2, or 3 is set. Chip erase polls the same register until LOCKSTATUS (bit 0) is clear. A missing byte is retried inside this window |
| `kNvmWaitMs` | 20 ms | Poll of NVM status `0x1002` until bits 0 and 1 are clear, before a repeated store or an NVM command. An idle byte returns on the first read |
| `kReadTimeoutMs` | 20 ms | One UART read of a target byte. A missing echo or any other missing byte fails that command |
| Erase key | `55 E0 65 73 61 72 45 4D 56 4E` | Chip erase, command `0x34` kind `0`. Reset is pulsed only after ASI_KEY_STATUS (`55 87`) has bit 3 set |
| Reset pulse | `55 C8 59`, then `55 C8 00` | Hold, then release. Used by enter, leave, and chip erase |
| NVM key | `55 E0 20 67 6F 72 50 4D 56 4E` | Enter on the running-chip path. Chip erase does not send it |
| Control B on GO or sign-off | `0x04`, wire `55 C3 04` | Disables the UPDI pin until the chip resets. Sent only while the progmode flag is set. Chip erase clears that flag after both reset stores |
| Flash page | From the descriptor. Under 256 bytes is NVM version 1. The ATtiny1614 page is 64. A recorded page of 0 is treated as 64 on a buffered write | A page of 256 or more is version 2, and memory commands fail |
| Signature | `0x1E9422` | ATtiny1614, read from the chip |

avrdude 8.1 waits 200 ms for one flash-page write and 100 ms for a 256-byte read. Those are host windows, not controller constants. The post-enter guard and the 20 ms NVM wait are what keep one page inside the write window.

## Tests

`test/test_desktop/test_isp.cpp` covers the STK500 subset, the reset-before-SPI order, the fixed 125 kHz clock, the session idle path, an unplug before the port has been seen, a sub-second unplug that leaves the session running, and a full one-second unplug. A second `begin()` after that confirmed unplug starts a new seen-port count.

`test/test_desktop/test_updi.cpp` covers the AVR067 CRC check value, the jtagice sign-on body, the 25/3/25 ms break with the UART installed before the first low, an input pull-up for the 3 ms gap, and `Control_A` `0x00` (`55 C2 00`) written only on the completing update, a `Control_A` store as one 3-byte write and the following system-status load as one 2-byte write, a signature read of `1E 94 22` after a slow BOOTDONE, enter that waits out four clear status bytes, enter that retries one missing status byte and drops a late `0x00`, enter that answers `RSP_NO_TARGET_POWER` when the status stays clear for `kResetWaitMs`, an echo mismatch that stops on the reset store, GO and sign-off that leave the pin enabled before programming mode and GO that writes `55 C3 04` after it, chip erase that answers OK when LOCKSTATUS clears (including status `0x00`) without sending the NVM key and then a 64-byte flash page, a lock bit held for `kResetWaitMs`, a key-status byte without bit 3, a key echo mismatch, a missed lock byte that is dropped, enter then erase then GO leaving the pin enabled, an NVM status that stays busy for 20 ms, a successful enter that writes `55 C2 03` once and a failed enter that does not, a bad CRC, an unknown command, leave versus shutdown, the link check's same 25/3/25 break and one `55 80` write on the completing poll, a gap that reads low and still sends that write, a scripted low 140 µs into the wait and 9 µs wide with four 2 µs widths and a frame fault, a dropped echo, one transmitter release and one pull-up removal for that status read and none for a store that has only echoes, and that the UPDI programmer object does not own an SPI master.

`test/test_desktop/test_deep_modules.cpp` covers slot 3 ISP leaving the other CS pins alone, slot 2 UPDI installing UART1 and then claiming CS GPIO7 at 115200 without starting SPI or driving MOD, a second `begin` that does not pulse or reinstall, the 3 ms gap as an input pull-up, 60 s silence and a seen-then-absent 1 s unplug for both methods, `begin(0)` / `begin(5)` leaving the bus polling, a slot 3 link check that stays inactive, holds the 25/3/25 break on CS GPIO15, leaves the seated slot's MOD pin alone, and then lets the bus poll, a slot 1 check that still sends `55 80` when the gap reads low, and `begin` releasing a check already in the first low and restoring that CS pin to an input with a pull-up. `test/test_desktop/test_configuration.cpp` covers the accepted lines, `ERR program`, and `ERR check`.

The `.rtc_noinit` record is ESP32-only. The first check on the board, after a `custom-esp32` build, is that avrdude syncs after the USB-open restart. A 328PB returns `0x1E9516`. An ATtiny1614 returns `0x1E9422`. The controller was not flashed as part of writing this page.

## Errors

| Condition | Result |
| --- | --- |
| Line is not `program N isp` or `program N updi` with N in 1..4 | `ERR program`, no session |
| `check` with a slot outside 1..4, during a session, or during a check | No measurement. The command text is in the [configuration reference](configuration.md) |
| `STATUSA` does not return a byte | Check ends, CS pin returns to an input with a pull-up |
| `begin` with a slot outside 1..4 | Inactive, bus not quiesced |
| Second `begin` while active | No extra reset pulse, no UART reattach |
| ISP programming enable fails three times | `STK_FAILED`, reset left high |
| UPDI CRC mismatch | `RSP_FAILED` (`0xA0`), session stays active |
| UPDI unknown command | `RSP_ILLEGAL_COMMAND` (`0xAA`), session stays active |
| UPDI not in programming mode, or NVM version 2 | Memory command fails, session stays active |
| Missing UPDI echo | That command fails |
| Enter or leave reset status bits 1–3 still clear after `kResetWaitMs` | `RSP_NO_TARGET_POWER` (`0xAB`) |
| Chip-erase key echo mismatch, or KEY_STATUS bit 3 clear | `RSP_NO_TARGET_POWER` (`0xAB`), reset not pulsed |
| Chip-erase LOCKSTATUS still set after `kResetWaitMs` | `RSP_NO_TARGET_POWER` (`0xAB`), progmode flag cleared |
| NVM status bit 0 or 1 still set after 20 ms | `RSP_NO_TARGET_POWER` (`0xAB`) |
| GO or sign-off before NVM programming | `RSP_OK`, UPDI pin left enabled |
| GO or sign-off while the progmode flag is set | Control B `0x04`, pin ignores UPDI until the chip resets. Chip erase clears the flag after both reset stores, so that close does not store it |
| 60 s with no protocol byte | Session ends. The claimed CS pin returns to an input with a pull-up. ISP also releases SPI |
| Link seen, then absent for 1 s | Session ends. The claimed CS pin returns to an input with a pull-up. ISP also releases SPI |
