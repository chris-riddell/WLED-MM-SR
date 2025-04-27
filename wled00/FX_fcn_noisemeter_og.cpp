/*
 * Noisemeter CE (FX_fcn_noisemeter_og.cpp)
 * 
 * Displays a classic volume meter visualization showing the amplitude of sound 
 * in real-time, similar to traditional VU meters with color variations based on intensity.
 * 
 * Best for: All music types, providing a simple but effective visualization for any 
 * audio content; particularly engaging with dynamic music that has both quiet and 
 * loud sections.
 */

#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include <cmath>

// Configuration
#define VOLUME_HISTORY_SIZE 30  // Reduced from 60 for faster response
#define MIN_VOLUME_THRESHOLD 30.0f
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)
#define DEBUG_NOISEMETER 0

// Fade rate constants
#define MIN_FADE_RATE 0.75f  // Much faster minimum fade (was implicit)
#define MAX_FADE_RATE 0.50f  // Much faster maximum fade
#define BASE_FADE_RATE 0.85f // Base fade rate before speed adjustment

// Helper function for float mapping
static float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

uint16_t mode_noisemeter_og(void) {
  // Get audio data
  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !um_data || !um_data->u_data) {
    // Fallback pattern for no audio
    uint8_t x = millis() / 20;
    SEGMENT.fill(SEGMENT.color_from_palette(x, false, true, 0));
    return FRAMETIME;
  }
  
  // Get volume data
  float volumeSmth = *(float*)um_data->u_data[0];
  
  // Allocate memory for volume history and base volume
  if (!SEGENV.allocateData(sizeof(float) * (VOLUME_HISTORY_SIZE + 2))) {
    return FRAMETIME;
  }
  
  float* volumeHistory = reinterpret_cast<float*>(SEGENV.data);
  float* baseVolume = &volumeHistory[VOLUME_HISTORY_SIZE];
  float* recentPeakVolume = &volumeHistory[VOLUME_HISTORY_SIZE + 1];
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGENV.setUpLeds();   // WLEDMM use lossless getPixelColor()
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
    for (int i = 0; i < VOLUME_HISTORY_SIZE; i++) {
      volumeHistory[i] = volumeSmth;
    }
  }
  
  // Update volume history
  static uint8_t historyIndex = 0;
  volumeHistory[historyIndex] = volumeSmth;
  historyIndex = (historyIndex + 1) % VOLUME_HISTORY_SIZE;
  
  // Calculate recent average and peak volume
  float recentAvgVolume = 0;
  float newPeakVolume = 0;
  
  // Use weighted average for recent volume (more weight to recent samples)
  float totalWeight = 0;
  for (int i = 0; i < VOLUME_HISTORY_SIZE; i++) {
    int idx = (historyIndex - 1 - i + VOLUME_HISTORY_SIZE) % VOLUME_HISTORY_SIZE;
    float weight = (VOLUME_HISTORY_SIZE - i) / (float)VOLUME_HISTORY_SIZE;
    recentAvgVolume += volumeHistory[idx] * weight;
    totalWeight += weight;
    newPeakVolume = max(newPeakVolume, volumeHistory[idx]);
  }
  recentAvgVolume /= totalWeight;
  
  // Update base volume with faster adaptation
  float adaptationRate = 0.02f;  // Increased from implicit value
  *baseVolume = (*baseVolume * (1.0f - adaptationRate)) + (recentAvgVolume * adaptationRate);
  
  // Update peak volume with faster decay
  *recentPeakVolume = max(newPeakVolume, *recentPeakVolume * 0.95f);  // Faster peak decay
  
  // Calculate display level based on current volume relative to base
  float relativeVolume = (volumeSmth - *baseVolume) / (*recentPeakVolume - *baseVolume + 1.0f);
  relativeVolume = constrain(relativeVolume, 0.0f, 1.0f);
  
  // Calculate fade rate based on speed
  uint8_t fadeRate = map(SEGMENT.speed, 0, 255, 220, 254);
  
  // Fade all pixels to black
  for(int i = 0; i < SEGLEN; i++) {
    uint32_t color = SEGMENT.getPixelColor(i);
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Apply fade to black
    r = r * fadeRate / 256;
    g = g * fadeRate / 256;
    b = b * fadeRate / 256;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }

  // Calculate active pixel position based on volume
  float tmpSound2 = volumeSmth * 2.0f;
  int maxLen = map_float(tmpSound2, 0, 255, 0, SEGLEN);
  maxLen = constrain(maxLen, 0, SEGLEN);

  // Set active pixels with color from palette and apply opacity AND intensity
  for (int i = 0; i < maxLen; i++) {
    uint8_t index = inoise8(i*volumeSmth+SEGENV.aux0, SEGENV.aux1+i*volumeSmth);
    uint32_t color = SEGMENT.color_from_palette(index, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply segment opacity AND intensity
    uint8_t brightness_scale = (uint16_t)SEGMENT.opacity * SEGMENT.intensity / 255;
    uint8_t r = ((color >> 16) & 0xFF) * brightness_scale / 255;
    uint8_t g = ((color >> 8) & 0xFF) * brightness_scale / 255;
    uint8_t b = (color & 0xFF) * brightness_scale / 255;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }

  // Update noise coordinates
  SEGENV.aux0 += beatsin8(5, 0, 10);
  SEGENV.aux1 += beatsin8(4, 0, 10);
  
  if (DEBUG_NOISEMETER && SEGENV.call % 120 == 0) {
    Serial.printf("EXP-NOISEMETER: Vol=%.1f MaxLen=%d Fade=%d Opacity=%d\n", 
      volumeSmth, maxLen, fadeRate, SEGMENT.opacity);
  }
  
  return FRAMETIME;
}

// Free memory when segment is deleted or changed
void mode_noisemeter_og_delete(uint8_t segIndex) {
  void* data = strip.getSegment(segIndex).data;
  if (data) free(data);
}