#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>

#define MIN_RIPPLE_THRESHOLD 30.0f
#define MAX_RIPPLES 8
#define RIPPLE_FADE_RATE 0.95f
#define VOLUME_RIPPLES_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

struct Ripple {
  float position;      // Center position (0-1 range from center)
  float size;          // Current size
  float brightness;    // Current brightness
  float speed;         // Expansion speed
  uint8_t colorIndex;  // Color index from palette
  bool active;         // Whether this ripple is currently active
  
  Ripple() : position(0), size(0), brightness(0), speed(0), colorIndex(0), active(false) {}
};

uint16_t mode_volume_ripples(void) {
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
  uint8_t samplePeak = um_data->u_data[3] ? *(uint8_t*)um_data->u_data[3] : 0;
  
  // Allocate memory for ripple array in SEGENV.data
  if (!SEGENV.allocateData(sizeof(Ripple) * MAX_RIPPLES)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  Ripple* ripples = reinterpret_cast<Ripple*>(SEGENV.data);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
    
    // Initialize ripples
    for (int i = 0; i < MAX_RIPPLES; i++) {
      ripples[i].active = false;
    }
  }
  
  // Speed controls color change and ripple expansion rate
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, 1, 5);
  float expansionRate = map_float(SEGMENT.speed, 0, 255, 0.02f, 0.07f);
  
  // Sensitivity controls volume threshold for new ripples
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 100.0f, MIN_RIPPLE_THRESHOLD);
  
  // Update color rotation
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  uint8_t baseColorIndex = SEGENV.aux0;
  
  // Create a new ripple on volume peaks
  static uint32_t lastRippleTime = 0;
  uint32_t now = millis();
  bool createRipple = false;
  
  // Check for sudden volume increase or sample peak
  static float prevVolume = 0;
  float volumeChange = volume - prevVolume;
  
  if ((volume > volumeThreshold && volumeChange > 5.0f) || samplePeak) {
    // Add timing restrictions based on sensitivity
    uint16_t minRippleInterval = map(255 - SEGMENT.intensity, 0, 255, 50, 250);
    
    if (now - lastRippleTime > minRippleInterval) {
      createRipple = true;
      lastRippleTime = now;
    }
  }
  
  prevVolume = volume;
  
  // Create new ripple if needed
  if (createRipple) {
    // Find a free ripple slot
    for (int i = 0; i < MAX_RIPPLES; i++) {
      if (!ripples[i].active) {
        ripples[i].active = true;
        ripples[i].position = 0.0f;  // Start at center
        ripples[i].size = 0.0f;
        ripples[i].brightness = map_float(volume, volumeThreshold, 255.0f, 128.0f, 255.0f);
        ripples[i].speed = expansionRate * (0.5f + (volume / 512.0f));  // Speed affected by volume
        ripples[i].colorIndex = (baseColorIndex + (i * 32)) % 256;  // Different color per ripple
        break;
      }
    }
  }
  
  // Clear the segment for redrawing
  SEGMENT.fill(BLACK);
  
  // Update and draw all active ripples
  for (int i = 0; i < MAX_RIPPLES; i++) {
    if (!ripples[i].active) continue;
    
    // Update ripple properties
    ripples[i].size += ripples[i].speed;
    
    // Fade brightness as the ripple expands
    ripples[i].brightness *= RIPPLE_FADE_RATE;
    
    // Deactivate ripple when it gets too big or too dim
    if (ripples[i].size >= 1.0f || ripples[i].brightness < 20.0f) {
      ripples[i].active = false;
      continue;
    }
    
    // Draw the ripple
    float halfLen = SEGLEN / 2.0f;
    int innerPos = round(halfLen - (ripples[i].size * halfLen));
    int outerPos = round(halfLen + (ripples[i].size * halfLen));
    
    // Clamp positions to segment bounds
    innerPos = max(0, innerPos);
    outerPos = min(SEGLEN - 1, outerPos);
    
    // Get ripple color
    uint32_t rippleColor = SEGMENT.color_from_palette(ripples[i].colorIndex, false, PALETTE_SOLID_WRAP, 0);
    
    // Draw ripple with brightness gradient
    for (int j = innerPos; j <= outerPos; j++) {
      // Calculate distance from center point (0.0-1.0)
      float distFromCenter = abs((j - halfLen) / halfLen);
      
      // Calculate position relative to ripple width
      float relativePos = abs(distFromCenter - ripples[i].size) / 0.05f;  // 0.05f controls thickness
      relativePos = constrain(relativePos, 0.0f, 1.0f);
      
      // Calculate pixel brightness using gaussian-like curve
      float pixelBrightness = ripples[i].brightness * (1.0f - (relativePos * relativePos));
      
      // Apply brightness to color
      uint8_t r = ((rippleColor >> 16) & 0xFF) * pixelBrightness / 255;
      uint8_t g = ((rippleColor >> 8) & 0xFF) * pixelBrightness / 255;
      uint8_t b = (rippleColor & 0xFF) * pixelBrightness / 255;
      
      uint32_t color = (r << 16) | (g << 8) | b;
      
      // Add to existing pixel color for blending multiple ripples
      SEGMENT.addPixelColor(j, color);
    }
  }
  
  // Debug output
  if (VOLUME_RIPPLES_DEBUG && SEGENV.call % 32 == 0) {
    int activeCount = 0;
    for (int i = 0; i < MAX_RIPPLES; i++) {
      if (ripples[i].active) activeCount++;
    }
    
    Serial.printf("VOLUME_RIPPLES: Vol=%.1f Peak=%d Rip=%d Spd=%d Sens=%d\n",
      volume, samplePeak, activeCount, SEGMENT.speed, SEGMENT.intensity);
  }
  
  return FRAMETIME;
} 