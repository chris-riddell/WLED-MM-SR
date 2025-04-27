# File: memory-bank/02-components.md
# ----
# Key Components and Modules

## Core WLED

-   **`wled.h`:** Global variable declarations, core constants, main `WLED` class definition.
-   **`wled.cpp`:** `WLED` class implementation, `setup()` and `loop()` functions, connection management, interface initialization, status LED handling.
-   **`const.h`:** Numerical constants, default values, pin definitions (if not overridden), mode IDs, color order IDs, etc.
-   **`fcn_declare.h`:** Forward declarations for globally used functions across different files.


## LED Control & Effects

-   **`FX.h`:** ...
-   **`FX.cpp`:** ...
-   **`FX_fcn.cpp`:** ...
-   **`FX_2Dfcn.cpp`:** ...
-   **`FX_fcn_*.cpp` (various):** ...
-   **`FX_EXP_fcn_*.cpp` (MoonModules specific):** ***Crucially, this is where the custom audio-reactive effects tailored for the mandala configuration (and other specific setups like plinths/leaves) are implemented. New effects of this nature should follow patterns established in these files.*** Examples include `mode_multi_comet_bass_ar`, `mode_fft_spectrum_ar`, `mode_volume_ripples`, `mode_bass_drop`, `mode_spectral_flux`, `mode_spectral_centroid`, `mode_harmonic_viz`, `mode_note_tracker`, `mode_dynamic_range`, `mode_plinth`, `mode_noisemeter_134`.
-   **`palettes.h`:** ...
-   **`colors.cpp` / `colorTools.hpp`:** ...
-   **`wled_math.cpp`:** ...
-   **`audio_utils.h` / `FX_fcn_audio_helpers.cpp`:** Utility functions specifically for audio-reactive effects (e.g., `detectTonality`, `calculateSpectralCentroid`).


## Hardware Abstraction

-   **`bus_manager.h` / `bus_manager.cpp`:** Defines `BusManager` and base `Bus` class. Manages multiple LED outputs.
-   **`BusDigital`, `BusPwm`, `BusOnOff`, `BusNetwork`, `BusHub75Matrix`:** Specific bus implementations inheriting from `Bus`.
-   **`bus_wrapper.h`:** Wrapper around the NeoPixelBus library, defining specific bus types and providing a unified interface (`PolyBus`) for interacting with them.
-   **`pin_manager.h` / `pin_manager.cpp`:** Manages GPIO pin allocation and prevents conflicts using `PinOwner` tags.

## Configuration & Storage

-   **`cfg.cpp`:** Handles serialization and deserialization of `cfg.json` (main settings) and `wsec.json` (security settings) using ArduinoJson.
-   **`presets.cpp`:** Manages loading, saving, and applying presets from `presets.json`. Handles temporary preset (255).
-   **`playlist.cpp`:** Handles loading and sequencing of playlists defined within presets.
-   **`wled_eeprom.cpp`:** (Legacy) Handles loading settings from EEPROM for older WLED versions (used via `deEEP()` functions). Enabled by `WLED_ADD_EEPROM_SUPPORT`.
-   **`file.cpp`:** Filesystem utilities for reading/writing JSON objects to files.

## Networking & Communication

-   **`wled_server.cpp`:** Sets up the AsyncWebServer, defines routes for the web UI, `/json` API, OTA updates, and other HTTP endpoints.
-   **`html_*.h`:** Contains gzipped HTML, CSS, and JS for the web UI, stored in PROGMEM.
-   **`ws.cpp`:** WebSocket server implementation for real-time communication with the UI.
-   **`mqtt.cpp`:** MQTT client implementation using AsyncMqttClient.
-   **`udp.cpp`:** Handles WLED UDP notifier protocol (syncing), receives real-time UDP data (E1.31, Art-Net, DDP, etc. via `handleE131Packet`), manages the node list.
-   **`e131.cpp`:** Specific handlers for E1.31 (sACN) and Art-Net packets, including DMX mapping logic (`handleDMXData`).
-   **`json.cpp`:** Handles JSON API requests (`/json`) and serialization/deserialization of state and info objects. Includes logic for applying state changes from JSON.
-   **`network.cpp`:** WiFi/Ethernet event handling and signal quality calculation.
-   **`wled_ethernet.h`:** Definitions for supported Ethernet board types.
-   **`alexa.cpp`:** Amazon Echo integration using Espalexa.
-   **`hue.cpp`:** Philips Hue bridge synchronization.
-   **`improv.cpp`:** Improv Serial provisioning protocol implementation.
-   **`net_debug.cpp` / `net_debug.h`:** (Optional) UDP-based network debugging output.

## Timing & Scheduling

-   **`ntp.cpp`:** Network Time Protocol client implementation, time zone handling, sunrise/sunset calculation, timed macro/preset execution (`checkTimers`), countdown logic.
-   **`wled.cpp` (part of `loop()`):** Main timing loop for effects service, transitions, nightlight, playlist handling, and interface updates.

## User Interface & IO

-   **`button.cpp`:** Handles physical button input (push, long press, double press, switch, PIR, touch, analog/potentiometer).
-   **`ir.cpp`:** Handles Infrared remote control input, decoding various remote protocols.
-   **`ir_codes.h`:** Defines specific IR codes for different remotes.
-   **`overlay.cpp`:** Draws clock overlays (now mostly handled by usermods like Cronixie).
-   **`xml.cpp`:** Generates XML responses for legacy `/win` API and JavaScript data for settings pages.

## Usermods & Extensibility

-   **`usermod.cpp`:** Legacy v1 usermod entry points (`userSetup`, `userConnected`, `userLoop`).
-   **`um_manager.h` / `um_manager.cpp`:** `UsermodManager` class to register and manage v2 usermods. Defines the `Usermod` base class interface.
-   **`usermods_list.cpp`:** Central place to register v2 usermod instances.
-   **`usermod_v2_*.h`:** Template/Example for creating v2 usermods.
-   **`usermods/` directory (excluded):** Contains the actual source code for various usermods. `USERMOD_AUDIOREACTIVE` is particularly relevant to this fork.

## Build & Platform

-   **`platformio.ini` / `platformio_override.ini`:** PlatformIO project configuration, defining environments, build flags, library dependencies, and partition schemes.
-   **`my_config.h` (optional):** User-defined compile-time overrides for certain defaults or features.