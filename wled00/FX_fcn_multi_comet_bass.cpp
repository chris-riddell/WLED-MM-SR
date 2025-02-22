#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include <cmath>    // for std::isfinite

#define MAX_ACTIVE_COMETS 6  // Maximum number of comets that can be active at once
#define MIN_BEAT_TIME 25
#define FADE_UPDATE_TIME 500
#define DEBUG_MULTI_COMET 1  // Set to 1 to enable debug output
#define DEBUG_BUFFER_SIZE 256  // Size of debug buffer
#define MIN_BASS_THRESHOLD 0.005f  // Lowered minimum bass threshold
#define MAX_TAIL_LENGTH 20   // Increased max tail length to 20 LEDs
#define REQUIRED_AUDIO_CHANNELS 18  // Number of required audio channels (0-17)

// Constants for bass detection
static const float MIN_VOLUME = 40.0f;          // Minimum volume to consider
static const float PEAK_DECAY = 0.95f;          // How fast peak volume decays
static const float MIN_HIT_INTERVAL = 150.0f;   // Minimum ms between hits
static const float ATTACK_RATE = 0.9f;          // How fast intensity rises
static const float DECAY_RATE = 0.8f;           // How fast intensity falls

// Helper function for float mapping (since mapf isn't available)
static float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Helper function to validate float values
static bool isValidFloat(float value) {
  return !std::isnan(value) && std::isfinite(value);  // Only check for NaN and infinity
}

// Helper function to check active comets
static int getActiveComets(uint16_t* comets) {
  int activeComets = 0;
  for (uint8_t i = 0; i < MAX_ACTIVE_COMETS; i++) {
    if (comets[i] < SEGLEN) activeComets++;
  }
  return activeComets;
}

// Helper function to safely check if audio data is available and valid
static bool isAudioDataValid(um_data_t *um_data) {
  if (!um_data || !um_data->u_data) return false;
  
  // Only check volumeSmth as it's the main one we use
  if (!um_data->u_data[0]) return false;
  
  // Validate volumeSmth
  float* volumeSmth = (float*)um_data->u_data[0];
  if (!isValidFloat(*volumeSmth)) return false;
  
  return true;
}

// Simplified bass detection using volume dynamics
float getBassIntensity(um_data_t* um_data, bool newReading) {
  static float lastVolume = 0.0f;
  static float peakVolume = 0.0f;
  static float smoothedBass = 0.0f;
  static uint32_t lastHitTime = 0;
  
  // Get current volume
  float volume = *(float*)um_data->u_data[0];
  float volumeChange = abs(volume - lastVolume);
  
  // Update peak with decay
  peakVolume = max(volume, peakVolume * PEAK_DECAY);
  peakVolume = max(peakVolume, MIN_VOLUME * 2.0f);  // Keep minimum peak
  
  // Determine state
  const char* state = "quiet";
  if (volume < MIN_VOLUME) {
    state = "quiet";
    smoothedBass *= 0.5f;  // Faster decay when quiet
  } else if (volume < peakVolume * 0.4f) {
    state = "low";
  } else if (volumeChange < peakVolume * 0.1f) {
    state = "stable";
  } else {
    state = "active";
  }

  // Detect hits - use Bass Detection slider to adjust sensitivity
  bool isHit = false;
  uint32_t now = millis();
  
  // Map Bass Detection (0-255) to threshold range (0.45-0.15)
  // At default value of 59, this gives us 0.30 (original threshold)
  float dynamicThreshold = map_float(SEGMENT.custom2, 0, 255, 0.45f, 0.15f);
  
  if (volume > MIN_VOLUME && 
      volumeChange > peakVolume * dynamicThreshold &&
      now - lastHitTime > MIN_HIT_INTERVAL) {
    isHit = true;
    lastHitTime = now;
    state = "HIT";
    smoothedBass = 1.0f;
  }

  // Smooth the output
  if (!isHit) {
    if (smoothedBass > 0.0f) {
      smoothedBass *= DECAY_RATE;
    }
    if (smoothedBass < 0.01f) {
      smoothedBass = 0.0f;
    }
  }

  // Update last volume
  lastVolume = volume;

  // Debug output
  if (newReading) {
    Serial.printf("BASS-COMET: Vol=%.1f Chg=%.1f Peak=%.1f Hit=%d Int=%.3f Out=%.3f Thresh=%.3f State=%s\n",
      volume, volumeChange, peakVolume, isHit ? 1 : 0, isHit ? 1.0f : 0.0f, smoothedBass, dynamicThreshold, state);
  }

  return smoothedBass;
}

static bool isFirstPixelActive(uint16_t* comets) {
  for (uint8_t i = 0; i < MAX_ACTIVE_COMETS; i++) {
    if (comets[i] == 0) return true;
  }
  return false;
}

uint16_t mode_multi_comet_bass_ar(void) {
  const uint8_t remappedSpeed = map(SEGMENT.speed, 0, 255, 200, 255);
  const uint32_t cycleTime = (uint32_t)((255 - remappedSpeed) * 1.5);
  const uint32_t it = strip.now / cycleTime;

  if (!SEGENV.allocateData(sizeof(uint16_t) * MAX_ACTIVE_COMETS)) return FX_MODE_STATIC;
  uint16_t* comets = reinterpret_cast<uint16_t*>(SEGENV.data);

  static uint8_t fadeRate = 180;
  static uint32_t lastBeat = 0;
  static uint32_t lastDebugTime = 0;

  // Map tail length from 0-255 to 1-20
  const uint8_t tailLength = map(SEGMENT.custom1, 0, 255, 1, MAX_TAIL_LENGTH);
  const uint8_t baseFade = map(tailLength, 1, MAX_TAIL_LENGTH, 250, 180);

  if (SEGENV.call == 0) {
    SEGMENT.fill(SEGCOLOR(1));
    for (uint8_t i = 0; i < MAX_ACTIVE_COMETS; i++) comets[i] = SEGLEN;
    fadeRate = baseFade;
    SEGENV.aux0 = 0;
    lastBeat = 0;
    SEGMENT.custom2 = SEGMENT.custom1;
  }

  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !um_data) return FRAMETIME;

  float bassIntensity = getBassIntensity(um_data, DEBUG_MULTI_COMET);

  // Map Bass Detection (0-255) to trigger threshold (0.45-0.15)
  float triggerThreshold = map_float(SEGMENT.custom2, 0, 255, 0.45f, 0.15f);
  
  // Create new comet on bass hits with dynamic threshold
  if (bassIntensity > triggerThreshold) {
    uint32_t timeSinceLastBeat = strip.now - lastBeat;
    if (timeSinceLastBeat > MIN_BEAT_TIME && !isFirstPixelActive(comets)) {
      lastBeat = strip.now;
      // Only create one comet
      for (uint8_t i = 0; i < MAX_ACTIVE_COMETS; i++) {
        if (comets[i] >= SEGLEN) {
          comets[i] = 0;
          break;  // Exit after creating one comet
        }
      }
    }
  }

  // Update comet visuals
  SEGMENT.fadeToBlackBy(baseFade);

  for (uint8_t i = 0; i < MAX_ACTIVE_COMETS; i++) {
    if (comets[i] < SEGLEN) {
      uint16_t headIndex = comets[i];
      int16_t tailStart = (headIndex >= tailLength) ? headIndex - (tailLength - 1) : 0;
      
      // Clear old tail end
      if (tailStart > 0) SEGMENT.setPixelColor(tailStart - 1, 0);

      // Draw tail
      for (int16_t t = tailStart; t <= headIndex && t < SEGLEN; t++) {
        float brightness = 1.0f - (float)(headIndex - t) / tailLength;
        uint32_t color = SEGCOLOR(2) != 0 && (i % 2) ? 
                        SEGCOLOR(2) : SEGMENT.color_from_palette(t, true, false, 0);
        
        SEGMENT.setPixelColor(t, 
          (uint8_t)((color >> 16) * brightness),
          (uint8_t)(((color >> 8) & 0xFF) * brightness),
          (uint8_t)((color & 0xFF) * brightness));
      }

      if (++comets[i] >= SEGLEN) comets[i] = SEGLEN;
    }
  }

  return cycleTime;
} 