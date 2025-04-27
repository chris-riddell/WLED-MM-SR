# File: memory-bank/06-custom-features.md
# ----
# Custom Features: Mandala & Audio Reactivity

This file details the specific custom features and design considerations unique to the WLED MoonModules fork.

## 1. Target Physical Layout: Mandala Configuration

-   **Description:** While WLED-MM can run on standard strips and matrices, many of its unique audio-reactive effects (`FX_EXP_fcn_*.cpp`) are optimized for or designed around a "mandala" configuration.
-   **Structure:** This typically involves multiple LED strips (often 8 or more) connected to different data pins on the ESP32. These strips are physically arranged to radiate outwards from a central point.
-   **LED Indexing:** The first LED (index 0) of each strip is assumed to be at the center, with subsequent LEDs moving outwards along the strip's length.
-   **Effect Design:** Effects designed for this setup often:
    *   Utilize calculations based on the distance from the center (LED index 0 of a segment).
    *   Create radially symmetric or expanding/contracting patterns.
    *   May treat the different strips (segments) as separate elements (leaves, petals) or as a continuous circular/radial display space.
    *   Some effects (`mode_plinth`, `mode_noisemeter_134`) target specific, potentially non-mandala parts of an installation.

## 2. Audio Reactivity Integration

-   **Core Component:** Relies heavily on the `USERMOD_AUDIOREACTIVE` usermod (typically compiled in). This usermod processes audio input (analog mic, digital I2S mic, or line-in) and provides processed data to effects.
-   **Data Structure (`um_data_t`):** Effects access audio data via the `usermods.getUMData()` function, retrieving a pointer to a `um_data_t` structure. Key data points often used include:
    *   `u_data[0]` (float\*): Smoothed volume (`volumeSmth`).
    *   `u_data[1]` (int16_t\*): Raw volume (`volumeRaw`).
    *   `u_data[2]` (uint8_t\*): FFT frequency bin results (typically 16 bins).
    *   `u_data[3]` (uint8_t\*): Sample peak detection flag.
    *   `u_data[4]` (float\*): Dominant frequency peak (`FFT_MajorPeak`).
    *   `u_data[5]` (float\*): FFT magnitude.
    *   Other indices may contain further processed data like AGC gain, sound pressure, etc.
-   **Effect Implementation:** Custom audio-reactive effects (`FX_EXP_fcn_*.cpp`) typically:
    *   Include `const.h` for `USERMOD_ID_AUDIOREACTIVE`.
    *   Include `audio_utils.h` for helper functions.
    *   Call `usermods.getUMData()` at the start.
    *   Use `SEGENV` to store state between frames (e.g., history arrays, previous values, timers). Use `SEGENV.allocateData()` for dynamic memory needs within the effect runtime.
    *   Map effect sliders (`SEGMENT.speed`, `SEGMENT.intensity`, `SEGMENT.custom1/2/3`) to control audio sensitivity, animation speed, decay rates, thresholds, etc.
    *   Implement fallback behavior (e.g., a simple pattern) if audio data is unavailable.
    *   Consider the mandala layout when calculating pixel positions and patterns.
-   **Utility Functions (`audio_utils.h`, `FX_fcn_audio_helpers.cpp`):** Provide reusable functions for common audio analysis tasks like detecting tonality, spectral centroid, bass drops, or musical notes, which can be leveraged by multiple effects.

## 3. Specialized Audio Analysis Features

The codebase includes several sophisticated audio analysis capabilities:

-   **Bass Drop Detection:** The `detectBassDropIntensity()` function analyzes audio characteristics to identify bass drops and buildups in music, enabling effects (like `mode_bass_drop`) to synchronize dramatic visual changes with musical climaxes.
-   **Tonality Analysis:** The `detectTonality()` function analyzes frequency content to classify audio as major (bright/happy), minor (dark/moody), or neutral, allowing effects (like `mode_harmonic_viz`) to adapt their color schemes accordingly.
-   **Spectral Analysis:** Functions like `calculateSpectralCentroid()` analyze the frequency distribution, determining if the sound is predominantly bass-heavy or treble-heavy.
-   **Dynamic Range Processing:** Effects like `mode_dynamic_range` track the audio's dynamic range over time to create visuals that respond to both subtle and dramatic changes in the music.
-   **Multi-dimensional Response:** Many effects track multiple audio characteristics simultaneously (volume, frequency content, transients, etc.) for complex and nuanced visual responses.

## 4. Key Audio-Reactive Effects

The codebase includes several specialized audio-reactive effects:

-   **Bass Drop (FX_EXP_fcn_bass_drop.cpp):** Detects and responds to bass drops in music, creating dramatic visual climaxes that match the music's energy. Uses a state machine approach to handle different phases (waiting, buildup, drop, recovery, calm), particularly effective for EDM, dubstep, drum and bass, and other electronic music with prominent bass drops.
-   **Dynamic Range (FX_EXP_fcn_dynamic_range.cpp):** Visualizes the dynamic range of audio, creating subtle patterns for consistent audio and dramatic effects for highly variable audio. Particularly effective for classical music, movie soundtracks, and other genres with dramatic volume dynamics and crescendos.
-   **Harmonic Visualization (FX_EXP_fcn_harmonic_viz.cpp):** Analyzes musical tonality to adapt colors - bright complementary colors for major keys, deeper saturated colors for minor keys. Works best with classical, jazz, folk, and pop music with clear tonality shifts.
-   **Note Tracker (FX_EXP_fcn_note_tracker.cpp):** Detects musical notes in real-time and creates visualizations based on the notes being played. Ideal for solo instrumental pieces, vocal performances, and music with clear, distinct melodies.
-   **Spectral Centroid (FX_EXP_fcn_spectral_centroid.cpp):** Maps the spectral "brightness" of sound to different color temperatures - cool/blue colors for bass-heavy audio and warm/orange for treble-heavy sounds. On circular displays, it brightens the center for bass and outer edges for treble.
-   **Spectral Flux (FX_EXP_fcn_spectral_flux.cpp):** Visualizes changes in the sound spectrum over time, detecting transitions between musical sections and creating flowing patterns that change direction during these shifts.
-   **Volume Ripples (FX_EXP_fcn_volume_ripples.cpp):** Creates expanding circular patterns from the center that react to audio volume peaks. Works best with percussive music with clear transients.
-   **FFT Spectrum (FX_fcn_fft_spectrum.cpp):** Creates a traditional frequency spectrum analyzer visualization with weighted distribution to show the intensity of different frequency bands (bass, mid, treble) across the LED display. Works with all music types but particularly effective with complex arrangements that have rich frequency content.
-   **Firenight Flux (FX_fcn_FirenightFlux.cpp):** Creates fluid, flowing patterns that respond to changes in audio spectrum, with direction changes triggered by significant spectral transitions.
-   **Multi Comet Bass (FX_fcn_multi_comet_bass.cpp):** Launches comet-like effects timed with bass beats, with intensity, color, and frequency controlled by bass energy. Best for bass-heavy music.
-   **Multi Comet 134 (FX_fcn_multi_comet_134.cpp):** The WLED v0.13.4 version of Multi Comet that generates multiple comet-like animations moving across the LED strip, triggered by audio peaks with various colors. Works well with rhythmic music with distinct beats.
-   **Noisemeter (FX_fcn_noisemeter_og.cpp, FX_fcn_noisemeter134.cpp):** Creates volume-reactive bar visualizations similar to traditional VU meters, with color variations based on intensity. Effective with any audio content, particularly with dynamic music.

## 5. Non-Audio Special Effects

Some special effects are designed for specific hardware configurations:

-   **Plinth (FX_fcn_plinth.cpp):** Optimized for dual strips of different lengths on a single pin, allowing smooth color cycling that repeats properly on shorter segments while extending to longer ones.
-   **2D Effects (FX_2Dfcn.cpp):** Contains functions for manipulating 2D LED layouts, supporting matrix and mandala configurations with X/Y coordinate mapping.

## 6. Common Design Patterns

Several common design approaches are used across the codebase:

-   **Smoothing:** Almost all effects incorporate temporal smoothing to avoid jarring transitions, typically using weighted averaging with configurable smoothing factors.
-   **History Tracking:** Many effects maintain history buffers to analyze audio patterns over time, enabling them to respond to trends rather than instantaneous changes.
-   **Speed/Intensity Mapping:** Effects consistently map the speed slider to animation rates and the intensity slider to sensitivity or "calmness" of the effect.
-   **Fallback Patterns:** All audio effects include graceful fallback patterns when audio data is unavailable.
-   **Radial Calculations:** Effects optimized for mandala layouts often calculate distances from center and use these for circular/radial visualizations.
-   **Palette Integration:** Effects use the built-in palette system (`SEGMENT.color_from_palette()`) for flexible color control.

## 7. Development Guidelines for New Custom Effects

-   Place new custom audio/mandala effects in files named `FX_EXP_fcn_*.cpp`.
-   Register the new effect function and its metadata string in `FX.cpp` within the `WS2812FX::setupEffectData()` function. Likewise, register the new function number in FX.h.
-   Utilize the `USERMOD_AUDIOREACTIVE` interface (`um_data_t`) for audio input.
-   Use `SEGENV` for persistent state within the effect.
-   Design with the radial/circular mandala layout as a primary consideration, but allow for reasonable behavior on standard linear strips or 2D matrices if possible.
-   Leverage existing audio utility functions where applicable.
-   Follow existing coding style and patterns found in other `FX_EXP_fcn_*.cpp` files.
-   ESP32 is the recommended platform due to performance needs for FFT and complex effects.