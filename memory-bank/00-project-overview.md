# File: memory-bank/00-project-overview.md
# ----
# Project Overview for WLED MoonModules Fork

## Project Goal

This project is a fork of the popular open-source WLED project (originally by Aircoookie), specifically the MoonModules branch. The primary goal is to extend the core WLED functionality with advanced features, custom effects, and particularly enhanced audio-reactive capabilities. It serves as a development platform based on WLED v0.14, incorporating features that may not be present in the main WLED repository or the WLED SR (Sound Reactive) v0.13 fork.

## Core Functionality

-   **LED Control:** Provides sophisticated control over various types of addressable LEDs (WS281x, SK6812, APA102, etc.) and PWM/analog LEDs.
-   **Web Interface:** Offers a comprehensive web UI for configuration, control, and live preview.
-   **Effects Engine:** Includes a wide range of built-in effects and palettes, managed by the WS2812FX library adapted for WLED.
-   **Segmentation:** Allows dividing the LED strip into multiple virtual segments, each with independent effects, colors, and settings.
-   **Presets & Playlists:** Enables saving and loading configurations as presets and sequencing them in playlists.
-   **Connectivity:** Supports WiFi (STA and AP modes), Ethernet (on specific ESP32 boards), MQTT, UDP Sync (WLED Notifier, E1.31, Art-Net, DDP), DMX (In/Out), Alexa integration, Hue Sync, and Serial communication (Adalight, JSON API, Improv).
-   **Usermods:** A modular system (v1 and v2 API) allows extending functionality with custom code (e.g., sensors, displays, custom logic).

## MoonModules Specific Focus

-   **Audio Reactivity:** Strong emphasis on audio-reactive effects, integrating with the `USERMOD_AUDIOREACTIVE` usermod. Several custom effects are designed specifically for audio visualization, often tailored for specific physical layouts.
-   **Custom Effects:** Development and integration of new, often complex, 1D and 2D effects, primarily housed in `FX_EXP_fcn_*.cpp` files.
-   **Mandala Configuration:** Many custom audio-reactive effects are designed with a specific physical layout in mind: multiple LED strips arranged radially outwards from a central point (LED 0), forming a mandala pattern. New effects should ideally consider or support this configuration. Some effects might target specific parts of this setup, like the plinth or individual leaves/strips.
-   **Performance Optimizations:** Experimental features like `WLEDMM_FASTPATH` aim to improve performance, especially crucial for audio reactivity and large installations.
-   **Hardware Support:** Integration and testing with various ESP32/ESP8266 boards and specific hardware shields (e.g., Wemos shields, Ethernet boards). ESP32 is generally preferred for audio-reactive features due to performance requirements.

## Target Hardware

-   ESP32