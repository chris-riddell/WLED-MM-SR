#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>
#include <algorithm>  // for std::min

// IMPROVED: Lowered threshold and increased responsiveness
#define MIN_RIPPLE_THRESHOLD 30.0f  // Was 40.0f - even more sensitive
#define MAX_RIPPLES 12  // Increased from 10 to allow more ripples
#define RIPPLE_FADE_RATE 0.985f  // Faster fade rate (was 0.996f) for quicker response
#define VOLUME_RIPPLES_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)
// IMPROVED: Define constants for better scaling of ripples
#define MIN_SIZE_SCALE 0.8f  // Increased from 0.6f for better strip utilization
#define MAX_SIZE_SCALE 1.6f  // Increased from 1.4f for better strip utilization
// NEW: Constants for reverse ripples
#define REVERSE_RIPPLE_CHANCE 30  // Increased from 20
#define MAX_REVERSE_RIPPLES 4     // Increased from 3

// Forward declaration of required helper functions 
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);

struct Ripple {
  float position;      // Center position (0-1 range from center)
  float size;          // Current size
  float brightness;    // Current brightness
  float speed;         // Expansion speed
  uint8_t colorIndex;  // Color index from palette
  bool active;         // Whether this ripple is currently active
  float width;         // Variable width for different ripple styles
  float maxSize;       // Track maximum size per ripple
  bool reverse;        // NEW: Whether this ripple moves inward from outward
  float startPoint;    // NEW: Starting point for the ripple (0.0 = bottom, 1.0 = top)
  
  Ripple() : position(0), size(0), brightness(0), speed(0), colorIndex(0), active(false), 
            width(0.05f), maxSize(0), reverse(false), startPoint(0) {}
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
  if (!SEGENV.allocateData(sizeof(Ripple) * MAX_RIPPLES + sizeof(float) + sizeof(uint8_t))) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  Ripple* ripples = reinterpret_cast<Ripple*>(SEGENV.data);
  // Add storage for smoothed volume at the end of data block
  float* smoothedVolume = reinterpret_cast<float*>(SEGENV.data + sizeof(Ripple) * MAX_RIPPLES);
  // NEW: Track current number of reverse ripples
  uint8_t* reverseRippleCount = reinterpret_cast<uint8_t*>(SEGENV.data + sizeof(Ripple) * MAX_RIPPLES + sizeof(float));
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
    *smoothedVolume = 0; // Initialize smoothed volume
    *reverseRippleCount = 0; // Initialize reverse ripple counter
    
    // Initialize ripples
    for (int i = 0; i < MAX_RIPPLES; i++) {
      ripples[i].active = false;
    }
  }
  
  // IMPROVED: Faster volume smoothing for better response
  *smoothedVolume = *smoothedVolume * 0.7f + volume * 0.3f;  // Was 0.8/0.2
  
  // IMPROVED: Speed now controls ripple expansion rate and fade speed with wider range
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, 1, 8);  // Reduced from 2-12 for slower movement
  float expansionRate = map_float(SEGMENT.speed, 0, 255, 0.002f, 0.1f);  // Much lower minimum for slower movement
  float fadeRateAdjustment = map_float(SEGMENT.speed, 0, 255, 0.998f, 0.975f); // Slower minimum fade
  
  // IMPROVED: Intensity controls volume threshold for new ripples
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 140.0f, MIN_RIPPLE_THRESHOLD);
  
  // NEW: Custom1 controls the chance of creating reverse ripples
  uint8_t reverseRippleChance = map(SEGMENT.custom1, 0, 255, 0, REVERSE_RIPPLE_CHANCE);
  
  // NEW: Custom2 controls the starting position range for reverse ripples
  float reverseStartMinPos = map_float(SEGMENT.custom2, 0, 255, 0.9f, 0.5f);
  
  // Update color rotation
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  uint8_t baseColorIndex = SEGENV.aux0;
  
  // Create a new ripple on volume peaks
  static uint32_t lastRippleTime = 0;
  uint32_t currentTime = millis();  // Get current time
  bool createRipple = false;
  bool createReverseRipple = false;
  
  // Check for sudden volume increase or sample peak
  static float prevVolume = 0;
  float volumeChange = *smoothedVolume - prevVolume;
  
  // IMPROVED: More responsive ripple creation with better volume scaling
  if ((*smoothedVolume > volumeThreshold && volumeChange > 3.0f) || samplePeak) {  // Reduced from 4.0f
    // Add timing restrictions based on volume and speed
    uint16_t minRippleInterval = map(255 - SEGMENT.speed, 0, 255, 25, 200);  // Shorter intervals
    
    // Higher volumes = more frequent ripples allowed
    if (*smoothedVolume > 160) minRippleInterval = max(15, minRippleInterval / 2);
    
    if (currentTime - lastRippleTime > minRippleInterval) {
      createRipple = true;
      
      if (*reverseRippleCount < MAX_REVERSE_RIPPLES && random8() < reverseRippleChance) {
        createReverseRipple = true;
      }
      
      lastRippleTime = currentTime;
      
      // Debug ripple creation
      if (VOLUME_RIPPLES_DEBUG && SEGENV.call % 8 == 0) {
        Serial.printf("Creating ripple: vol=%.1f change=%.1f thresh=%.1f reverse=%d\n", 
                      *smoothedVolume, volumeChange, volumeThreshold, createReverseRipple ? 1 : 0);
      }
    }
  }
  
  prevVolume = *smoothedVolume;
  
  // Count active reverse ripples for tracking purposes
  *reverseRippleCount = 0;
  for (int i = 0; i < MAX_RIPPLES; i++) {
    if (ripples[i].active && ripples[i].reverse) {
      (*reverseRippleCount)++;
    }
  }
  
  // IMPROVED: Create new ripple if needed with enhanced properties
  if (createRipple) {
    // Find a free ripple slot
    for (int i = 0; i < MAX_RIPPLES; i++) {
      if (!ripples[i].active) {
        ripples[i].active = true;
        
        if (createReverseRipple) {
          ripples[i].reverse = true;
          ripples[i].startPoint = random8(100) / 100.0f * (1.0f - reverseStartMinPos) + reverseStartMinPos;
          ripples[i].size = 0.0f;
          (*reverseRippleCount)++;
        } else {
          ripples[i].reverse = false;
          ripples[i].startPoint = 0.0f;
          ripples[i].size = 0.0f;
        }
        
        // IMPROVED: Scale brightness by volume with higher minimum
        float volumeRatio = (*smoothedVolume - volumeThreshold) / (255.0f - volumeThreshold);
        volumeRatio = constrain(volumeRatio, 0.0f, 1.0f);
        ripples[i].brightness = 200.0f + (volumeRatio * 55.0f);  // Range 200-255 for better visibility
        
        // IMPROVED: Speed affected by volume with more impact
        ripples[i].speed = expansionRate * (0.8f + (volumeRatio * 1.8f));  // More variation
        
        // IMPROVED: Different color per ripple with more variety
        ripples[i].colorIndex = (baseColorIndex + random8(96)) % 256; // Less color variety for cohesion
        
        // IMPROVED: Randomize ripple width for visual variety
        ripples[i].width = 0.04f + (random8(4) / 100.0f);  // Width between 0.04-0.08
        
        // IMPROVED: Set maximum size based on speed and volume with better strip utilization
        float sizeScale = map_float(SEGMENT.speed, 0, 255, MIN_SIZE_SCALE, MAX_SIZE_SCALE);
        ripples[i].maxSize = constrain(sizeScale + (volumeRatio * 0.6f), MIN_SIZE_SCALE, MAX_SIZE_SCALE);  // Increased volume impact
        
        break;
      }
    }
  }
  
  // Clear the segment for redrawing
  SEGMENT.fill(BLACK);
  
  // IMPROVED: Update and draw all active ripples with better visual quality
  for (int i = 0; i < MAX_RIPPLES; i++) {
    if (!ripples[i].active) continue;
    
    // Update ripple size and brightness with speed-dependent fade
    ripples[i].size += ripples[i].speed;
    ripples[i].brightness *= fadeRateAdjustment;
    
    // IMPROVED: Make ripples inactive when they get too big or too dim
    if (ripples[i].size > ripples[i].maxSize || ripples[i].brightness < 40) {  // Higher cutoff
      ripples[i].active = false;
      if (ripples[i].reverse) {
        (*reverseRippleCount) = max(0, (*reverseRippleCount) - 1);
      }
      continue;
    }
    
    // Calculate ripple position and bounds based on direction
    int rippleStart, rippleEnd;
    
    if (ripples[i].reverse) {
      float normalizedPosition = ripples[i].size / ripples[i].maxSize;
      float currentPosition = ripples[i].startPoint * (1.0f - normalizedPosition);
      
      rippleStart = round(currentPosition * SEGLEN);
      rippleEnd = round((currentPosition + 0.2f) * SEGLEN); // Wider ripple
      
      rippleStart = max(0, rippleStart);
      rippleEnd = min(SEGLEN - 1, rippleEnd);
    } else {
      rippleStart = 0;
      // IMPROVED: Better strip utilization by using maxSize directly
      rippleEnd = round(ripples[i].size * SEGLEN);  // Removed MIN_SIZE_SCALE division
      rippleEnd = min(SEGLEN - 1, rippleEnd);
    }
    
    // Get ripple color
    uint32_t rippleColor = SEGMENT.color_from_palette(ripples[i].colorIndex, false, PALETTE_SOLID_WRAP, 0);
    
    // Draw ripple with enhanced brightness gradient
    for (int j = rippleStart; j <= rippleEnd; j++) {
      float posInRipple;
      
      if (ripples[i].reverse) {
        float relativePosition = (float)(j - rippleStart) / max(1, rippleEnd - rippleStart);
        posInRipple = 1.0f - relativePosition;
      } else {
        posInRipple = (float)j / rippleEnd;
      }
      
      float distFromLeadingEdge;
      
      if (ripples[i].reverse) {
        distFromLeadingEdge = abs(posInRipple - 0.05f);
      } else {
        distFromLeadingEdge = abs(posInRipple - (ripples[i].size / 1.05f));
      }
      
      float relativePos = distFromLeadingEdge / ripples[i].width;
      relativePos = constrain(relativePos, 0.0f, 1.0f);
      
      // IMPROVED: Sharper ripple definition
      float pixelBrightness = ripples[i].brightness * exp(-relativePos * relativePos * 5.0f);
      
      // Apply brightness to color using built-in color_fade
      uint32_t color = color_fade(rippleColor, pixelBrightness);
      
      // Add to existing pixel color for blending
      SEGMENT.addPixelColor(j, color);
    }
  }
  
  // Debug output
  if (VOLUME_RIPPLES_DEBUG && SEGENV.call % 32 == 0) {
    int activeCount = 0;
    int reverseCount = 0;
    float maxRippleSize = 0;
    for (int i = 0; i < MAX_RIPPLES; i++) {
      if (ripples[i].active) {
        activeCount++;
        maxRippleSize = max(maxRippleSize, ripples[i].size);
        if (ripples[i].reverse) reverseCount++;
      }
    }
    
    Serial.printf("EXP-VOLUME-RIPPLES: Vol=%.1f SmoothVol=%.1f Peak=%d Rip=%d Rev=%d MaxSize=%.2f\n",
      volume, *smoothedVolume, samplePeak, activeCount, reverseCount, maxRippleSize);
  }
  
  return FRAMETIME;
} 