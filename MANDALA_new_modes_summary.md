# Mandala LED Modes - New Effects Summary

This document provides a summary of the specialized audio-reactive LED effects available in the WLED MoonModules fork. These effects are particularly optimized for mandala configurations (radial LED strips extending from a central point) but can also work on standard linear strips and matrices.

## Audio-Reactive Visualization Modes

### 1. Bass Drop
**File:** `FX_EXP_fcn_bass_drop.cpp`

Detects bass drops in music and creates dynamic animations with different states and intensities based on the drop's strength and buildup. Uses a state machine approach to handle different phases (waiting, buildup, drop, recovery, calm).

**Best for:** EDM, dubstep, drum and bass, trap, and other electronic music with prominent bass drops and buildups.

### 2. Dynamic Range
**File:** `FX_EXP_fcn_dynamic_range.cpp`

Visualizes the dynamic range of audio by mapping volume changes to brightness and color variations, creating flashes during dramatic volume shifts. When significant volume changes are detected, it triggers flash effects with intensity proportional to the change. The effect continuously adapts to the audio's baseline volume.

**Best for:** Classical music, movie soundtracks, progressive rock, and other genres with dramatic volume dynamics and crescendos.

### 3. FFT Spectrum
**File:** `FX_fcn_fft_spectrum.cpp`

Creates a traditional frequency spectrum analyzer visualization with weighted distribution to show the intensity of different frequency bands (bass, mid, treble) on different areas on the mandala.

**Best for:** All music types, particularly effective with complex arrangements that have rich frequency content like orchestral music, electronic music with layered synths, or rock with multiple instruments.

### 4. Harmonic Viz
**File:** `FX_EXP_fcn_harmonic_viz.cpp`

Analyzes the emotional quality (major/minor tonality) of music and translates it into distinct visual patterns with corresponding color schemes - bright expansive patterns for happy (major) music and deeper, edge-focused patterns for moody (minor) music. 

The detection works by analyzing the FFT distribution where high frequency indicates major keys and low is minor keys. Brightness is distributed to emphasize the center for major keys and the edges for minor keys. Uses sine/cosine waves where frequency and amplitude are modulated by the detected tone, creating a visualization that "feels" like the emotional quality of the music.

**Note:** The effect doesn't override your chosen palette, but rather manipulates the hue position, saturation, and brightness to express the emotional quality of the music.

**Best for:** Classical, jazz, folk, and pop music with clear tonality and emotional shifts between major and minor sections.

### 5. Multi Comet: 13.4 version
**File:** `FX_fcn_multi_comet_134.cpp`

Generates multiple comet-like animations that move across the LED strip, triggered by audio peaks with different colors. This is the WLED v0.13.4 version of the effect, maintained for compatibility.

**Best for:** Rhythmic music with distinct beats like pop, rock, hip-hop, and dance music.

### 6. Multi Comet Bass (CE)
**File:** `FX_fcn_multi_comet_bass.cpp`

Creates bass-responsive comets that are launched in response to bass beats, with intensity, color, and frequency controlled by bass energy.

**Best for:** Bass-heavy music including hip-hop, R&B, dubstep, and electronic dance music with strong bass lines.

### 7. Noisemeter (CE)
**File:** `FX_fcn_noisemeter_og.cpp`

Displays a classic volume meter visualization showing the amplitude of sound in real-time, similar to traditional VU meters with color variations based on intensity.

**Best for:** All music types, providing a simple but effective visualization for any audio content; particularly engaging with dynamic music that has both quiet and loud sections.

### 8. Noisemeter 13.4 version
**File:** `FX_fcn_noisemeter134.cpp`

A variation of the classic volume meter visualization from WLED v0.13.4, maintained for compatibility.

**Best for:** All music types, particularly with dynamic volume ranges.

### 9. Note Tracker
**File:** `FX_EXP_fcn_note_tracker.cpp`

Detects musical notes in audio and displays them as colored segments that persist briefly, creating patterns based on the melody and musical flow. This effect tries to detect and visualize notes like "C", "C#", "D", "D#", etc.

**Best for:** Solo instrumental pieces, vocal performances, and music with clear, distinct melodies like piano solos, flute music, or songs with prominent vocal lines.

### 10. Spectral Centroid
**File:** `FX_EXP_fcn_spectral_centroid.cpp`

Maps the spectral "brightness" of sound (bass vs. treble balance) to different color temperatures and patterns—cool colors for bass-heavy audio and warm colors for treble-heavy sounds. 

The effect uses the average of all FFT bins which are assigned to different positions on the strip to determine where the "center of mass" of the sound lies. This value is found to be bass-heavy or treble-heavy and mapped to a color temperature spectrum (cool/blue for bass, warm/orange for treble). For circular displays, it creates distinct brightness patterns - brightening the center for bass-heavy audio and the outer edges for treble-heavy content.

**Best for:** Music with contrasting sections of bass and treble content, like electronic music that alternates between bass drops and high synth sections, or orchestral pieces with contrasting instrumental sections.

### 11. Spectral Flux
**File:** `FX_EXP_fcn_spectral_flux.cpp`

Visualizes changes in the sound spectrum over time, detecting transitions between musical sections and creating flowing patterns that change direction during shifts in musical sections. 

It measures how quickly the audio spectrum is changing by calculating the sum of positive differences between consecutive FFT frames. Significant flux events are detected when recent flux exceeds a threshold, with a minimum wait time between direction changes.

**Best for:** Progressive music with distinct section changes, DJ mixes with track transitions, and songs with bridge sections or dramatic changes in arrangement.

### 12. Firenight Flux
**File:** `FX_fcn_FirenightFlux.cpp`

Creates fluid, flowing patterns that respond to changes in the sound spectrum, detecting transitions between musical sections and creating flowing patterns that change direction during shifts. Similar to Spectral Flux but with a more flowing, fire-like visual effect.

**Best for:** Progressive music with distinct section changes, DJ mixes with track transitions, and songs with dramatic changes in arrangement.

### 13. Volume Ripples
**File:** `FX_EXP_fcn_volume_ripples.cpp`

Creates outward-expanding ripples from the center when volume peaks are detected, with ripple properties (size, brightness, color) tied to the intensity of the volume peaks.

**Best for:** Percussive music with clear transients like drums, piano with staccato playing, or any music with distinct and separated notes or beats.

## Special Utility Modes

### 1. Plinth
**File:** `FX_fcn_plinth.cpp`

A special non-audio effect optimized for dual strips of different lengths on one pin, allowing smooth color cycling that repeats properly on shorter segments while extending to longer ones. Designed for specific hardware configurations where strips of different lengths need to display coordinated patterns.

For installations that have a "plinth" or base component with a specific LED layout that differs from the main mandala configuration.

### 2. 2D Functions
**File:** `FX_2Dfcn.cpp`

Contains utility functions for manipulating 2D LED layouts, supporting matrix and mandala configurations with X/Y coordinate mapping. These helper functions enable effects to work with 2D layouts by providing methods for setting and getting pixel colors based on X/Y coordinates rather than just linear indices.

## Audio Analysis Helpers
**File:** `FX_fcn_audio_helpers.cpp`

A collection of utility functions for audio-reactive effects:
- Basic audio data validation and feature detection
- Spectral analysis (calculating spectral centroid)
- Bass drop detection
- Musical note detection
- Tonality analysis (major/minor classification)

These functions are used by various audio-reactive effects to create visuals that respond intelligently to different musical characteristics beyond just volume. 