# Agent Directives & Architecture Rules

## 1. Project Architecture
This project uses PlatformIO build-time environments to manage a single C/C++ codebase across multiple sensor hardware profiles without runtime overhead.

- No multi-codebase splitting: all code must exist in this repository.
- Build flags over runtime flags: hardware capabilities are toggled exclusively at compile time using flags defined in platformio.ini.

### Naming Propagation Rule
If a sensor or feature is named in `platformio.ini`, that exact name must propagate into:

- Compile-time feature naming in `src/main.cpp`.
- JSON output field naming prefixes.
- Documentation examples and matrices in this file.

Current required names:

- `ENABLE_SOIL_MOISTURE_PROBE_PRONE`
- `temp_sensor_TO-92`

### Profile Composition Model
PlatformIO configuration is split into reusable layers that are composed in each firmware profile:

- `env:esp32_base`: board/framework/upload defaults and secret flags.
- `flags:device_*`: per-device identity values (for example `DEVICE_ID`).
- `flags:sensor_*`: sensor enable flags, output naming flags, and sensor-specific pin mappings.
- `flags:board_common`: board-wide pins shared by profiles on the same hardware.
- `env:node_*`: deployable profile that composes one device block plus one or more sensor blocks.

This keeps one codebase in `src/main.cpp` while allowing different firmware variants via flag composition.

## 2. Coding Guidelines
- Preprocessor isolation: all hardware drivers, library includes, globals, setup routines, and loop routines must be guarded by conditional preprocessor directives.

```cpp
#if ENABLE_TEMP_SENSOR_TO_92
  #include <Adafruit_SHT31.h>
  // Driver logic
#endif
```

- Modular hardware decoupling: keep raw sensor I/O isolated from core business and telemetry logic. Feed raw values into pure, hardware-agnostic functions for local host testing.
- Zero dead code: do not instantiate drivers or allocate resources for sensors that are not enabled in the active environment.

## 3. Adding New Sensor Hardware
When adding a new sensor module or node configuration:

- Add or update a reusable `flags:sensor_<name>` section in platformio.ini.
- Compose the sensor block into one or more `env:node_<type>` profiles.
- Add required libraries under `lib_deps` only for profiles that enable that sensor.
- Add guarded initialization and read routines in src/main.cpp.

### New Profile Recipe (Copy/Paste)
Use this exact pattern to add a new node profile by composing reusable flag blocks:

```ini
[env:node_<type>]
extends = env:esp32_base
build_flags =
    ${env:esp32_base.build_flags}
    ${flags:device_<device_name>.build_flags}
    ${flags:board_common.build_flags}
    ${flags:sensor_<sensor_a>.build_flags}
    ${flags:sensor_<sensor_b>.build_flags}
    -D SENSOR_TYPE_NAME=\"<TYPE_NAME>\"
lib_deps =
    <library-owner>/<library-name> @ ^<version>
```

Only include a sensor library in `lib_deps` when that profile enables the sensor.

### New Device Recipe (Copy/Paste)
Use this exact pattern when adding another physical node identity:

```ini
[flags:device_<device_name>]
build_flags =
    -D DEVICE_ID=\"<device-id-string>\"
  -D SENSOR_SEND_INTERVAL_MS=<interval-ms>
```

Then reference it from one or more `env:node_*` profiles via:

```ini
${flags:device_<device_name>.build_flags}
```

### Output Naming Convention Rule (Required)
For every enabled sensor feature, if an output name is declared in `platformio.ini`, that exact name must be used as the JSON key prefix in `src/main.cpp`.

Current required output names:

- `ENABLE_SOIL_MOISTURE_PROBE_PRONE`
- `temp_sensor_TO-92`

Do not shorten, lowercase, alias, or partially rename these in JSON keys.

### Required Touch Points Checklist
When introducing a new sensor capability, update all applicable files:

1. platformio.ini
2. Add or update `flags:device_*`, `flags:sensor_*`, and `flags:board_common` sections as needed.
3. Add or update one or more [env:node_*] targets that compose those flag sections.
4. src/main.cpp
5. Guard includes, globals, setup, and read logic with #if ENABLE_<SENSOR_NAME>.
6. include/sensor_logic.h (if needed)
7. Add pure helper logic for host-testable transformations.
8. test/test_desktop/test_logic.cpp
9. Add or extend native tests for new pure logic.
10. test/test_device/test_hardware.cpp (optional integration checks)
11. Add simple hardware sanity/integration checks when useful.

## 4. Verification and Testing Requirements
Before completing any task or pull request, run all checks:

- Compile all embedded target environments:

```sh
~/.platformio/penv/bin/platformio run -e node_sht31_soil -e node_soil_probe_prone -e node_sht31 -e node_combo
```

- Run desktop unit tests:

```sh
~/.platformio/penv/bin/platformio test -e native
```

- Validate code isolation: disabling sensor flags must exclude related drivers cleanly without compile errors.

## Hardware Pin Abstraction & Configuration Rules

1. **Strict Exclusion of Hardcoded GPIO Pins:**
   - Physical GPIO pin assignments MUST NEVER be hardcoded directly inside implementation files (`.cpp`, `.c`, or `.h`).
   - Hardcoded integer literals for pin assignments in driver initializations are strictly prohibited.

2. **Pin Configuration via `platformio.ini`:**
   - All pin mapping assignments must be declared as compile-time build flags within the corresponding environment definition in `platformio.ini`.
   - Format requirement for pin flags: `-D <SENSOR_NAME>_<SIGNAL>_PIN=<GPIO_NUM>` (for example `_AO_PIN`, `_POWER_PIN`, `_SDA_PIN`, `_SCL_PIN`).
   - Example:
     ```ini
     [flags:sensor_soil_moisture_probe_prone]
     build_flags =
         -D ENABLE_SOIL_MOISTURE_PROBE_PRONE=1
         -D SOIL_MOISTURE_PROBE_1_POWER_PIN=21
         -D SOIL_MOISTURE_PROBE_1_AO_PIN=0
         -D SOIL_MOISTURE_PROBE_2_POWER_PIN=20
         -D SOIL_MOISTURE_PROBE_2_AO_PIN=1
     ```

3. **Fallback Pin Header (`src/pins.h`):**
   - Every sensor pin referenced in the code must reside behind an `#ifndef` guard in `src/pins.h` to provide default assignments if a specific environment omits explicit pin flags.
   - Example implementation (`src/pins.h`):
     ```cpp
     #ifndef PINS_H
     #define PINS_H

     #ifndef SOIL_MOISTURE_PROBE_1_POWER_PIN
       #define SOIL_MOISTURE_PROBE_1_POWER_PIN 21  // Default fallback GPIO
     #endif

     #ifndef SOIL_MOISTURE_PROBE_1_AO_PIN
       #define SOIL_MOISTURE_PROBE_1_AO_PIN 0  // Default fallback GPIO
     #endif

     #ifndef SOIL_MOISTURE_PROBE_2_POWER_PIN
       #define SOIL_MOISTURE_PROBE_2_POWER_PIN 20  // Default fallback GPIO
     #endif

     #ifndef SOIL_MOISTURE_PROBE_2_AO_PIN
       #define SOIL_MOISTURE_PROBE_2_AO_PIN 1  // Default fallback GPIO
     #endif

     #endif // PINS_H
     ```

4. **Source Code Implementation:**
   - Include `"pins.h"` before instantiating hardware drivers or setting up GPIO pins in `src/main.cpp`.
   - Initialize drivers dynamically using the defined pin constant:
     ```cpp
     #include "pins.h"

     #ifdef ENABLE_SOIL_MOISTURE_PROBE_PRONE
       pinMode(SOIL_MOISTURE_PROBE_1_POWER_PIN, OUTPUT);
       pinMode(SOIL_MOISTURE_PROBE_2_POWER_PIN, OUTPUT);
     #endif
     ```

### Pin Override Matrix (Current Profiles)
Use this table to verify required pin flags per environment in platformio.ini.

| Environment | ENABLE Flags | Required Pin Flags |
| --- | --- | --- |
| `env:node_sht31_soil` | `ENABLE_TEMP_SENSOR_TO_92`, `ENABLE_SOIL_MOISTURE_PROBE_PRONE` | `LED_PIN`, `TEMP_SENSOR_TO_92_SDA_PIN`, `TEMP_SENSOR_TO_92_SCL_PIN`, `SOIL_MOISTURE_PROBE_1_POWER_PIN`, `SOIL_MOISTURE_PROBE_1_AO_PIN`, `SOIL_MOISTURE_PROBE_2_POWER_PIN`, `SOIL_MOISTURE_PROBE_2_AO_PIN` |
| `env:node_soil_probe_prone` | `ENABLE_SOIL_MOISTURE_PROBE_PRONE` | `LED_PIN`, `SOIL_MOISTURE_PROBE_1_POWER_PIN`, `SOIL_MOISTURE_PROBE_1_AO_PIN`, `SOIL_MOISTURE_PROBE_2_POWER_PIN`, `SOIL_MOISTURE_PROBE_2_AO_PIN` |
| `env:node_sht31` | `ENABLE_TEMP_SENSOR_TO_92` | `LED_PIN`, `TEMP_SENSOR_TO_92_SDA_PIN`, `TEMP_SENSOR_TO_92_SCL_PIN` |
| `env:node_combo` | `ENABLE_TEMP_SENSOR_TO_92`, `ENABLE_SOIL_MOISTURE_PROBE_PRONE` | `LED_PIN`, `TEMP_SENSOR_TO_92_SDA_PIN`, `TEMP_SENSOR_TO_92_SCL_PIN`, `SOIL_MOISTURE_PROBE_1_POWER_PIN`, `SOIL_MOISTURE_PROBE_1_AO_PIN`, `SOIL_MOISTURE_PROBE_2_POWER_PIN`, `SOIL_MOISTURE_PROBE_2_AO_PIN` |

### Device Matrix (Current Profiles)
Use this table to verify `DEVICE_ID` source per environment.

| Environment | Device Flag Section | Required Device Build Flag |
| --- | --- | --- |
| `env:node_sht31_soil` | `flags:device_esp32_c3_garden_02` | `DEVICE_ID=\"esp32-c3-garden-02\"` |
| `env:node_soil_probe_prone` | `flags:device_esp32_c3_garden_02` | `DEVICE_ID=\"esp32-c3-garden-02\"` |
| `env:node_sht31` | `flags:device_esp32_c3_garden_02` | `DEVICE_ID=\"esp32-c3-garden-02\"` |
| `env:node_combo` | `flags:device_esp32_c3_garden_02` | `DEVICE_ID=\"esp32-c3-garden-02\"` |

Matrix update rule:
- If a profile enables a sensor, define all pins used by that sensor in the profile's build_flags.
- If a profile disables a sensor, do not require that sensor's pin flags.
