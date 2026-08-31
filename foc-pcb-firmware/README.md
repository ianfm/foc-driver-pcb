# FOC Driver Firmware (ESP32-S3-WROOM-1 N16R8)

Consolidated Field-Oriented Control (FOC) firmware for BLDC motor drive using the **ESP32-S3-WROOM-1 N16R8**, **TI DRV8323S** 3-phase gate driver, and **AS5600** 12-bit magnetic angle sensor.

---

## Build System & Toolchain Specification (Host: Enma / Windows 11)

This firmware was built, verified, and flashed on **Windows 11 (amd64)** on host machine **Enma**.

### Exact Toolchain Components
- **Host OS**: Microsoft Windows 11 (amd64)
- **PlatformIO Core**: `6.1.19`
- **Platform**: `espressif32` (`55.3.311` / `pioarduino` registry)
- **Framework**: `framework-espidf @ 3.50505.0` (ESP-IDF `v5.5.5`)
- **Compiler**: `toolchain-xtensa-esp-elf @ 14.2.0+20260121` (`xtensa-esp32s3-elf-gcc 14.2.0`)
- **Debugger**: `tool-xtensa-esp-elf-gdb @ 17.1.0+20260402`
- **Flasher**: `tool-esptoolpy @ 5.3.0` (`esptool v5.3.0`)
- **Build Engine**: `tool-cmake @ 4.0.3`, `tool-ninja @ 1.13.1`, `tool-scons @ 4.40801.0`
- **Python Environment**: `Python 3.12.10`

---

## Target Hardware Specifications

- **Microcontroller**: ESP32-S3 (Xtensa Dual-Core LX7 @ 240 MHz)
- **Flash**: 16 MB Quad SPI (QIO @ 80 MHz)
- **PSRAM**: 8 MB Octal SPI (OPI @ 80 MHz)
- **Gate Driver**: Texas Instruments DRV8323S (SPI, 3x PWM Mode)
- **Angle Sensor**: AMS AS5600 (I2C @ 400 kHz Fast-Mode)

---

## Pinout Mapping

| Function | Firmware Macro | ESP32-S3 GPIO | Description |
| :--- | :--- | :--- | :--- |
| **SPI CS** | `PIN_SPI_CS` | **GPIO 10** | DRV8323 `nSCS` (Active LOW) |
| **SPI SCLK** | `PIN_SPI_CLK` | **GPIO 11** | DRV8323 `SCLK` (1 MHz Mode 1) |
| **SPI MOSI** | `PIN_SPI_MOSI` | **GPIO 12** | DRV8323 `SDI` |
| **SPI MISO** | `PIN_SPI_MISO` | **GPIO 13** | DRV8323 `SDO` |
| **PWM High A** | `PIN_INHA` | **GPIO 15** | Phase A High-Side MCPWM (25 kHz center-aligned) |
| **PWM High B** | `PIN_INHB` | **GPIO 16** | Phase B High-Side MCPWM (25 kHz center-aligned) |
| **PWM High C** | `PIN_INHC` | **GPIO 17** | Phase C High-Side MCPWM (25 kHz center-aligned) |
| **PWM Low A** | `PIN_INLA` | **GPIO 18** | Phase A Low-Side Enable (Held HIGH for 3x mode) |
| **PWM Low B** | `PIN_INLB` | **GPIO 21** | Phase B Low-Side Enable (Held HIGH for 3x mode) |
| **PWM Low C** | `PIN_INLC` | **GPIO 47** | Phase C Low-Side Enable (Held HIGH for 3x mode) |
| **DRV Enable** | `PIN_DRV_EN` | **GPIO 8** | DRV8323 `ENABLE` (Active HIGH) |
| **DRV Fault** | `PIN_DRV_FAULT` | **GPIO 9** | DRV8323 `nFAULT` (Active LOW, internal pull-up) |
| **Current Sense A** | `PIN_SOA_GPIO` | **GPIO 4** | `ADC1_CHANNEL_3` Continuous DMA |
| **Current Sense B** | `PIN_SOB_GPIO` | **GPIO 5** | `ADC1_CHANNEL_4` Continuous DMA |
| **Current Sense C** | `PIN_SOC_GPIO` | **GPIO 6** | `ADC1_CHANNEL_5` Continuous DMA |
| **Encoder SDA** | `PIN_I2C_SDA` | **GPIO 1** | AS5600 I2C Data |
| **Encoder SCL** | `PIN_I2C_SCL` | **GPIO 2** | AS5600 I2C Clock |

*Note: GPIO 33–37 are reserved for internal Octal PSRAM. GPIO 19/20 are reserved for native USB/JTAG.*

---

## Build & Flash Commands

### Build
```bash
pio run -d foc-pcb-firmware
```

### Upload
```bash
pio run -d foc-pcb-firmware -t upload
```

### Monitor Serial Output
```bash
pio device monitor -d foc-pcb-firmware -b 115200
```
