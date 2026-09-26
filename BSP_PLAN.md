# BSP development plan for ESP32-S3 Touch LCD 3.5B

## 1. Objective
Build a clean board support package for the Waveshare ESP32-S3 Touch LCD 3.5 board using the current ESP-IDF v6.1 environment and upstream open-source libraries instead of a custom BSP layer.

The goal is to bring up the board one subsystem at a time, validate each block, and then integrate them into a stable application framework.

## 2. Working assumptions
This project is using:
- ESP32-S3 target
- ESP-IDF v6.1
- CMake-based project structure
- LVGL and ESP-IDF official drivers where available

The temporary reference repo cloned for inspection is:
- .old_version

That repo is useful as a hardware stack reference only. The final BSP should be based on the actual board schematic and current ESP-IDF APIs, not on outdated vendor code.

## 3. Board stack to confirm from schematic
From the reference examples, the board appears to include a typical ESP32-S3 display module stack:
- ESP32-S3 SoC
- TFT display panel with backlight
- touch controller over I2C
- PMIC / power management IC
- IMU sensor (QMI8658 is present in the vendor examples)
- optional audio, camera, or storage features depending on board revision

Important: exact GPIO numbers, panel controller, touch controller model, and PMIC register map must be confirmed from the board schematic before finalizing the BSP.

## 4. Architecture approach
Use a layered BSP structure:
- board config and pin map
- I2C / SPI bus layer
- display driver
- touch driver
- PMIC / power control
- sensor layer
- LVGL integration layer

Prefer:
- ESP-IDF official drivers: esp_driver_i2c, esp_driver_spi, esp_lcd, esp_lvgl_port
- upstream open-source libraries with active maintenance
- vendor examples only as a reference for register sequences and power sequencing

Avoid:
- custom monolithic driver logic for each peripheral
- copying stale vendor BSPs into the project without adapting them to current APIs

## 5. Priority order
### Priority 0 — Project baseline and debug
1. Create a clean ESP-IDF project structure for the board target
2. Confirm target: ESP32-S3
3. Set up serial console and USB CDC/JTAG workflow
4. Add board-specific logging and a simple heartbeat app
5. Verify flash + monitor path works on the connected board

Goal: ensure the board can be programmed and debugged reliably before any peripheral work.

### Priority 1 — Power and basic GPIO bring-up
1. Bring up the PMIC / power rail control
2. Confirm 3.3 V and backlight rails are stable
3. Map all board GPIOs from schematic
4. Confirm reset, enable, interrupt, and power pins
5. Add default GPIO configuration for all peripherals

Goal: create a safe power-on baseline for all connected chips.

### Priority 2 — Display bring-up
1. Identify display controller and panel resolution
2. Configure SPI or RGB interface correctly
3. Initialize backlight and display reset sequence
4. Test frame buffer rendering and color output
5. Integrate LVGL display port with the panel driver

Goal: display must be stable before touch and sensor work.

### Priority 3 — Touch controller bring-up
1. Identify touch IC model from schematic or board layout
2. Configure I2C bus and interrupt line
3. Validate read/write communication
4. Map raw touch coordinates to screen coordinates
5. Integrate with LVGL input driver

Goal: user input must work reliably and be calibrated.

### Priority 4 — IMU and sensors
1. Bring up the I2C bus with the IMU sensor
2. Validate chip ID and reset sequence
3. Configure sensor mode and sample rate
4. Add readings for accelerometer and gyroscope
5. Expose data through a clean sensor API

Goal: sensor data is available for apps without destabilizing the main system.

### Priority 5 — Audio, camera, storage, and optional hardware
1. Validate audio codec or I2S path if present
2. Validate camera or SD card path only if the board revision includes it
3. Keep the implementation isolated behind capability checks
4. Document unsupported or optional blocks explicitly

Goal: add optional hardware without blocking core BSP functionality.

### Priority 6 — Application integration
1. Integrate display, touch, and sensor stack into one BSP API
2. Verify boot sequence and power-on behavior
3. Add board-level helper APIs for brightness, reset, sleep, and status
4. Validate long-running stability and interrupt behavior

Goal: produce a usable BSP that other applications can call without duplicating hardware logic.

## 6. Recommended BSP component structure
Create a component structure like this:

- components/bsp_board
  - Kconfig.projbuild
  - CMakeLists.txt
  - include/bsp_board.h
  - src/bsp_board.cpp

- components/bsp_display
  - include/bsp_display.h
  - src/bsp_display.cpp

- components/bsp_touch
  - include/bsp_touch.h
  - src/bsp_touch.cpp

- components/bsp_pmic
  - include/bsp_pmic.h
  - src/bsp_pmic.cpp

- components/bsp_imu
  - include/bsp_imu.h
  - src/bsp_imu.cpp

- components/bsp_i2c
  - include/bsp_i2c.h
  - src/bsp_i2c.cpp

Each component should expose:
- init function
- deinit function
- status/error return values
- clear board-level abstraction from application code

## 7. Execution sequence for the user
Execute one phase at a time in this order:

1. Baseline board debug and flashing
2. PMIC and rail validation
3. Display controller + backlight
4. Touch controller
5. IMU sensor
6. Optional features
7. Final BSP API integration

The code should not move to the next stage until the current stage is validated with a real hardware test.

## 8. Practical implementation rules
- Keep each driver in a separate component or module
- Use the official ESP-IDF driver APIs wherever possible
- Prefer upstream open-source libraries over a home-grown custom layer
- Do not bring in unmaintained vendor BSP code directly
- Add hardware validation logs for every init path
- Maintain a board pin map file as a single source of truth

## 9. Verification checklist for each phase
For every subsystem, verify:
- communication succeeds over the expected bus
- reset and enable pins are correct
- interrupt lines are configured correctly
- timing and power sequencing match the datasheet
- real values appear on the monitor and are stable over time
- no warnings or errors appear during boot

## 10. Recommended debug commands
Use the board with the current ESP-IDF environment:

- idf.py build
- idf.py -p /dev/ttyUSB0 flash
- idf.py -p /dev/ttyUSB0 monitor
- idf.py monitor

The exact serial port depends on the host machine, so it should be selected from the available /dev/tty* devices before flashing.

## 11. Recommended next step
The first execution item should be:
- hardware validation and board GPIO map
- PMIC bring-up
- baseline serial monitor working

Once that is complete, the next phase is the display driver, then touch input, then IMU.
