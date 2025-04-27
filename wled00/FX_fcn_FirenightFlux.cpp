/*
 * Firenight Flux (FX_fcn_FirenightFlux.cpp)
 * 
 * Creates fluid, flowing patterns that respond to changes in the sound spectrum,
 * detecting transitions between musical sections and creating flowing patterns
 * that change direction during shifts. Similar to Spectral Flux but with a more
 * flowing, fire-like visual effect.
 * 
 * Best for: Progressive music with distinct section changes, DJ mixes with track 
 * transitions, and songs with dramatic changes in arrangement.
 */

#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include <cmath>

// Increased history size for better smoothing
#define NUM_FFT_BINS 16
#define FLUX_HISTORY_SIZE 16  // Increased from 12 for even smoother tracking
#define MIN_VOLUME_THRESHOLD 40.0f  // Increased from 30.0f
#define FLUX_THRESHOLD 50.0f
#define SPECTRAL_MIN_THRESHOLD 40.0f  // Increased from original value
#define SPECTRAL_MAX_THRESHOLD 80.0f  // Increased as well
#define FLUX_FADE_RATE 0.97f  // Slower fade rate
#define SPECTRAL_FLUX_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// IMPROVED: Define speed mapping for more dramatic effect at low speeds
#define MIN_COLOR_SPEED 1     // Minimum color movement speed
#define MAX_COLOR_SPEED 4     // Maximum color movement speed (reduced from 6)
#define MIN_FADE_SPEED 0.99f  // Extremely slow fade at minimum speed (was 0.97f)
#define MAX_FADE_SPEED 0.90f  // Faster fade at maximum speed (was 0.85f)
#define MIN_FLOW_DIVISOR 400.0f // Very slow flow at minimum speed (was 100.0f)
#define MAX_FLOW_DIVISOR 80.0f  // Faster flow at maximum speed

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);
extern bool hasFFTData(um_data_t *um_data);

uint16_t mode_spec_flux_firenight(void) {
  // Get audio data
  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !isAudioDataValid(um_data) || !hasFFTData(um_data)) {
    // Fallback pattern for no audio
    uint8_t x = millis() / 20;
    SEGMENT.fill(SEGMENT.color_from_palette(x, false, true, 0));
    return FRAMETIME;
  }
  
  // Get volume and FFT data
  float volume = *(float*)um_data->u_data[0];
  uint8_t* fftData = (uint8_t*)um_data->u_data[2];
  
  // Allocate memory for flux history and previous FFT
  const int dataSize = sizeof(float) * FLUX_HISTORY_SIZE + sizeof(uint8_t) * NUM_FFT_BINS;
  if (!SEGENV.allocateData(dataSize)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  // Parse the data - first FLUX_HISTORY_SIZE floats are for flux history
  float* fluxHistory = reinterpret_cast<float*>(SEGENV.data);
  
  // The remaining space is for the previous FFT data
  uint8_t* prevFFT = reinterpret_cast<uint8_t*>(SEGENV.data) + sizeof(float) * FLUX_HISTORY_SIZE;
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
    SEGENV.aux1 = 0;  // Current flow direction (0 = outward, 1 = inward)
    
    // Initialize flux history
    for (int i = 0; i < FLUX_HISTORY_SIZE; i++) {
      fluxHistory[i] = 0;
    }
    
    // Initialize previous FFT
    for (int i = 0; i < NUM_FFT_BINS; i++) {
      prevFFT[i] = 0;
    }
  }
  
  // IMPROVED: Speed more dramatically controls all animation rates
  // Much slower at low settings, still responsive at high settings
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, MIN_COLOR_SPEED, MAX_COLOR_SPEED);
  float fadeSpeed = map_float(SEGMENT.speed, 0, 255, MIN_FADE_SPEED, MAX_FADE_SPEED);
  float flowDivisor = map_float(SEGMENT.speed, 0, 255, MIN_FLOW_DIVISOR, MAX_FLOW_DIVISOR);
  
  // Intensity now truly controls "sensitivity" to flux
  float sensitivity = map_float(SEGMENT.intensity, 0, 255, 0.4f, 1.5f);  // More moderate range
  float fluxThreshold = map_float(SEGMENT.intensity, 0, 255, SPECTRAL_MAX_THRESHOLD, SPECTRAL_MIN_THRESHOLD);
  
  // IMPROVED: Only update color movement every N frames based on speed
  // At lowest speed, update every 4 frames, at highest speed, every frame
  uint8_t updateRate = map(SEGMENT.speed, 0, 255, 4, 1);
  if (SEGENV.call % updateRate == 0) {
    SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  }
  
  // Calculate spectral flux (sum of differences between current and previous FFT)
  float currentFlux = 0;
  
  if (volume > MIN_VOLUME_THRESHOLD) {
    for (int i = 0; i < NUM_FFT_BINS; i++) {
      // Only consider positive changes (increases in energy)
      float diff = max(0.0f, (float)fftData[i] - (float)prevFFT[i]);
      currentFlux += diff;
    }
    
    // IMPROVED: Apply speed-dependent damping factor for smoother transitions at low speeds
    float dampingFactor = map_float(SEGMENT.speed, 0, 255, 0.85f, 0.6f) * sensitivity;
    
    // Gradually update previous FFT for smoother transitions
    for (int i = 0; i < NUM_FFT_BINS; i++) {
      prevFFT[i] = prevFFT[i] * dampingFactor + fftData[i] * (1.0f - dampingFactor);
    }
  }
  
  // Update flux history
  static uint8_t historyIndex = 0;
  fluxHistory[historyIndex] = currentFlux;
  historyIndex = (historyIndex + 1) % FLUX_HISTORY_SIZE;
  
  // Calculate average flux from history for stability
  float avgFlux = 0;
  float recentFlux = 0;
  
  // Use weighted average - recent values count more
  float totalWeight = 0;
  for (int i = 0; i < FLUX_HISTORY_SIZE; i++) {
    // Circular buffer - find actual index relative to current
    int actualIndex = (historyIndex - 1 - i + FLUX_HISTORY_SIZE) % FLUX_HISTORY_SIZE;
    float weight = FLUX_HISTORY_SIZE - i;
    
    avgFlux += fluxHistory[actualIndex] * weight;
    totalWeight += weight;
    
    // Average of most recent values for sharp reactions
    if (i < 3) recentFlux += fluxHistory[actualIndex];
  }
  avgFlux /= totalWeight;
  recentFlux /= 3;
  
  // Detect significant spectral flux (sonic transitions)
  bool fluxEvent = recentFlux > fluxThreshold;
  
  // IMPROVED: Need sustained flux for direction change with speed-dependent timing
  static uint32_t lastDirectionChange = 0;
  uint32_t now = millis();
  
  // Only change direction if enough time has passed (much longer at low speeds)
  float directionChangeDelay = map_float(SEGMENT.speed, 0, 255, 5000.0f, 1500.0f) * sensitivity;
  if (fluxEvent && now - lastDirectionChange > directionChangeDelay) {
    SEGENV.aux1 = !SEGENV.aux1;  // Toggle flow direction
    lastDirectionChange = now;
  }
  
  // Update flux event tracking
  static uint32_t lastFluxEvent = 0;
  static float fluxEventIntensity = 0;
  
  // IMPROVED: Speed-dependent timing for flux events
  float eventDelay = map_float(SEGMENT.speed, 0, 255, 1200.0f, 300.0f) * sensitivity;
  
  // Update flux event with minimum time between events based on speed and sensitivity
  if (fluxEvent && now - lastFluxEvent > eventDelay) {
    float newIntensity = map_float(recentFlux, fluxThreshold, fluxThreshold * 3.0f, 0.5f, 1.0f);
    newIntensity = constrain(newIntensity, 0.5f, 1.0f);
    
    // IMPROVED: Speed-dependent smoothing - slower at low speeds for smoother transitions
    float smoothFactor = map_float(SEGMENT.speed, 0, 255, 0.15f, 0.5f);
    fluxEventIntensity = fluxEventIntensity * (1.0f - smoothFactor) + newIntensity * smoothFactor;
    lastFluxEvent = now;
  } else {
    // IMPROVED: Speed-dependent decay - much slower at low speeds
    // Apply fade speed directly to the decay rate
    fluxEventIntensity *= fadeSpeed + (sensitivity * 0.015f);
  }
  
  // Apply spectral flux visualization optimized for circular display
  for (int i = 0; i < SEGLEN; i++) {
    // Base color moves slowly
    uint8_t baseHue = SEGENV.aux0;
    
    // For circular display, calculate position relative to center
    // Distance from center (0.0 = center, 1.0 = edge)
    float centerDistance;
    
    // Optimize for circular layout by using modulo distance from center
    int halfLength = SEGLEN / 2;
    int distFromCenter = abs(i - halfLength);
    if (distFromCenter > halfLength) distFromCenter = SEGLEN - distFromCenter;
    centerDistance = (float)distFromCenter / halfLength;
    
    // IMPROVED: Create flowing pattern with speed-dependent flow rate
    float flowDirection = SEGENV.aux1 ? -1.0f : 1.0f;
    float flowOffset = (now / flowDivisor) * flowDirection;
    float flowPosition = fmod(centerDistance * 3.0f + flowOffset, 2.0f);
    if (flowPosition > 1.0f) flowPosition = 2.0f - flowPosition;  // Triangle wave
    
    // Modify hue based on flow position and flux events, with sensitivity factor
    float hueShift = (flowPosition * 128) + (fluxEventIntensity * 85 / sensitivity);
    uint8_t hue = (int)(baseHue + hueShift) % 256;
    
    // Calculate brightness based on flux, with sensitivity dampening
    uint8_t brightness;
    
    if (fluxEventIntensity > 0.1f) {
      // IMPROVED: Speed-dependent wave frequency - slower at low speeds
      float waveSpeed = map_float(SEGMENT.speed, 0, 255, 400.0f, 120.0f) * sensitivity;
      float wave = sin(centerDistance * TWO_PI * 2 + now / waveSpeed);
      
      // Scale volume impact by sensitivity
      float volumeBrightness = map_float(volume, MIN_VOLUME_THRESHOLD, 255, 0.25f, 1.0f);
      volumeBrightness = constrain(volumeBrightness, 0.25f, 1.0f);
      
      // Calculate final brightness with sensitivity and speed factors
      brightness = 64 + volumeBrightness * 191 * (0.8f + 0.2f * wave) / sensitivity;
      
      // IMPROVED: Speed-dependent flash effect - subtler at low speeds
      float flashIntensity = map_float(SEGMENT.speed, 0, 255, 0.15f, 0.3f);
      if (fluxEventIntensity > 0.7f) {
        brightness = brightness * (1.0f + fluxEventIntensity * flashIntensity / sensitivity);
      }
    } else {
      // Normal brightness when no flux events
      float volumeBrightness = map_float(volume, MIN_VOLUME_THRESHOLD, 255, 0.25f, 1.0f);
      volumeBrightness = constrain(volumeBrightness, 0.25f, 1.0f);
      brightness = 64 + volumeBrightness * 191 / sensitivity;
    }
    
    // Get color from palette
    uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply brightness
    uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
    uint8_t g = ((color >> 8) & 0xFF) * brightness / 255;
    uint8_t b = (color & 0xFF) * brightness / 255;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  // Debug output
  if (SPECTRAL_FLUX_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("EXP-SPECTRAL-FLUX: Vol=%.1f Flux=%.1f Speed=%d FlowDiv=%.1f FadeSpeed=%.4f Thresh=%.1f Event=%d\n",
      volume, avgFlux, SEGMENT.speed, flowDivisor, fadeSpeed, fluxThreshold, fluxEvent ? 1 : 0);
  }
  
  return FRAMETIME;
} 