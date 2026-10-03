# Programming daughter modules

The controller can act as an Arduino-as-ISP programmer for an ATmega328PB
module. The sensor and solenoid modules have a separate 2×3 ISP header
(MISO, VCC, SCK, MOSI, RESET, GND). That header is not the slot connector,
so the module under test sits off the motherboard and is jumpered from an
empty firmware slot 1.

`program updi` is reserved for a later programmer. The pump module is an
ATtiny1614 and is not programmed by this firmware.

## Pins

Jumper firmware slot 1 to the module ISP header:

| ISP signal | Controller net |
| --- | --- |
| MOSI | GPIO11 (MOSI_R on the slot header) |
| MISO | GPIO13 (MISO_R) |
| SCK | GPIO12 (SCK_R) |
| RESET | Slot 1 CS, GPIO6 |
| VCC | Slot 3.3 V |
| GND | Slot GND |

The slot header is the schematic connector, not a silkscreen “slot 1”.
Physical left-to-right on the board is schematic slot 4 toward slot 1, so
this plug is the right-hand header. SPI to every slot is the same bus, with
series resistors on the `_R` nets. Program with the other modules removed.
The ISP clock is 125 kHz, which is slow enough for those resistors.

Power is 3.3 V only. The sensor runs from its internal 8 MHz oscillator, so
the ISP clock must stay at or below 2 MHz. This firmware always clocks SPI
at 125 kHz. avrdude’s `-B5` is accepted and does not change that clock.

## Commands

While the USB link is plugged in and no programming session is active:

```text
program
program isp
```

Both reply:

```text
OK programming
ISP slot 1: MOSI GPIO11, MISO GPIO13, SCK GPIO12, RESET GPIO6, 3V3, GND
```

Any other `program ...` line, including `program updi`, replies
`ERR program` and does not start a session.

The sensor project’s Upload_ISP environment already uses
`upload_protocol = stk500v1`, `upload_speed = 19200`, and `-B5`. Point
`upload_port` at this controller’s COM port. The baud rate is not a UART
on this USB port; the port is the ESP32 USB serial link.

## Session

`program` prints those two lines, stores an RTC marker, and gives the USB
byte stream to STK500. `Programming::begin` quiesces the module bus. While
the session is active, `loop()` updates Wi-Fi, NTP, and MQTT, then the
programmer, and returns. It does not read the console, publish slot status,
update the sensor, solenoid, or pump modules, or print the heap lines.
Desired-state messages can still arrive, and they are applied only when
those modules run again after the session.

Absence timers keep counting during the session. A session longer than
`kSolenoidCommandTimeoutMs` (15 minutes) or `kPumpCommandTimeoutMs`
(3 minutes) makes the next module update turn those outputs off. A shorter
session leaves outputs as they were.

The session ends 60 seconds after the last STK500 byte, or 60 seconds after
it started when none arrive (`kProgrammingIdleTimeoutMs` in `src/main.cpp`).

After the USB link has been present in this session, it also ends once that
link has stayed absent for 1 second (`kProgrammingUnplugTimeoutMs`). A session
that has not seen the link yet stays up until the silence timer. Closing or
opening the serial monitor can reset the chip and stop USB frames while the
port enumerates again. The session and the RTC marker survive that gap. Setup
on the restarted chip starts STK500 with no banner.

When the session ends, slot 1 CS returns to an input with pull-up, SPI is
released, the RTC marker is cleared, and the normal loop resumes.

The marker is one word in the `.rtc_noinit` part of RTC slow memory.
That section is not reloaded from the firmware image, so a USB-open
reset or a software restart keeps it. The reset button pulls `CHIP_PU`
low, and a power cycle removes RTC power, so both clear it.

## USB-open restart

avrdude opens the port with DTR/RTS. On this board that is the USB Serial/JTAG
controller, so the ESP32 may restart, or the ROM download mode may take the
port. The firmware does not add a second USB stack. If the application
restarts, setup sees the RTC marker, skips the two-second banner delay, and
starts STK500 before Wi-Fi, NVS, and the banner. That boot prints nothing.
The `program` command has already printed `OK programming` before any reset.

If the ROM download mode captures the port, avrdude will not get a sync
answer. That has to be checked on the board. Desktop tests do not cover it.

## ISP sequence

SPI starts first, mode 0, MSB first, SCK idle low. Reset (slot 1 CS) is then
driven low. After 20 ms the programmer sends `AC 53 00 00`. Success is the
third response byte equal to `0x53`. It tries three times. Between tries it
pulses reset high, then low, and waits 20 ms again. After three failures it
replies STK500 failed and drives reset high as an output.

A second enter, while already programming, replies OK and does not pulse
reset. Leave programming ends SPI and drives reset high. The input pull-up
is restored when the session ends, not on leave. `ModuleHost::begin()`
configures CS only once, so the session restores the pin itself.

Page writes wait 5 ms. Each EEPROM byte waits 10 ms. Chip erase waits 10 ms,
including a universal `AC 80 00 00`. The signature is read from the chip
(an ATmega328PB answers `1E 95 16`). Fuse writes are passed through.
A wrong clock fuse can stop ISP until a high-voltage programmer is used.

The spoken protocol is the STK500v1 subset avrdude uses: sync, sign-on
`AVR ISP`, parameters, device setup, programming enable, universal, paged
flash and EEPROM read/write, signature, and chip erase. Unknown commands
answer `STK_UNKNOWN` when the next byte is the end marker.

## Later hardware

A later board can add a 2×3 header wired the same way, plus one spare GPIO
for UPDI. GPIO9, GPIO10, GPIO14, GPIO17, and GPIO18 are free on this MCU and
are not routed for that. This firmware does not drive them.

## Tests

Native tests in `test/test_desktop/test_isp.cpp` cover the STK500 subset,
the reset-before-SPI order, the fixed 125 kHz clock, and the session idle
path. They also cover an unplug before the port has been seen, a sub-second
unplug that leaves STK500 running, and a full one-second unplug. A second
`begin()` after that confirmed unplug starts a new seen-port count.
`test_modules.cpp` covers host quiesce. `test_configuration.cpp`
covers `program`, `program isp`, and `program updi`. The `.rtc_noinit`
marker is ESP32-only and is not part of the desktop tests.

```text
pio test -e native
```

The first check on the board, after those tests and a `custom-esp32` build,
is that avrdude syncs after the USB-open restart and a 328PB returns its
signature. This change does not flash the controller.
