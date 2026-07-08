# MQTT Modular Controller Board

ESP32-S3-WROOM-1U based motherboard for modular expansion. The board hosts up to four plug-in modules for sensor interfacing, actuator control (pumps, solenoids, etc.), and other I/O tasks. Communication uses I²C (SDA/SCL) and SPI buses, with MQTT support planned for firmware.

## Features

- ESP32-S3-WROOM-1U microcontroller with antenna support
- Four module slots using standardized 2x10 pin headers
- Power rails: 12 V, 5 V, and 3.3 V supplied to modules
- High-side reverse polarity protection on 12 V and 5 V inputs
- USB-powered operation for programming and testing (no 12 V required)
- Screw terminal power input
- 4-layer PCB designed for assembly (routing / GND / GND / routing)
- 4 x 3.5mm Mounting Holes

## Architecture

### Core Components
- **Microcontroller**: ESP32-S3-WROOM-1U
- **Power Input**: 12 V via screw terminals
- **Power Conversion**:
  - 12 V → 5 V using TPS563200DDCR (TI WEBENCH design)
  - 5 V → 3.3 V using LDO
- **Maximum Outputs**:
  - 12 V: Limited by external supply
  - 5 V: 3 A total (0.8 A used internally)
  - 3.3 V: 1 A total (0.6 A used by ESP32)

### Module Interface
Each of the four modules connects via two 10-pin headers and receives:
- Power: 12 V (4 pins shared), 5 V (2 pins), 3.3 V (2 pins), GND (4 pins)
- Communication:
  - Shared SPI: CS(per module), MOSI(GPIO11), MISO(GPIO13), SCK(GPIO12)
  - Shared I²C: SDA(GPIO4), SCL(GPIO5)
- Control/Detection:
  - One general-purpose GPIO per module
  - One module detection/sense pin per module
  - Module-specific CS pins.

#### Module Slots (Left to Right)
Slot 1:
- General Purpose: GPIO1
- Module Detection: GPIO2
- CS Pin: GPIO16
  
Slot 2:
- General Purpose: GPIO43
- Module Detection: GPIO44
- CS Pin: GPIO15
  
Slot 3:
- General Purpose: GPIO42
- Module Detection: GPIO41
- CS Pin: GPIO7
  
Slot 4:
- General Purpose: GPIO40
- Module Detection: GPIO39
- CS Pin: GPIO6
  
### Additional Features
- USB micro-B for programming and power (VBUS sense on GPIO08)
- Power indicator LED on 3.3 V rail
- EN button and RESET functionality
- Manual download mode: Hold BOOT button while pressing RESET, then release BOOT

### PCB Stackup
4-layer board:  
Component/Routing → Ground plane → Ground plane → Routing
Designed for JLCPCB Impedance Control Stackup of JLC04161H-7628, Finished thickness of 1.59mm.

## Setup and Build

**Firmware**: Not yet implemented (TBA).

**Programming**:
- Connect via USB micro-B.
- Use ESP32 flashing tools (esptool, Arduino IDE, ESP-IDF, etc.).
- For manual bootloader mode: Hold BOOT button, press RESET, release BOOT.

**Module Development**:
Modules must conform to the 2x10 header pinout with 40mm pitch.
Board uses 2 x 10, 8.5mm Female Headers (A2541HWV-10P). Recommended 3A support.

## Repository Contents

- KiCad schematic and PCB files
- Project configuration
- Backups folder

## License

This project is open source. See LICENSE file for details (add one if not present).
