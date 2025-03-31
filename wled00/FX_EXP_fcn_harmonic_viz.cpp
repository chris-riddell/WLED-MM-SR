#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>

// Increased history size for smoother transitions
#define TONALITY_HISTORY_SIZE 20  // Increased from 10
#define MIN_VOLUME_THRESHOLD 40.0f  // Increased from 30.0f
#define HARMONIC_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);
extern float detectTonality(um_data_t *um_data);

uint16_t mode_harmonic_viz(void) {
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
  
  // Allocate memory for tonality history and smoothing state
  if (!SEGENV.allocateData(sizeof(int) * TONALITY_HISTORY_SIZE + sizeof(float) * 3)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  int* tonalityHistory = reinterpret_cast<int*>(SEGENV.data);
  // Store smoothed tonality value and last detected tonality at the end
  float* smoothedTonality = reinterpret_cast<float*>(SEGENV.data + sizeof(int) * TONALITY_HISTORY_SIZE);
  float* lastTonality = smoothedTonality + 1;
  float* smoothedBrightness = lastTonality + 1; // Store smoothed brightness
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
    *smoothedTonality = 0.0f;  // Start with neutral tonality
    *lastTonality = 0.0f;
    *smoothedBrightness = 100.0f; // Initial smoothed brightness
    
    // Initialize tonality history
    for (int i = 0; i < TONALITY_HISTORY_SIZE; i++) {
      tonalityHistory[i] = 0;
    }
  }
  
  // Speed controls color change rate, animation speed, and wave dynamics
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, 1, 10);  // Expanded range from 1-6 to 1-10
  float animationSpeed = map_float(SEGMENT.speed, 0, 255, 0.3f, 4.0f); // Wider animation speed range
  float waveFrequency = map_float(SEGMENT.speed, 0, 255, 0.8f, 6.0f); // Expanded wave frequency range
  
  // Intensity now controls "calmness" - higher value = more calm, gradual transitions
  float calmness = map_float(SEGMENT.intensity, 0, 255, 0.2f, 1.5f);
  float smoothingFactor = 0.05f + (calmness * 0.15f); // More smoothing at higher intensities
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 30.0f, 120.0f); // Upper limit increased
  
  // Update color movement counter at variable speed
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  
  // Only detect tonality if volume is sufficient
  static uint8_t historyIndex = 0;
  
  if (volume > volumeThreshold) {
    // Detect tonality
    float tonality = detectTonality(um_data);
    
    // Add hysteresis to prevent rapid fluctuations
    float tonalityDiff = abs(tonality - *lastTonality);
    if (tonalityDiff > 0.5f) {
      // Only update if change is significant
      *lastTonality = tonality;
      
      // Update history
      tonalityHistory[historyIndex] = tonality;
      historyIndex = (historyIndex + 1) % TONALITY_HISTORY_SIZE;
    }
  }
  
  // Calculate dominant tonality over history with weighted voting
  int majorCount = 0;
  int minorCount = 0;
  int neutralCount = 0;
  
  // Use weighted voting where recent values count more
  for (int i = 0; i < TONALITY_HISTORY_SIZE; i++) {
    // Circular buffer - find actual index relative to current
    int actualIndex = (historyIndex - 1 - i + TONALITY_HISTORY_SIZE) % TONALITY_HISTORY_SIZE;
    float weight = (TONALITY_HISTORY_SIZE - i) / (float)TONALITY_HISTORY_SIZE;
    
    if (tonalityHistory[actualIndex] > 0) majorCount += weight * 10;
    else if (tonalityHistory[actualIndex] < 0) minorCount += weight * 10;
    else neutralCount += weight * 10;
  }
  
  // Determine dominant tonality
  int dominantTonality = 0;
  if (majorCount > minorCount && majorCount > neutralCount) {
    dominantTonality = 1;  // Major
  } else if (minorCount > majorCount && minorCount > neutralCount) {
    dominantTonality = -1;  // Minor
  }
  
  // Smooth tonality transitions - approach dominant tonality gradually
  float targetTonality = (float)dominantTonality;
  *smoothedTonality = *smoothedTonality * (1.0f - smoothingFactor) + targetTonality * smoothingFactor;
  
  // Calculate a smoothed tonality factor (-1.0 to 1.0)
  float tonalityFactor = *smoothedTonality;
  
  // Set up color schemes based on tonality
  uint8_t baseHue = SEGENV.aux0;
  uint8_t saturation;
  // Cap maximum brightness to prevent white flashes
  uint8_t targetBrightness = map(volume, volumeThreshold, 255, 80, 230); // Slightly reduced min, increased max
  
  // Adjust color scheme based on smoothed tonality
  if (tonalityFactor > 0) {
    // Major: Bright complementary colors (analogous)
    // Linearly interpolate saturation based on how "major" it is
    saturation = 220 - (tonalityFactor * 70);  // Less saturation as it becomes more major
  } else if (tonalityFactor < 0) {
    // Minor: Deeper, more saturated colors
    saturation = 180 + (abs(tonalityFactor) * 75);  // More saturation as it becomes more minor
    // Shift base hue based on minor factor
    baseHue = (baseHue + (int)(abs(tonalityFactor) * 128)) % 256;
  } else {
    // Neutral: Moderate saturation
    saturation = 150;
  }
  
  // Scale brightness by calmness but ensure it never goes above 230
  targetBrightness = constrain(80 + (targetBrightness - 80) * (1.0f + (1.0f / calmness)), 0, 230);
  
  // SMOOTH brightness transitions
  float brightnessSmoothingFactor = 0.10f / calmness; // Slower smoothing, adjustable by calmness
  *smoothedBrightness = *smoothedBrightness * (1.0f - brightnessSmoothingFactor) + 
                         targetBrightness * brightnessSmoothingFactor;
  uint8_t brightness = constrain(*smoothedBrightness, 0, 255);
  
  // Get current time for animations
  uint32_t now = millis();
  
  // Visualize with flowing pattern optimized for circular display
  for (int i = 0; i < SEGLEN; i++) {
    // For circular layout, calculate radial position (0 = center, 1 = edge)
    int halfLength = SEGLEN / 2;
    int distFromCenter = abs(i - halfLength);
    if (distFromCenter > halfLength) distFromCenter = SEGLEN - distFromCenter;
    float radialPos = (float)distFromCenter / halfLength;
    
    // Angle around the circle (0-360 degrees)
    float angle = (float)i / SEGLEN * TWO_PI;
    
    // Create waves based on radial position and angle
    float wave1 = sin(radialPos * TWO_PI * waveFrequency + now / (1000.0f / animationSpeed));
    float wave2 = cos(angle * 3 + now / (2000.0f / animationSpeed));
    float combinedWave = (wave1 + wave2) / 2.0f;
    
    // Adjust hue based on position, tonality, and waves
    float hueShift;
    if (tonalityFactor > 0) {
      // Major: gentle hue shifts with rainbow pattern
      hueShift = 30.0f * combinedWave * (1.0f + tonalityFactor);
    } else if (tonalityFactor < 0) {
      // Minor: sharper hue contrasts
      hueShift = 60.0f * combinedWave * (1.0f + abs(tonalityFactor));
    } else {
      // Neutral: moderate shifts
      hueShift = 45.0f * combinedWave;
    }
    
    // Calculate final hue with smoother transitions
    uint8_t hue = (baseHue + (int)hueShift) % 256;
    
    // FIXED: Adjust brightness based on volume - now increases with volume
    float waveBrightness = brightness * (0.7f + 0.3f * combinedWave);
    
    // Add circular pattern - brighter in center for major, brighter at edges for minor
    if (tonalityFactor > 0) {
      // Major: brighter in center (happy, expansive)
      waveBrightness *= (1.0f - radialPos * 0.3f * tonalityFactor);
    } else if (tonalityFactor < 0) {
      // Minor: brighter at edges (dark, moody)
      waveBrightness *= (1.0f + radialPos * 0.3f * abs(tonalityFactor));
    }
    
    // Get color from palette 
    uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply saturation and brightness adjustments
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Apply saturation (blend with gray of same brightness)
    uint8_t gray = (r + g + b) / 3;
    r = r + ((gray - r) * (255 - saturation)) / 255;
    g = g + ((gray - g) * (255 - saturation)) / 255;
    b = b + ((gray - b) * (255 - saturation)) / 255;
    
    // Apply brightness
    r = (r * waveBrightness) / 255;
    g = (g * waveBrightness) / 255;
    b = (b * waveBrightness) / 255;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  // Debug output
  if (HARMONIC_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("EXP-HARMONIC-VIZ: Vol=%.1f Tonality=%.2f (Maj:%d Min:%d Neu:%d) Sat=%d Calm=%.2f\n",
      volume, tonalityFactor, majorCount/10, minorCount/10, neutralCount/10, saturation, calmness);
  }
  
  return FRAMETIME;
}