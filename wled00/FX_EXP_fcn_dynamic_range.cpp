/*
 * Dynamic Range (FX_EXP_fcn_dynamic_range.cpp)
 * 
 * Visualizes the dynamic range of audio by mapping volume changes to brightness 
 * and color variations, creating flashes during dramatic volume shifts. When 
 * significant volume changes are detected, it triggers flash effects with 
 * intensity proportional to the change. The effect continuously adapts to the 
 * audio's baseline volume.
 * 
 * Best for: Classical music, movie soundtracks, progressive rock, and other 
 * genres with dramatic volume dynamics and crescendos.
 */

#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include <cmath>

#define HISTORY_SIZE 32  // Increased from 24 for smoother transitions
#define MIN_VOLUME_THRESHOLD 40.0f
#define FLASH_THRESHOLD 0.3f  // Reduced from 0.4f for even less jarring flashes
#define DYNAMIC_RANGE_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// IMPROVED: Better color and flash control
#define MIN_SATURATION 180        // Much higher minimum saturation to preserve colors
#define MAX_SATURATION_REDUCTION 30  // Drastically reduced from 50 to maintain colors
#define MIN_WHITE_THRESHOLD 240   // Higher threshold before any white (reduced white flashing)
#define COLOR_CHANGE_SPEED_MIN 2
#define COLOR_CHANGE_SPEED_MAX 8  // Reduced from 12 for smoother transitions
#define COLOR_DIVERSITY_FACTOR 2.0f

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);

uint16_t mode_dynamic_range(void) {
  // Get audio data
  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !isAudioDataValid(um_data)) {
    // Fallback pattern for no audio
    uint8_t x = millis() / 20;
    SEGMENT.fill(SEGMENT.color_from_palette(x, false, true, 0));
    return FRAMETIME;
  }
  
  // Get volume data
  float volume = *(float*)um_data->u_data[0];
  
  // Allocate memory for volume history and additional state variables
  if (!SEGENV.allocateData(sizeof(float) * HISTORY_SIZE + sizeof(float) * 4)) {
    return FRAMETIME;
  }
  
  float* volumeHistory = reinterpret_cast<float*>(SEGENV.data);
  float* smoothedVolume = reinterpret_cast<float*>(SEGENV.data + sizeof(float) * HISTORY_SIZE);
  float* flashIntensity = smoothedVolume + 1;
  float* smoothedRange = flashIntensity + 1;
  float* smoothedBrightnessFactor = smoothedRange + 1;
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) {
      volumeHistory[i] = volume;
    }
    *smoothedVolume = volume;
    *flashIntensity = 0;
    *smoothedRange = 0;
    *smoothedBrightnessFactor = 0.7f;
  }
  
  // IMPROVED: More gradual response to changes
  float responsiveness = map_float(SEGMENT.speed, 0, 255, 0.01f, 0.2f); // Reduced max
  float animationSpeed = map_float(SEGMENT.speed, 0, 255, 0.2f, 1.0f);  // Reduced max
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, COLOR_CHANGE_SPEED_MIN, COLOR_CHANGE_SPEED_MAX);
  
  // Intensity controls visualization style and flash sensitivity
  float calmness = map_float(SEGMENT.intensity, 0, 255, 0.4f, 2.0f);
  float flashSensitivity = map_float(SEGMENT.intensity, 0, 255, 1.5f, 0.5f);
  
  // Update color movement
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  
  // Smooth volume with variable rate
  float smoothingFactor = 0.6f + (calmness * 0.2f);
  *smoothedVolume = *smoothedVolume * smoothingFactor + volume * (1.0f - smoothingFactor);
  
  // Update history
  static uint8_t historyIndex = 0;
  volumeHistory[historyIndex] = *smoothedVolume;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  
  // Calculate volume statistics with weighted recent values
  float minVolume = volumeHistory[0];
  float maxVolume = volumeHistory[0];
  float avgVolume = 0;
  float recentAvgVolume = 0;
  float totalWeight = 0;
  
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
    float weight = (HISTORY_SIZE - i) / (float)HISTORY_SIZE;
    float vol = volumeHistory[idx];
    
    minVolume = min(minVolume, vol);
    maxVolume = max(maxVolume, vol);
    avgVolume += vol * weight;
    totalWeight += weight;
    
    if (i < HISTORY_SIZE / 4) {
      recentAvgVolume += vol;
    }
  }
  
  avgVolume /= totalWeight;
  recentAvgVolume /= (HISTORY_SIZE / 4);
  
  // Calculate and smooth dynamic range
  float dynamicRange = maxVolume - minVolume;
  float normalizedRange = constrain(dynamicRange / (maxVolume + 1), 0.0f, 1.0f);
  *smoothedRange = *smoothedRange * (1.0f - responsiveness) + normalizedRange * responsiveness;
  
  // Detect volume changes for flash effect
  static float prevVolume = 0;
  float volumeChange = *smoothedVolume - prevVolume;
  prevVolume = *smoothedVolume;
  
  // IMPROVED: More controlled flash triggering
  if (volumeChange > FLASH_THRESHOLD * prevVolume * flashSensitivity && 
      *smoothedVolume > MIN_VOLUME_THRESHOLD) {
    // ENHANCED: Increase flash intensity for more visible effect
    float newFlash = (volumeChange / max(*smoothedVolume, 1.0f)) * (1.2f / calmness); // Increased multiplier (was 1.0f)
    *flashIntensity = constrain(newFlash, 0.0f, 0.8f); // Increased max flash intensity (was 0.6f)
  } else {
    *flashIntensity *= 0.85f; // Gentler flash decay
  }
  
  // Calculate base color parameters
  uint8_t baseHue = SEGENV.aux0;
  // ENHANCED: Make range visualization more prominent
  uint8_t baseSaturation = map(*smoothedRange * 255, 0, 255, MIN_SATURATION, 255);
  
  // IMPROVED: Much more conservative white blending
  uint8_t saturationReduction = 0;
  if (*smoothedVolume > MIN_WHITE_THRESHOLD) {
    saturationReduction = map_float(*smoothedVolume, MIN_WHITE_THRESHOLD, 255, 0, MAX_SATURATION_REDUCTION);
    saturationReduction = saturationReduction * saturationReduction / 255; // Non-linear reduction
  }
  
  uint8_t saturation = constrain(baseSaturation - saturationReduction, MIN_SATURATION, 255);
  
  // Fill strip with dynamic range visualization
  int centerIdx = SEGLEN / 2;
  uint32_t now = millis();
  
  for (int i = 0; i < SEGLEN; i++) {
    // Calculate position relative to center
    int distFromCenter = abs(i - centerIdx);
    if (distFromCenter > SEGLEN/2) distFromCenter = SEGLEN - distFromCenter;
    float distRatio = (float)distFromCenter / (SEGLEN/2);
    
    // Calculate dynamic color pattern
    float angle = (float)i / SEGLEN * TWO_PI;
    float wave = sin(angle * 3 + now / (1000.0f / (animationSpeed + 0.2f)));
    float wave2 = cos(angle * 2 + now / (1200.0f / animationSpeed));
    float combinedWave = (wave + wave2) * 0.5f;
    
    // Calculate hue variation based on dynamic range
    uint8_t hueOffset;
    if (*smoothedRange < 0.3f) {
      hueOffset = 48 * distRatio + 15 * combinedWave;
    } else {
      hueOffset = (i * 100) / SEGLEN + combinedWave * 25;
    }
    
    uint8_t hue = (baseHue + hueOffset) % 256;
    
    // Calculate brightness based on position and range
    float volRatio = constrain((*smoothedVolume - MIN_VOLUME_THRESHOLD) / (200.0f - MIN_VOLUME_THRESHOLD), 0.0f, 1.0f);
    float baseBrightness = 0.1f + volRatio * 0.5f;
    
    float targetBrightnessFactor = baseBrightness + (0.3f * (1.0f - distRatio) * volRatio);
    
    // ENHANCED: Make flash effect more prominent and react more to volume
    float flashBoost = constrain(*flashIntensity * 1.5f, 0.0f, 1.0f);
    targetBrightnessFactor += flashBoost * (1.0f - distRatio * 0.4f);
    targetBrightnessFactor = constrain(targetBrightnessFactor, 0.0f, 1.0f);

    // Smooth the brightness factor to reduce flicker
    float brightnessSmoothing = 0.20f;
    *smoothedBrightnessFactor = *smoothedBrightnessFactor * (1.0f - brightnessSmoothing) + targetBrightnessFactor * brightnessSmoothing;
    float brightnessFactor = *smoothedBrightnessFactor;
    
    // ENHANCED: Add volume-based boost to brightness to make effect more reactive
    float volumeBoost = constrain((*smoothedVolume - MIN_VOLUME_THRESHOLD) / 120.0f, 0.0f, 0.8f);
    brightnessFactor = min(1.0f, brightnessFactor * (1.0f + volumeBoost));
    
    // Get and modify color
    uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
    uint8_t r = ((color >> 16) & 0xFF) * brightnessFactor;
    uint8_t g = ((color >> 8) & 0xFF) * brightnessFactor;
    uint8_t b = (color & 0xFF) * brightnessFactor;
    
    // Apply very minimal white blending only on extreme volumes
    if (saturationReduction > 0) {
      uint8_t whiteBlend = (saturationReduction * brightnessFactor) / 2;
      r = qadd8(r, whiteBlend);
      g = qadd8(g, whiteBlend);
      b = qadd8(b, whiteBlend);
    }
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  if (DYNAMIC_RANGE_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("DYN-RANGE: Vol=%.1f Smooth=%.1f Range=%.2f Flash=%.2f Sat=%d\n",
                 volume, *smoothedVolume, *smoothedRange, *flashIntensity, saturation);
  }
  
  return FRAMETIME;
} 