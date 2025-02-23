#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include <cmath>    // for std::isfinite
#include <vector>   // for dynamic comet storage

// Structure to represent a comet
struct Comet {
    uint16_t position;
    uint8_t color_index;
    uint32_t creation_time;  // When the comet was created
    
    Comet(uint16_t pos, uint8_t idx, uint32_t now) 
        : position(pos), color_index(idx), creation_time(now) {}
};

#define MIN_BEAT_TIME 25
#define FADE_UPDATE_TIME 500
#define DEBUG_MULTI_COMET 1  // Set to 1 to enable debug output
#define DEBUG_BUFFER_SIZE 256  // Size of debug buffer
#define MIN_BASS_THRESHOLD 25.0f     // Lower minimum threshold (was 35.0f)
#define MAX_BASS_THRESHOLD 120.0f    // Keep maximum threshold
#define MAX_TAIL_LENGTH 20   // Increased max tail length to 20 LEDs
#define REQUIRED_AUDIO_CHANNELS 18  // Number of required audio channels (0-17)
#define NUM_FFT_BINS 3       // Number of FFT bins to analyze for bass
#define MIN_BIN_THRESHOLD 5  // Minimum value required for a bin to be counted
#define DEBUG_OUTPUT_INTERVAL 250  // Output debug info every 250ms
#define ENABLE_DEBUG_OUTPUT true   // Enable regular debug output every DEBUG_OUTPUT_INTERVAL ms
#define NOISE_FLOOR_DECAY 0.99f    // Faster decay
#define NOISE_FLOOR_ATTACK 0.01f   // Much slower attack
#define MIN_NOISE_FLOOR 20.0f      // Minimum noise floor
#define MAX_NOISE_FLOOR 100.0f     // Maximum allowed noise floor
#define MIN_ABSOLUTE_VOLUME 20.0f  // Absolute minimum volume required for any trigger
#define MAX_ABSOLUTE_VOLUME 255.0f // Maximum expected volume for scaling
#define NOISE_FLOOR_TRIGGER 1.5f   // How much above noise floor to trigger noise floor update
#define MIN_COMET_SPACING 200     // Spacing between comets (was 250)
#define MAX_COMETS_PER_SECOND 6   // Max comets per second (was 4)

// Constants for bass detection
static const float MIN_VOLUME = 40.0f;          // Minimum volume to consider
static const float PEAK_DECAY = 0.95f;          // How fast peak volume decays
static const float MIN_HIT_INTERVAL = 40.0f;    // Minimum time between hits
static const float ATTACK_RATE = 0.9f;          // How fast intensity rises
static const float DECAY_RATE = 0.8f;           // How fast intensity falls
static const uint16_t RATE_WINDOW = 1000;       // Window for rate limiting (ms)

// Structure to track comet creation rate
struct CometRateTracker {
    uint32_t lastCreationTimes[MAX_COMETS_PER_SECOND];
    uint8_t nextIndex;
    
    CometRateTracker() : nextIndex(0) {
        for(int i = 0; i < MAX_COMETS_PER_SECOND; i++) {
            lastCreationTimes[i] = 0;
        }
    }
    
    bool canCreateComet(uint32_t now) {
        // Check if we've created MAX_COMETS_PER_SECOND comets in the last RATE_WINDOW
        uint32_t oldestTime = now - RATE_WINDOW;
        uint8_t recentComets = 0;
        
        for(int i = 0; i < MAX_COMETS_PER_SECOND; i++) {
            if(lastCreationTimes[i] > oldestTime) {
                recentComets++;
            }
        }
        
        return recentComets < MAX_COMETS_PER_SECOND;
    }
    
    void recordCreation(uint32_t now) {
        lastCreationTimes[nextIndex] = now;
        nextIndex = (nextIndex + 1) % MAX_COMETS_PER_SECOND;
    }
};

// Helper function for float mapping (since mapf isn't available)
static float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Helper function to validate float values
static bool isValidFloat(float value) {
  return !std::isnan(value) && std::isfinite(value);  // Only check for NaN and infinity
}

// Helper function to check active comets
static int getActiveComets(std::vector<Comet>* comets) {
  return comets->size();
}

// Helper function to safely check if audio data is available and valid
static bool isAudioDataValid(um_data_t *um_data) {
  if (!um_data || !um_data->u_data) return false;
  
  // Check for required FFT data
  if (!um_data->u_data[2]) return false;  // FFT data array
  
  return true;
}

// Bass detection using FFT data
float getBassIntensity(um_data_t* um_data, bool newReading) {
  static uint32_t lastDebugTime = 0;
  static float noiseFloor = MIN_NOISE_FLOOR;  // Running average of background noise
  static uint8_t lastSensitivity = 255;  // Track sensitivity changes
  bool shouldDebug = false;
  
  // Check if we should output debug based on time or sensitivity change
  uint32_t now = millis();
  bool sensitivityChanged = (lastSensitivity != SEGMENT.intensity);
  if (sensitivityChanged) {
    Serial.printf("BASS-COMET-DEBUG: Sensitivity changed from %d to %d\n", lastSensitivity, SEGMENT.intensity);
  }
  
  if (ENABLE_DEBUG_OUTPUT && (sensitivityChanged || now - lastDebugTime >= DEBUG_OUTPUT_INTERVAL)) {
    shouldDebug = true;
    lastDebugTime = now;
    if (sensitivityChanged) {
      lastSensitivity = SEGMENT.intensity;
    }
  }

  // Debug audio data availability
  if (!um_data) {
    if (shouldDebug) Serial.println("BASS-COMET-DEBUG: um_data is NULL!");
    return 0.0f;
  }
  if (!um_data->u_data) {
    if (shouldDebug) Serial.println("BASS-COMET-DEBUG: um_data->u_data is NULL!");
    return 0.0f;
  }
  if (!um_data->u_data[2]) {
    if (shouldDebug) Serial.println("BASS-COMET-DEBUG: FFT data array (u_data[2]) is NULL!");
    return 0.0f;
  }

  static float lastBassEnergy = 0.0f;
  static float peakBassEnergy = 0.0f;
  static float smoothedBass = 0.0f;
  static uint32_t lastHitTime = 0;
  static float binPeaks[NUM_FFT_BINS] = {0};
  
  // Get FFT data
  uint8_t* fftData = (uint8_t*)um_data->u_data[2];
  
  // Calculate bass energy with weighted bins (emphasizing lower frequencies)
  float bassEnergy = 0;
  int validBins = 0;
  const float binWeights[NUM_FFT_BINS] = {1.0f, 0.7f, 0.5f};  // Weight lower frequencies more
  
  // First check if lowest bin (deepest bass) is strong enough
  float lowestBin = fftData[0];
  if (lowestBin < MIN_BIN_THRESHOLD * 1.5f) {  // Require 50% stronger signal in lowest bin
    bassEnergy = 0;
  } else {
    for(int i = 0; i < NUM_FFT_BINS; i++) {
      float binValue = fftData[i];
      binPeaks[i] = max(binValue, binPeaks[i] * PEAK_DECAY);
      
      // Only count bin if it exceeds minimum threshold
      if (binValue > MIN_BIN_THRESHOLD) {
        bassEnergy += binValue * binWeights[i];  // Apply frequency-based weighting
        validBins++;
      }
    }
    
    // Average only using valid bins, but maintain weighting
    if (validBins > 0) {
      float totalWeight = 0;
      for(int i = 0; i < validBins; i++) totalWeight += binWeights[i];
      bassEnergy = bassEnergy / totalWeight;
    } else {
      bassEnergy = 0;
    }
  }
  
  // Update noise floor with asymmetric attack/decay
  if (bassEnergy > noiseFloor * NOISE_FLOOR_TRIGGER) {
    noiseFloor = noiseFloor * (1.0f - NOISE_FLOOR_ATTACK) + bassEnergy * NOISE_FLOOR_ATTACK;
  } else {
    noiseFloor = max(noiseFloor * NOISE_FLOOR_DECAY, MIN_NOISE_FLOOR);
  }
  
  // Update peak with decay
  peakBassEnergy = max(bassEnergy, peakBassEnergy * PEAK_DECAY);
  peakBassEnergy = max(peakBassEnergy, noiseFloor);
  
  // Calculate relative change in bass energy from noise floor
  float bassChange = bassEnergy - noiseFloor;
  
  // Map Bass Detection (0-255) to sensitivity ranges
  float rawSensitivity = SEGMENT.intensity / 255.0f;  // 0->0.0 (hard), 255->1.0 (easy)
  rawSensitivity = powf(rawSensitivity, 0.4f);  // More aggressive curve for better low-end response
  
  // Calculate required bass threshold based on sensitivity
  float requiredBassLevel = MAX_BASS_THRESHOLD - (rawSensitivity * (MAX_BASS_THRESHOLD - MIN_BASS_THRESHOLD));
  
  // Dynamic threshold relative to noise floor (3.5x to 1.5x)
  float dynamicThreshold = 3.5f - (rawSensitivity * 2.0f);
  
  // Relative change threshold (1.2 to 0.3)
  float relativeChangeThreshold = 1.2f - (rawSensitivity * 0.9f);
  
  // Volume spike threshold (2.5x to 1.2x)
  float volumeSpikeThreshold = 2.5f - (rawSensitivity * 1.3f);

  // Limit noise floor
  if (noiseFloor > MAX_NOISE_FLOOR) {
    noiseFloor = MAX_NOISE_FLOOR;
  }
  
  // Scale bass energy relative to maximum for absolute threshold check
  float normalizedBassEnergy = bassEnergy / MAX_ABSOLUTE_VOLUME;
  
  // Simplified hit detection logic
  bool isHit = false;
  if (validBins >= 2 && 
      now - lastHitTime >= MIN_HIT_INTERVAL &&
      bassEnergy >= requiredBassLevel &&  // Primary gate: must exceed sensitivity threshold
      bassEnergy > noiseFloor * dynamicThreshold) {  // Must exceed dynamic threshold
    
    // Then either significant change alone OR change+spike
    if (bassChange > noiseFloor * relativeChangeThreshold * 2.0f ||  // Stronger change requirement alone
        (bassChange > noiseFloor * relativeChangeThreshold &&  // Regular change requirement
         bassEnergy > peakBassEnergy * volumeSpikeThreshold)) {  // With volume spike
      isHit = true;
      lastHitTime = now;
      smoothedBass = 1.0f;
    }
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

  // Update last bass energy
  lastBassEnergy = bassEnergy;

  // Enhanced debug output
  if (shouldDebug) {
    bool wouldTriggerDynamic = bassEnergy > noiseFloor * dynamicThreshold;
    bool wouldTriggerChange = bassChange > noiseFloor * relativeChangeThreshold;
    bool wouldTriggerSpike = bassEnergy > peakBassEnergy * volumeSpikeThreshold;
    
    Serial.printf("BASS-COMET: [Sensitivity=%d->%.3f (prev=%d)] FFT[%d,%d,%d] Bass=%.1f Noise=%.1f Chg=%.1f Peak=%.1f Hit=%d [Dyn=%d,Chg=%d,Spk=%d] Smooth=%.3f ReqBass=%.1f DynThresh=%.2f RelThresh=%.3f SpikeThresh=%.2f ValidBins=%d Speed=%d TailLen=%d\n",
      SEGMENT.intensity, rawSensitivity, lastSensitivity,
      fftData[0], fftData[1], fftData[2],
      bassEnergy, noiseFloor, bassChange, peakBassEnergy, isHit ? 1 : 0,
      wouldTriggerDynamic ? 1 : 0, wouldTriggerChange ? 1 : 0, wouldTriggerSpike ? 1 : 0,
      smoothedBass, requiredBassLevel, dynamicThreshold, relativeChangeThreshold, volumeSpikeThreshold,
      validBins, SEGMENT.speed, SEGMENT.custom1);
  }

  return smoothedBass;
}

uint16_t mode_multi_comet_bass_ar(void) {
  const uint8_t remappedSpeed = map(SEGMENT.speed, 0, 255, 200, 255);
  const uint32_t cycleTime = (uint32_t)((255 - remappedSpeed) * 1.5);
  const uint32_t it = strip.now / cycleTime;

  // Use SEGENV.data to store a pointer to our vector of comets
  if (!SEGENV.allocateData(sizeof(std::vector<Comet>))) return FX_MODE_STATIC;
  std::vector<Comet>* comets = reinterpret_cast<std::vector<Comet>*>(SEGENV.data);

  static uint8_t fadeRate = 180;
  static uint32_t lastBeat = 0;
  static uint32_t lastDebugTime = 0;
  static uint8_t colorIndex = 0;
  static uint32_t lastTailPassTime = 0;  // Time when last comet's tail passed position 0
  static CometRateTracker rateTracker;

  // Map tail length from 0-255 to 1-20
  const uint8_t tailLength = map(SEGMENT.custom1, 0, 255, 1, MAX_TAIL_LENGTH);
  const uint8_t baseFade = map(tailLength, 1, MAX_TAIL_LENGTH, 250, 180);

  if (SEGENV.call == 0) {
    SEGMENT.fill(SEGCOLOR(1));
    new (SEGENV.data) std::vector<Comet>();  // Properly construct vector in place
    fadeRate = baseFade;
    SEGENV.aux0 = 0;
    lastBeat = 0;
    lastTailPassTime = 0;
  }

  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !um_data) {
    if (DEBUG_MULTI_COMET) Serial.printf("BASS-COMET-DEBUG: Audio acquisition failed. hasAudio=%d um_data=%p\n", hasAudio, um_data);
    return FRAMETIME;
  }

  float bassIntensity = getBassIntensity(um_data, DEBUG_MULTI_COMET);

  // Create new comet on bass hits when intensity is high enough
  if (bassIntensity > 0.0f) {
    uint32_t timeSinceLastBeat = strip.now - lastBeat;
    uint32_t timeSinceLastTail = strip.now - lastTailPassTime;
    
    // Only create new comet if:
    // 1. Minimum beat time has passed AND
    // 2. Either this is the first comet OR enough time has passed since the last tail AND
    // 3. We haven't exceeded our rate limit
    if (timeSinceLastBeat > MIN_BEAT_TIME && 
        (comets->empty() || timeSinceLastTail >= MIN_COMET_SPACING) &&
        rateTracker.canCreateComet(strip.now)) {
      lastBeat = strip.now;
      comets->emplace_back(0, colorIndex++ % 2, strip.now);
      rateTracker.recordCreation(strip.now);
      
      if (DEBUG_MULTI_COMET) {
        Serial.printf("BASS-COMET-DEBUG: Created new comet at %u ms\n", strip.now);
      }
    }
  }

  // Update comet visuals
  SEGMENT.fadeToBlackBy(baseFade);

  // Update and draw all comets
  for (auto it = comets->begin(); it != comets->end();) {
    uint16_t headIndex = it->position;
    int16_t tailStart = (headIndex >= tailLength) ? headIndex - (tailLength - 1) : 0;
    
    // Update lastTailPassTime when the tail end moves past position 0
    if (tailStart == 1) {
      lastTailPassTime = strip.now;
      if (DEBUG_MULTI_COMET) {
        Serial.printf("BASS-COMET-DEBUG: Tail passed at %u ms\n", lastTailPassTime);
      }
    }
    
    // Clear old tail end
    if (tailStart > 0) SEGMENT.setPixelColor(tailStart - 1, 0);

    // Draw tail
    for (int16_t t = tailStart; t <= headIndex && t < SEGLEN; t++) {
      float brightness = 1.0f - (float)(headIndex - t) / tailLength;
      uint32_t color = SEGCOLOR(2) != 0 && (it->color_index == 1) ? 
                      SEGCOLOR(2) : SEGMENT.color_from_palette(t, true, false, 0);
      
      SEGMENT.setPixelColor(t, 
        (uint8_t)((color >> 16) * brightness),
        (uint8_t)(((color >> 8) & 0xFF) * brightness),
        (uint8_t)((color & 0xFF) * brightness));
    }

    // Update position and remove if past end
    if (++it->position >= SEGLEN) {
      it = comets->erase(it);
    } else {
      ++it;
    }
  }

  return cycleTime;
} 