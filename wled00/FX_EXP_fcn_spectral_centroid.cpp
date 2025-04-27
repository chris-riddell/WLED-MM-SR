/*
 * Spectral Centroid (FX_EXP_fcn_spectral_centroid.cpp)
 * 
 * Maps the spectral "brightness" of sound (bass vs. treble balance) to different 
 * color temperatures and patterns—cool colors for bass-heavy audio and warm colors 
 * for treble-heavy sounds. 
 * 
 * The effect uses the average of all FFT bins which are assigned to different 
 * positions on the strip to determine where the "center of mass" of the sound lies. 
 * This value is found to be bass-heavy or treble-heavy and mapped to a color 
 * temperature spectrum (cool/blue for bass, warm/orange for treble). 
 * 
 * For circular displays, it creates distinct brightness patterns - brightening 
 * the center for bass-heavy audio and the outer edges for treble-heavy content.
 * 
 * Best for: Music with contrasting sections of bass and treble content, like 
 * electronic music that alternates between bass drops and high synth sections, 
 * or orchestral pieces with contrasting instrumental sections.
 */

#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>

#define MIN_VOLUME_THRESHOLD 40.0f  // Increased from 30.0f
#define CENTROID_HISTORY_SIZE 16  // Increased from 8 for smoother transitions
#define CENTROID_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);
extern bool hasFFTData(um_data_t *um_data);
extern float calculateSpectralCentroid(uint8_t* fftData, int numBins);

uint16_t mode_spectral_centroid(void) {
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
  
  // Allocate memory for centroid history and additional state variables
  if (!SEGENV.allocateData(sizeof(float) * CENTROID_HISTORY_SIZE + sizeof(float) * 3 + sizeof(uint8_t))) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  float* centroidHistory = reinterpret_cast<float*>(SEGENV.data);
  // Store smoothed centroid, prev volume, and effect intensity at the end
  float* smoothedCentroid = reinterpret_cast<float*>(SEGENV.data + sizeof(float) * CENTROID_HISTORY_SIZE);
  float* prevVolume = smoothedCentroid + 1;
  float* effectIntensity = prevVolume + 1;
  uint8_t* paletteOffset = reinterpret_cast<uint8_t*>(SEGENV.data + sizeof(float) * CENTROID_HISTORY_SIZE + sizeof(float) * 3);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color offset for animation
    
    // Initialize centroid history
    for (int i = 0; i < CENTROID_HISTORY_SIZE; i++) {
      centroidHistory[i] = 0.5f;  // Middle value
    }
    
    *smoothedCentroid = 0.5f;
    *prevVolume = 0;
    *effectIntensity = 0;
    *paletteOffset = 0; // Initialize palette offset
  }
  
  // Speed controls effect responsiveness and animation speed
  float responsiveness = map_float(SEGMENT.speed, 0, 255, 0.01f, 0.4f);  // Expanded range for more responsiveness
  float animationSpeed = map_float(SEGMENT.speed, 0, 255, 0.5f, 8.0f);   // Wider range for more visible speed control
  
  // Intensity now controls the intensity of color shifts and effect animations
  float colorIntensity = map_float(SEGMENT.intensity, 0, 255, 0.5f, 3.0f);
  
  // Custom1 now used for calmness/fade rate control to make it more effective
  float calmness = map_float(SEGMENT.custom1, 0, 255, 0.1f, 2.0f);
  float volumeThreshold = map_float(SEGMENT.custom1, 0, 255, 20.0f, 100.0f);  
  
  // Custom2 controls how much of the palette we use
  float paletteRange = map_float(SEGMENT.custom2, 0, 255, 0.25f, 1.0f);  // How much of the palette to cycle through
  
  // Update palette offset for continuous color cycling
  // The more volume and higher the centroid (treble), the faster the palette cycles
  float cycleSpeed = animationSpeed * 0.5f;
  if (volume > volumeThreshold) {
    // Volume-reactive palette cycling
    float volumeFactor = constrain((volume - volumeThreshold) / (255.0f - volumeThreshold), 0.0f, 1.0f);
    cycleSpeed += volumeFactor * animationSpeed * 0.5f;
    
    // Centroid affects cycle direction and speed
    if (*smoothedCentroid > 0.5f) {
      // Higher frequencies cause forward cycling (treble)
      *paletteOffset = (*paletteOffset + (uint8_t)(cycleSpeed)) % 256;
    } else {
      // Lower frequencies cause reverse cycling (bass)
      *paletteOffset = (*paletteOffset - (uint8_t)(cycleSpeed)) % 256;
    }
  } else {
    // Continue cycling at base speed when volume is low
    *paletteOffset = (*paletteOffset + (uint8_t)(cycleSpeed * 0.25f)) % 256;
  }
  
  // Only update centroid if volume is sufficient
  static uint8_t historyIndex = 0;
  
  if (volume > volumeThreshold) {
    // Calculate spectral centroid (frequency center of gravity)
    float centroid = calculateSpectralCentroid(fftData, 16);  // Use 16 FFT bins
    
    // Apply a bit of hysteresis to prevent rapid fluctuations
    float centroidDiff = abs(centroid - centroidHistory[historyIndex]);
    if (centroidDiff > 0.05f / calmness) {  // More sensitive with lower calmness
      // Only update if change is significant
      centroidHistory[historyIndex] = centroid;
      historyIndex = (historyIndex + 1) % CENTROID_HISTORY_SIZE;
    }
  }
  
  // Calculate average centroid from history for stability
  float avgCentroid = 0;
  float totalWeight = 0;
  
  // Use weighted average - recent values count more
  for (int i = 0; i < CENTROID_HISTORY_SIZE; i++) {
    // Circular buffer - find actual index relative to current
    int actualIndex = (historyIndex - 1 - i + CENTROID_HISTORY_SIZE) % CENTROID_HISTORY_SIZE;
    float weight = CENTROID_HISTORY_SIZE - i;
    
    avgCentroid += centroidHistory[actualIndex] * weight;
    totalWeight += weight;
  }
  avgCentroid /= totalWeight;
  
  // Smooth the centroid transition - directly use calmness to control fade rate
  // Lower calmness (lower custom1) = faster response
  *smoothedCentroid = *smoothedCentroid * (1.0f - responsiveness / calmness) + 
                     avgCentroid * responsiveness / calmness;
  
  // Detect volume changes for effect intensity
  float volumeChange = abs(volume - *prevVolume);
  *prevVolume = volume;
  
  // Update effect intensity based on volume changes
  if (volumeChange > 5.0f && volume > volumeThreshold) {
    // Increase effect intensity on significant volume changes
    *effectIntensity = min(1.0f, *effectIntensity + volumeChange / (100.0f / colorIntensity));
  } else {
    // Decay effect intensity - fade rate directly affected by calmness
    *effectIntensity = max(0.0f, *effectIntensity - 0.01f * calmness);
  }
  
  // Get current time for animations
  uint32_t now = millis();
  
  // Map spectral centroid to color temperature
  uint8_t warmth = 255 * *smoothedCentroid;  // 0 = cool, 255 = warm
  
  // Use smoothed centroid to determine color distribution in a circular pattern
  for (int i = 0; i < SEGLEN; i++) {
    // For circular layout, calculate position relative to center
    int halfLength = SEGLEN / 2;
    int distFromCenter = abs(i - halfLength);
    if (distFromCenter > halfLength) distFromCenter = SEGLEN - distFromCenter;
    float centerDistance = (float)distFromCenter / halfLength;
    
    // Angle around the circle
    float angle = (float)i / SEGLEN * TWO_PI;
    
    // Determine color based on centroid (temperature) and position
    uint32_t color;
    
    if (SEGMENT.palette) {
      // Use palette if specified, with enhanced cycling
      // Basic cyclic position in palette based on LED position and base offset
      uint8_t baseIndex = (i * 256 / SEGLEN) % 256;
      
      // Add wave pattern based on angle, animation time, and centroid
      float wave = sin(angle * 3 + now / (1000.0f / animationSpeed));
      
      // Calculate overall palette index with expanded range 
      uint16_t rawIndex = *paletteOffset;
      
      // Add position-based component for spatial variation - scaled by palette range
      rawIndex += baseIndex * paletteRange;
      
      // Add wave component that's affected by effect intensity
      rawIndex += (int)(wave * 30 * *effectIntensity * colorIntensity);
      
      // Add centroid-based offset for frequency response
      // Lower frequencies shift palette more dramatically  
      if (*smoothedCentroid < 0.5f) {
        float bassEffect = (0.5f - *smoothedCentroid) * 2.0f;
        rawIndex += (uint16_t)(bassEffect * 128 * colorIntensity * *effectIntensity);
      }
      
      uint8_t index = rawIndex % 256;
      color = SEGMENT.color_from_palette(index, false, PALETTE_SOLID_WRAP, 0);
    } else {
      // Generate color temperature directly
      uint8_t r, g, b;
      
      // Create a color gradient based on centroid
      if (*smoothedCentroid < 0.5f) {
        // Cool colors (blue to cyan)
        float cool = *smoothedCentroid * 2.0f;  // 0-1 range
        r = 0;
        g = 210 * cool;
        b = 180 + 75 * (1.0f - cool);
      } else {
        // Warm colors (cyan to orange/red)
        float warm = (*smoothedCentroid - 0.5f) * 2.0f;  // 0-1 range
        r = 100 + 155 * warm;
        g = 210 * (1.0f - warm * 0.7f);
        b = 255 * (1.0f - warm);
      }
      
      // Apply brightness based on volume and calmness
      float volumeFactor = map_float(volume, volumeThreshold, 255, 0.4f, 1.0f);
      volumeFactor = constrain(volumeFactor, 0.4f, 1.0f);
      
      uint8_t brightness = 255 * volumeFactor * colorIntensity;
      r = min(255, (r * brightness) / 255);
      g = min(255, (g * brightness) / 255);
      b = min(255, (b * brightness) / 255);
      
      color = (r << 16) | (g << 8) | b;
    }
    
    // Apply circular pattern effects based on spectral centroid
    float brightnessMod = 1.0f;
    
    // Add central pulsing effect
    if (*smoothedCentroid < 0.5f) {
      // Bass-heavy audio: brighten center
      float bassFactor = 1.0f - *smoothedCentroid * 2.0f;  // 1.0-0.0 range
      float pulse = (sin(now / (2000.0f / animationSpeed)) + 1.0f) / 2.0f;  // 0-1 pulse
      
      // More intense center effect for bass
      if (centerDistance < 0.3f) {
        brightnessMod += bassFactor * pulse * (1.0f - centerDistance * 3.0f) * 0.6f * colorIntensity;
      }
    } else {
      // Treble-heavy audio: brighten outer ring
      float trebleFactor = (*smoothedCentroid - 0.5f) * 2.0f;  // 0.0-1.0 range
      float pulse = (sin(now / (1500.0f / animationSpeed)) + 1.0f) / 2.0f;  // 0-1 pulse
      
      // More intense edge effect for treble
      if (centerDistance > 0.7f) {
        brightnessMod += trebleFactor * pulse * (centerDistance - 0.7f) * 3.0f * 0.6f * colorIntensity;
      }
    }
    
    // Add wave effect based on effect intensity
    float wave = sin(angle * (6 + *smoothedCentroid * 4) + now / (1000.0f / animationSpeed));
    brightnessMod += wave * 0.25f * *effectIntensity * colorIntensity;
    
    // Apply calculated brightness modulation
    brightnessMod = constrain(brightnessMod, 0.0f, 2.0f);
    
    uint8_t r = min(255, (int)(((color >> 16) & 0xFF) * brightnessMod));
    uint8_t g = min(255, (int)(((color >> 8) & 0xFF) * brightnessMod));
    uint8_t b = min(255, (int)((color & 0xFF) * brightnessMod));
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  // Debug output
  if (CENTROID_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("EXP-SPECTRAL-CENTROID: Vol=%.1f Centroid=%.2f SmoothCent=%.2f Effect=%.2f PalOffset=%d\n",
      volume, avgCentroid, *smoothedCentroid, *effectIntensity, *paletteOffset);
  }
  
  return FRAMETIME;
} 