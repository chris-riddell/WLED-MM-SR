#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>

#define MIN_VOLUME_THRESHOLD 30.0f
#define CENTROID_HISTORY_SIZE 8
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
  
  // Allocate memory for centroid history
  if (!SEGENV.allocateData(sizeof(float) * CENTROID_HISTORY_SIZE)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  float* centroidHistory = reinterpret_cast<float*>(SEGENV.data);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    
    // Initialize centroid history
    for (int i = 0; i < CENTROID_HISTORY_SIZE; i++) {
      centroidHistory[i] = 0.5f;  // Middle value
    }
  }
  
  // Speed controls effect responsiveness
  float responsiveness = map_float(SEGMENT.speed, 0, 255, 0.05f, 0.3f);
  
  // Sensitivity affects minimum volume threshold
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 80.0f, MIN_VOLUME_THRESHOLD);
  
  // Only update centroid if volume is sufficient
  static uint8_t historyIndex = 0;
  
  if (volume > volumeThreshold) {
    // Calculate spectral centroid
    float centroid = calculateSpectralCentroid(fftData, 16);  // Use 16 FFT bins
    
    // Update history
    centroidHistory[historyIndex] = centroid;
    historyIndex = (historyIndex + 1) % CENTROID_HISTORY_SIZE;
  }
  
  // Calculate average centroid from history for stability
  float avgCentroid = 0;
  for (int i = 0; i < CENTROID_HISTORY_SIZE; i++) {
    avgCentroid += centroidHistory[i];
  }
  avgCentroid /= CENTROID_HISTORY_SIZE;
  
  // Map spectral centroid to color temperature
  // Lower centroid (more bass) = cool/blue colors
  // Higher centroid (more treble) = warm/orange colors
  uint8_t warmth = 255 * avgCentroid;  // 0 = cool, 255 = warm
  
  // Use smoothed centroid to determine color temperature
  for (int i = 0; i < SEGLEN; i++) {
    // Determine color based on centroid (temperature)
    uint32_t color;
    
    if (SEGMENT.palette) {
      // Use palette if specified, with position controlled by warmth
      uint8_t index = map(i, 0, SEGLEN - 1, warmth, 255 - warmth);
      color = SEGMENT.color_from_palette(index, false, PALETTE_SOLID_WRAP, 0);
    } else {
      // Generate color temperature directly
      uint8_t r, g, b;
      
      if (warmth < 128) {
        // Cool colors (blue to cyan)
        float cool = warmth / 128.0f;
        r = 0;
        g = 255 * cool;
        b = 255;
      } else {
        // Warm colors (cyan to orange/red)
        float warm = (warmth - 128) / 127.0f;
        r = 255 * warm;
        g = 255 * (1.0f - warm * 0.5f);
        b = 255 * (1.0f - warm);
      }
      
      // Apply brightness based on volume
      uint8_t brightness = map(volume, volumeThreshold, 255, 64, 255);
      r = (r * brightness) / 255;
      g = (g * brightness) / 255;
      b = (b * brightness) / 255;
      
      color = (r << 16) | (g << 8) | b;
    }
    
    // Apply positional effects based on segment length
    float posRatio = (float)i / SEGLEN;
    float posEffect = sin(posRatio * PI); // 0->1->0 across segment
    
    // Brighten the center when bass heavy (low centroid)
    // Brighten the edges when treble heavy (high centroid)
    float brightness = 0.7f + 0.3f * (avgCentroid < 0.5f ? posEffect : (1.0f - posEffect));
    
    // Apply brightness modulation
    uint8_t r = ((color >> 16) & 0xFF) * brightness;
    uint8_t g = ((color >> 8) & 0xFF) * brightness;
    uint8_t b = (color & 0xFF) * brightness;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  // Debug output
  if (CENTROID_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("SPECTRAL_CENTROID: Vol=%.1f Centroid=%.2f Warmth=%d\n",
      volume, avgCentroid, warmth);
  }
  
  return FRAMETIME;
} 