# File: memory-bank/03-development-process.md
# ----
# Development Process and Workflow

## Overview

This project is a fork (MoonModules/WLED) of the main WLED project, focusing on adding experimental features, particularly audio reactivity and custom 2D effects, based on the WLED 0.14 codebase.

## Version Control

-   **Git:** The project uses Git for version control, hosted on GitHub.
-   **Branching:** The primary development branch appears to be `mdev`. Changes are likely developed in feature branches and merged into `mdev`. Releases might be tagged or branched off `mdev`.

## Build System

-   **PlatformIO:** The project is configured for building with PlatformIO (`platformio.ini`).
-   **Environments:** Multiple PlatformIO environments are defined, targeting different hardware (ESP8266 variants, ESP32 variants including S2/S3/C3, Ethernet boards, boards with PSRAM) and feature sets (e.g., `_S` for speed-optimized basic features, `_M` for medium features, `_XL` for extended features, `_V4` for IDF 4.4 builds, `_HUB75` for matrix panels).
-   **Customization:**
    -   `platformio_override.ini`: Users can create this file locally to select specific environments or add custom build flags without modifying the main `platformio.ini`.
    -   `wled00/my_config.h`: Users can create this file (or copy `my_config_sample.h`) to override certain `#define` constants at compile time (requires `-DWLED_USE_MY_CONFIG` build flag, which is present in most MM environments).

## Build Process

1.  **Prerequisites:** PlatformIO Core installed.
2.  **Configuration:** Select the desired environment(s) in `platformio.ini` or `platformio_override.ini`. Optionally create/modify `my_config.h`.
3.  **Build:** Run `platformio run -e <environment_name>` or `platformio run` (for default environments).
4.  **Scripts:** Several PlatformIO scripts automate parts of the build:
    *   `set_version.py`: Updates version information.
    *   `build-html.py`: Compresses and converts web UI files (`wled00/data/`) into C++ header files (`html_*.h`). Uses `node`, `html-minifier-terser`, `clean-css`, `zlib`.
    *   `output_bins.py`: Copies compiled firmware binaries to a `build_output` directory.
    *   `strip-floats.py`: (Potentially) Removes floating-point support if not needed (usage seems conditional/optional).
    *   `user_config_copy.py`: Copies `my_config_sample.h` to `my_config.h` if it doesn't exist.

## Deployment

-   **OTA Updates:** WLED supports Over-The-Air firmware updates via the web UI (`/update`) or ArduinoOTA (if enabled). OTA updates require sufficient flash space (usually requiring a partition scheme with at least two app partitions). OTA can be locked for security.
-   **Serial Flashing:** Firmware can be flashed directly using PlatformIO (`platformio run -t upload`) or other ESP flashing tools (e.g., esptool.py). A full flash erase might be necessary when changing partition schemes or upgrading between major framework versions (e.g., IDF 3.x to 4.x).
-   **Filesystem:** Configuration (`cfg.json`, `wsec.json`), presets (`presets.json`), and potentially usermod files or ledmaps (`ledmap.json`, `palette*.json`) are stored on the device's filesystem (LittleFS or SPIFFS). These can be managed via the `/edit` page (if enabled) or file uploads.

## Testing

-   No automated testing framework is apparent in the provided codebase.
-   Testing likely relies on manual flashing and observation on target hardware.
-   Debugging is primarily done via Serial output (`Serial.print*`) or network UDP debugging (`NetDebug`). `#define WLED_DEBUG` enables more verbose output. ESP32 builds can use `monitor_filters = esp32_exception_decoder` in PlatformIO for crash analysis.