# File: memory-bank/01-architecture.md
# ----
# WLED MoonModules Architecture

## Overview

WLED MoonModules builds upon the WLED architecture, employing an event-driven approach centered around a main loop. It integrates various communication protocols, a flexible effects engine, and a modular system for extensions.

## Core Components

1.  **Main Controller (`WLED` class in `wled.cpp`, `wled.h`):**
    *   Handles initialization (`setup()`) and the main execution cycle (`loop()`).
    *   Manages device state, network connections, and coordinates updates across different modules.
    *   Integrates timing (`handleTime()`), I/O (`handleIO()`), network connectivity (`handleConnection()`), and other core tasks.

2.  **Effects Engine (`WS2812FX` class in `FX.h`, `FX.cpp`):**
    *   Adapted from the WS2812FX library.
    *   Manages effects (`mode_*` functions in `FX_fcn*.cpp`), palettes, speed, and intensity.
    *   Handles segment-specific effect rendering.
    *   Includes 1D and 2D effect capabilities.

3.  **Segment Management (`Segment` struct in `FX.h`):**
    *   Allows dividing the LED strip into virtual segments.
    *   Each segment maintains its own state (colors, effect, speed, intensity, palette, options like reverse/mirror).
    *   Segments are stored in a `std::vector` within the `WS2812FX` instance.
    *   Supports complex 2D mapping via `ledmap.json` or panel configurations.

4.  **Bus Management (`BusManager` class, `Bus` base class in `bus_manager.h`, `bus_manager.cpp`):**
    *   Abstracts hardware communication with different LED types (Digital SPI/single-wire, PWM, Network).
    *   Handles pin allocation via `PinManager`.
    *   Manages multiple output busses, potentially of different types and configurations.
    *   Applies brightness limiting (ABL) based on estimated power consumption.

5.  **Pin Management (`PinManagerClass` in `pin_manager.h`, `pin_manager.cpp`):**
    *   Tracks GPIO pin allocation to prevent conflicts between different features (LEDs, buttons, relays, IR, usermods, hardware peripherals like I2C/SPI).
    *   Uses `PinOwner` tags for tracking and debugging.

6.  **Configuration Management (`cfg.cpp`, `wled_eeprom.cpp`):**
    *   Handles loading and saving configuration from/to the filesystem (`cfg.json`, `wsec.json`, `presets.json`).
    *   Uses ArduinoJson library for serialization/deserialization.
    *   Includes legacy EEPROM loading for compatibility with older versions (deprecated).

7.  **Web Server (`wled_server.cpp`):**
    *   Based on `ESPAsyncWebServer`.
    *   Serves the web UI (HTML, CSS, JS stored in flash program memory).
    *   Handles HTTP GET/POST requests for control and configuration.
    *   Provides RESTful JSON API (`/json`).
    *   Includes OTA update functionality (`/update`).
    *   Optional filesystem editor (`/edit`).

8.  **Communication Protocols:**
    *   **WebSockets (`ws.cpp`):** For real-time UI updates and live LED previews.
    *   **MQTT (`mqtt.cpp`):** For IoT integration and home automation systems. Uses `AsyncMqttClient`.
    *   **UDP Sync (`udp.cpp`):** WLED Notifier protocol for state synchronization between instances. Also handles E1.31 (sACN), Art-Net, DDP, Adalight, TPM2.NET real-time data input.
    *   **DMX (`dmx_output.cpp`, `dmx_input.cpp`):** Serial DMX output and input support.
    *   **Alexa Emulation (`alexa.cpp`):** Uses `Espalexa` library to emulate a Philips Hue bridge.
    *   **Hue Sync (`hue.cpp`):** Syncs state with a Philips Hue bridge.
    *   **Improv Serial (`improv.cpp`):** Protocol for easy WiFi provisioning.

9.  **Usermod System (`um_manager.cpp`, `usermods_list.cpp`, `Usermod` base class):**
    *   Modular system (v1 and v2 APIs) for extending WLED functionality.
    *   Usermods are registered and managed by `UsermodManager`.
    *   Can interact with core WLED features via provided methods and hooks (e.g., `loop()`, `addToJsonState()`, `readFromConfig()`).
    *   The `USERMOD_AUDIOREACTIVE` usermod is a key component of this fork.

## Control Flow

-   **Setup:** Initializes hardware, loads configuration, sets up busses, connects to WiFi/Ethernet, starts servers (Web, WS, UDP, etc.), initializes usermods.
-   **Loop:** Continuously handles network connections, incoming data (Serial, UDP, MQTT, WS, etc.), button/IR input, time synchronization, playlist progression, nightlight logic, transitions, usermod loops, and finally calls `strip.service()` to calculate and display the next LED frame.
-   **`strip.service()`:** Iterates through active segments, calls the appropriate effect function (`fx_*`), applies brightness/transitions, and triggers the `BusManager` to send data to the LEDs via `busses.show()`.

## Key Technologies

-   C++
-   PlatformIO build system
-   Arduino framework (ESP8266 Core / ESP32 Core)
-   ESPAsyncWebServer & AsyncTCP / ESPAsyncTCP for networking
-   ArduinoJson for JSON handling
-   NeoPixelBus library (adapted) for digital LED control
-   FastLED library (types and color utilities only)
-   LittleFS/SPIFFS for filesystem storage