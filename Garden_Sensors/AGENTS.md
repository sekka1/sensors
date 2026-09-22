# Agent Directives & Architecture Rules

## 1. Project Architecture
This project uses PlatformIO build-time environments to manage a single C/C++ codebase across multiple sensor hardware profiles without runtime overhead.

- No multi-codebase splitting: all code must exist in this repository.
- Build flags over runtime flags: hardware capabilities are toggled exclusively at compile time using flags defined in platformio.ini.

## 2. Coding Guidelines
- Preprocessor isolation: all hardware drivers, library includes, globals, setup routines, and loop routines must be guarded by conditional preprocessor directives.

```cpp
#if ENABLE_SHT31
  #include <Adafruit_SHT31.h>
  // Driver logic
#endif
```

- Modular hardware decoupling: keep raw sensor I/O isolated from core business and telemetry logic. Feed raw values into pure, hardware-agnostic functions for local host testing.
- Zero dead code: do not instantiate drivers or allocate resources for sensors that are not enabled in the active environment.

## 3. Adding New Sensor Hardware
When adding a new sensor module or node configuration:

- Create a new [env:node_<type>] section in platformio.ini.
- Define explicit build flags using -D ENABLE_<SENSOR_NAME>=1.
- Add required libraries under lib_deps in that target environment.
- Add guarded initialization and read routines in src/main.cpp.

### New Profile Recipe (Copy/Paste)
Use this exact pattern to add a new sensor profile:

```ini
[env:node_<type>]
extends = env:esp32_base
build_flags =
  ${env:esp32_base.build_flags}
  -D ENABLE_<SENSOR_NAME>=1
  -D SENSOR_TYPE_NAME=\"<TYPE_NAME>\"
lib_deps =
  <library-owner>/<library-name> @ ^<version>
```

If a profile combines multiple sensors, add multiple ENABLE_ flags in the same environment.

### Required Touch Points Checklist
When introducing a new sensor capability, update all applicable files:

1. platformio.ini
2. Add or update one or more [env:node_*] targets with flags and sensor-specific lib_deps.
3. src/main.cpp
4. Guard includes, globals, setup, and read logic with #if ENABLE_<SENSOR_NAME>.
5. include/sensor_logic.h (if needed)
6. Add pure helper logic for host-testable transformations.
7. test/test_desktop/test_logic.cpp
8. Add or extend native tests for new pure logic.
9. test/test_device/test_hardware.cpp (optional integration checks)
10. Add simple hardware sanity/integration checks when useful.

## 4. Verification and Testing Requirements
Before completing any task or pull request, run all checks:

- Compile all embedded target environments:

```sh
~/.platformio/penv/bin/platformio run -e node_sht31_soil -e node_soil -e node_sht31 -e node_combo
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
   - Format requirement for pin flags: `-D <SENSOR_NAME>_PIN=<GPIO_NUM>`
   - Example:
     ```ini
     [env:node_temperature]
     build_flags =
         -D ENABLE_DS18B20=1
         -D DS18B20_PIN=4

     [env:node_soil]
     build_flags =
         -D ENABLE_SOIL_MOISTURE=1
         -D SOIL_MOISTURE_PIN=2
     ```

3. **Fallback Pin Header (`src/pins.h`):**
   - Every sensor pin referenced in the code must reside behind an `#ifndef` guard in `src/pins.h` to provide default assignments if a specific environment omits explicit pin flags.
   - Example implementation (`src/pins.h`):
     ```cpp
     #ifndef PINS_H
     #define PINS_H

     #ifndef DS18B20_PIN
       #define DS18B20_PIN 4  // Default fallback GPIO
     #endif

     #ifndef SOIL_MOISTURE_PIN
       #define SOIL_MOISTURE_PIN 2  // Default fallback GPIO
     #endif

     #endif // PINS_H
     ```

4. **Source Code Implementation:**
   - Include `"pins.h"` before instantiating hardware drivers or setting up GPIO pins in `src/main.cpp`.
   - Initialize drivers dynamically using the defined pin constant:
     ```cpp
     #include "pins.h"

     #ifdef ENABLE_DS18B20
       OneWire oneWire(DS18B20_PIN);
       DallasTemperature tempSensor(&oneWire);
     #endif
     ```

### Pin Override Matrix (Current Profiles)
Use this table to verify required pin flags per environment in platformio.ini.

| Environment | ENABLE Flags | Required Pin Flags |
| --- | --- | --- |
| `env:node_sht31_soil` | `ENABLE_SHT31`, `ENABLE_SOIL_MOISTURE` | `LED_PIN`, `TEMP_SENSOR_SDA_PIN`, `TEMP_SENSOR_SCL_PIN`, `SOIL_MOISTURE_POWER_PIN`, `SOIL_MOISTURE_PIN` |
| `env:node_soil` | `ENABLE_SOIL_MOISTURE` | `LED_PIN`, `SOIL_MOISTURE_POWER_PIN`, `SOIL_MOISTURE_PIN` |
| `env:node_sht31` | `ENABLE_SHT31` | `LED_PIN`, `TEMP_SENSOR_SDA_PIN`, `TEMP_SENSOR_SCL_PIN` |
| `env:node_combo` | `ENABLE_SHT31`, `ENABLE_SOIL_MOISTURE` | `LED_PIN`, `TEMP_SENSOR_SDA_PIN`, `TEMP_SENSOR_SCL_PIN`, `SOIL_MOISTURE_POWER_PIN`, `SOIL_MOISTURE_PIN` |

Matrix update rule:
- If a profile enables a sensor, define all pins used by that sensor in the profile's build_flags.
- If a profile disables a sensor, do not require that sensor's pin flags.
