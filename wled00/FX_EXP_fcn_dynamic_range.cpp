#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include <cmath>

#define HISTORY_SIZE 24  // Frames of volume history to track
#define MIN_VOLUME_THRESHOLD 20.0f
#define FLASH_THRESHOLD 0.5f  // Relative threshold for flash effect
#define DYNAMIC_RANGE_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

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
  
  // Allocate memory for volume history
  if (!SEGENV.allocateData(sizeof(float) * HISTORY_SIZE)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  float* volumeHistory = reinterpret_cast<float*>(SEGENV.data);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
    
    // Initialize volume history
    for (int i = 0; i < HISTORY_SIZE; i++) {
      volumeHistory[i] = 0;
    }
  }
  
  // Speed controls color change rate and effect responsiveness
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, 1, 3);
  float responsiveness = map_float(SEGMENT.speed, 0, 255, 0.05f, 0.2f);
  
  // Sensitivity affects minimum volume threshold
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 80.0f, MIN_VOLUME_THRESHOLD);
  
  // Update color movement counter
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  
  // Update volume history
  static uint8_t historyIndex = 0;
  volumeHistory[historyIndex] = volume;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  
  // Calculate min, max, and average volume over history
  float minVolume = 255.0f;
  float maxVolume = 0.0f;
  float avgVolume = 0.0f;
  
  for (int i = 0; i < HISTORY_SIZE; i++) {
    minVolume = min(minVolume, volumeHistory[i]);
    maxVolume = max(maxVolume, volumeHistory[i]);
    avgVolume += volumeHistory[i];
  }
  
  avgVolume /= HISTORY_SIZE;
  
  // Calculate dynamic range and normalize
  float dynamicRange = maxVolume - minVolume;
  float normalizedRange = constrain(dynamicRange / 128.0f, 0.0f, 1.0f);
  
  // Calculate volume relative to its recent history
  float relativeVolume = 0.0f;
  if (maxVolume > minVolume) {
    relativeVolume = (volume - minVolume) / (maxVolume - minVolume);
  }
  
  // Detect sudden volume changes for flash effect
  static float prevVolume = 0;
  float volumeChange = volume - prevVolume;
  bool triggerFlash = (volumeChange > FLASH_THRESHOLD * maxVolume) && (volume > volumeThreshold);
  
  // Store current values for next frame
  prevVolume = volume;
  
  // Calculate saturation based on dynamic range
  uint8_t saturation = 255 * normalizedRange;
  
  // Apply base color based on relative volume
  uint8_t baseHue = SEGENV.aux0;
  uint8_t baseBrightness = constrain(volume, 0, 255);
  
  // Apply dynamic range visualization
  for (int i = 0; i < SEGLEN; i++) {
    // Calculate position-based hue offset
    uint8_t posOffset = (i * 256) / SEGLEN;
    uint8_t hue = (baseHue + posOffset) % 256;
    
    // Apply dynamic range to saturation
    uint8_t brightness = map(i, 0, SEGLEN - 1, baseBrightness, baseBrightness * normalizedRange);
    
    // Get color from palette with modified parameters
    uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply saturation adjustment (shift toward white when range is small)
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Calculate white amount (inverse of saturation)
    uint8_t whiteAmount = 255 - saturation;
    
    // Blend with white based on saturation
    r = r + ((255 - r) * whiteAmount) / 255;
    g = g + ((255 - g) * whiteAmount) / 255;
    b = b + ((255 - b) * whiteAmount) / 255;
    
    // Scale by brightness
    r = (r * brightness) / 255;
    g = (g * brightness) / 255;
    b = (b * brightness) / 255;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  // Apply flash effect if triggered
  if (triggerFlash) {
    // Flash brightness scales with volume change
    float flashBrightness = min(255.0f, volumeChange * 2.0f);
    
    // Create white flash
    for (int i = 0; i < SEGLEN; i++) {
      uint32_t color = SEGMENT.getPixelColor(i);
      uint8_t r = (color >> 16) & 0xFF;
      uint8_t g = (color >> 8) & 0xFF;
      uint8_t b = color & 0xFF;
      
      // Add flash intensity to each color channel
      r = min(255, r + (int)flashBrightness);
      g = min(255, g + (int)flashBrightness);
      b = min(255, b + (int)flashBrightness);
      
      SEGMENT.setPixelColor(i, r, g, b);
    }
  }
  
  // Debug output
  if (DYNAMIC_RANGE_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("DYNAMIC_RANGE: Vol=%.1f Range=%.1f NormRange=%.2f RelVol=%.2f Sat=%d Flash=%d\n",
      volume, dynamicRange, normalizedRange, relativeVolume, saturation, triggerFlash ? 1 : 0);
  }
  
  return FRAMETIME;
} 