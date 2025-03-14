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

#define MIN_BEAT_TIME 600        // Fixed 600ms between triggers
#define FADE_UPDATE_TIME 500
#define DEBUG_MULTI_COMET 1      // Set to 1 to enable debug output
#define DEBUG_BUFFER_SIZE 256    // Size of debug buffer
#define MIN_BASS_THRESHOLD 40.0f // Increased minimum threshold for bass detection
#define MAX_BASS_THRESHOLD 120.0f // Keep maximum threshold
#define MAX_TAIL_LENGTH 30       // Increased max tail length
#define REQUIRED_AUDIO_CHANNELS 18 // Number of required audio channels (0-17)
#define NUM_FFT_BINS 3           // Number of FFT bins to analyze for bass
#define MIN_BIN_THRESHOLD 8      // Increased from 5 - minimum value for a bin to be counted
#define DEBUG_OUTPUT_INTERVAL 250 // Output debug info every 250ms
#define ENABLE_DEBUG_OUTPUT true  // Enable regular debug output every DEBUG_OUTPUT_INTERVAL ms
#define NOISE_FLOOR_DECAY 0.97f   // Faster decay (changed from 0.99f)
#define NOISE_FLOOR_ATTACK 0.01f  // Much slower attack
#define MIN_NOISE_FLOOR 25.0f     // Increased from 20.0f
#define MAX_NOISE_FLOOR 80.0f     // Reduced from 100.0f to prevent severe saturation
#define MIN_ABSOLUTE_VOLUME 35.0f // Increased from 30.0f
#define MAX_ABSOLUTE_VOLUME 255.0f // Maximum expected volume for scaling
#define NOISE_FLOOR_TRIGGER 1.5f  // How much above noise floor to trigger noise floor update
#define NOISE_FLOOR_HIGH_LEVEL 60.0f // Level at which we consider the noise floor "high"
#define NOISE_FLOOR_FAST_DECAY 0.92f // Much faster decay when noise floor is high
#define MIN_FADE_RATE 110         // Minimum fade rate (was 180 - allows for slower fade)
#define MAX_FADE_RATE 220         // Maximum fade rate (was 250 - allows for more fade)
#define MIN_BASS_INTENSITY 0.3f   // Minimum bass intensity to create a comet
#define MIN_FIRST_BIN_VALUE 14    // Minimum value for first FFT bin (bass)
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)  // Added from original comet

// Constants for bass detection
static const float MIN_VOLUME = 40.0f;          // Minimum volume to consider
static const float PEAK_DECAY = 0.95f;          // How fast peak volume decays
static const float MIN_HIT_INTERVAL = 40.0f;    // Minimum time between hits
static const float ATTACK_RATE = 0.9f;          // How fast intensity rises
static const float DECAY_RATE = 0.8f;           // How fast intensity falls

// Simplified rate tracker - just tracks last creation time
struct CometRateTracker {
    uint32_t lastCreationTime;  // Track the most recent creation time
    
    CometRateTracker() : lastCreationTime(0) {}
    
    bool canCreateComet(uint32_t now) {
        // Check if enough time has passed since last creation
        return (now - lastCreationTime >= MIN_BEAT_TIME);
    }
    
    void recordCreation(uint32_t now) {
        lastCreationTime = now;
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
  static uint32_t lastLoudPassageTime = 0; // Track when last loud passage occurred
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
  
  // Early return if lowest bin (real bass) is too weak
  if (fftData[0] < MIN_FIRST_BIN_VALUE) {
    if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: First FFT bin too low: %d < %d\n", fftData[0], MIN_FIRST_BIN_VALUE);
    return 0.0f;  // Not enough bass to consider
  }
  
  // Check if all bins are below threshold (silence or very quiet)
  bool allBinsLow = true;
  for(int i = 0; i < NUM_FFT_BINS; i++) {
    if (fftData[i] >= MIN_BIN_THRESHOLD) {
      allBinsLow = false;
      break;
    }
  }
  
  if (allBinsLow) {
    if (shouldDebug) Serial.println("BASS-COMET-DEBUG: All bins below threshold - silence detected");
    return 0.0f;
  }
  
  // New approach to bass detection that better handles real-world audio
  bool bassDominant = false;
  float bassRatio = 0.0f;
  
  // Calculate average of mid and high frequencies
  float avgHigherFreqs = (fftData[1] + fftData[2]) / 2.0f;
  
  // Calculate ratio of bass to average higher frequencies
  if (avgHigherFreqs > 0) {
    bassRatio = (float)fftData[0] / avgHigherFreqs;
  }
  
  // Bass is considered dominant if ANY of these conditions are met:
  // 1. Bass ratio is at least 0.4 (reduced from 0.5) compared to higher frequencies
  if (bassRatio >= 0.4f && fftData[0] >= MIN_FIRST_BIN_VALUE && fftData[0] >= 40) {
    bassDominant = true;
    if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: Bass dominant due to ratio: %.2f\n", bassRatio);
  }
  
  // 2. Bass is very strong in absolute terms (reduced from 100 to 85)
  else if (fftData[0] >= 85) {
    bassDominant = true;
    if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: Bass dominant due to absolute strength: %d\n", fftData[0]);
  }
  
  // 3. Bass has increased significantly compared to recent history
  static float lastFirstBin = 0;
  float binChange = (float)fftData[0] - lastFirstBin;
  if (binChange > 25 && fftData[0] >= 70) {
    bassDominant = true;
    if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: Bass dominant due to rapid increase: +%.1f\n", binChange);
  }
  lastFirstBin = fftData[0] * 0.3f + lastFirstBin * 0.7f; // Smooth the tracking
  
  // 4. Bass has a good absolute value and exceeds sensitivity threshold
  float sensitivity = SEGMENT.intensity / 255.0f;
  float sensitivityThreshold = 90.0f - (sensitivity * 30.0f); // 60-90 range based on sensitivity
  if (fftData[0] >= MIN_FIRST_BIN_VALUE && 
      fftData[0] >= sensitivityThreshold && 
      bassRatio >= 0.35f) {
    bassDominant = true;
    if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: Bass dominant due to sensitivity match: %d >= %.1f\n", 
                               fftData[0], sensitivityThreshold);
  }
  
  if (!bassDominant) {
    if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: Not bass-dominant: First bin (%d) ratio %.2f vs Avg Higher (%d,%d)\n", 
                               fftData[0], bassRatio, fftData[1], fftData[2]);
    return 0.0f;
  }
  
  // Calculate bass energy with stronger weighting for lower frequencies
  float bassEnergy = 0;
  int validBins = 0;
  
  // Simplified weighting that focuses primarily on the bass bin
  const float binWeights[NUM_FFT_BINS] = {1.0f, 0.3f, 0.1f};
  
  // Calculate bass energy focusing mainly on the first bin
  float bassBoost = 1.0f + (sensitivity * 0.5f); // Reduced sensitivity impact (was 1.0)
  
  bassEnergy = fftData[0] * binWeights[0] * bassBoost;
  
  // Add small contributions from other frequencies to maintain some dynamics
  for(int i = 1; i < NUM_FFT_BINS; i++) {
    if (fftData[i] > MIN_BIN_THRESHOLD) {
      bassEnergy += fftData[i] * binWeights[i];
      validBins++;
    }
  }
  
  // Normalize bass energy
  float totalWeight = binWeights[0] * bassBoost;
  for(int i = 1; i < NUM_FFT_BINS; i++) {
    if (fftData[i] > MIN_BIN_THRESHOLD) {
      totalWeight += binWeights[i];
    }
  }
  
  bassEnergy = bassEnergy / totalWeight;
  
  // Apply absolute minimum threshold
  if (bassEnergy < MIN_ABSOLUTE_VOLUME) {
    if (shouldDebug && bassEnergy > 0) {
      Serial.printf("BASS-COMET-DEBUG: Bass energy too low: %.1f < %.1f\n", bassEnergy, MIN_ABSOLUTE_VOLUME);
    }
    return 0.0f;
  }
  
  // Update noise floor with improved adaptive attack/decay
  if (bassEnergy > noiseFloor * NOISE_FLOOR_TRIGGER) {
    // If noise floor is already high, use even slower attack rate
    float adaptiveAttack = NOISE_FLOOR_ATTACK;
    if (noiseFloor > NOISE_FLOOR_HIGH_LEVEL) {
      adaptiveAttack = NOISE_FLOOR_ATTACK * 0.5f; // Half the attack rate when already high
    }
    
    noiseFloor = noiseFloor * (1.0f - adaptiveAttack) + bassEnergy * adaptiveAttack;
    lastLoudPassageTime = now; // Mark this as a loud passage
  } else {
    // Use faster decay when noise floor is high or it's been quiet for a while
    float adaptiveDecay = NOISE_FLOOR_DECAY;
    
    // If noise floor is high, decay faster
    if (noiseFloor > NOISE_FLOOR_HIGH_LEVEL) {
      adaptiveDecay = NOISE_FLOOR_FAST_DECAY;
    }
    
    // If it's been quiet for over 3 seconds, accelerate floor decrease
    if (now - lastLoudPassageTime > 3000 && noiseFloor > MIN_NOISE_FLOOR * 1.5f) {
      adaptiveDecay = NOISE_FLOOR_FAST_DECAY * 0.95f; // Even faster decay after silence
    }
    
    noiseFloor = max(noiseFloor * adaptiveDecay, MIN_NOISE_FLOOR);
  }
  
  // Limit noise floor
  if (noiseFloor > MAX_NOISE_FLOOR) {
    noiseFloor = MAX_NOISE_FLOOR;
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

  // Scale bass energy relative to maximum for absolute threshold check
  float normalizedBassEnergy = bassEnergy / MAX_ABSOLUTE_VOLUME;
  
  // Simplified hit detection logic - make it more lenient and consistent
  bool isHit = false;
  if (validBins >= 2 && 
      now - lastHitTime >= MIN_HIT_INTERVAL) {
    
    // Modified conditions for hit detection - only need to pass ONE of these:
    bool absoluteThresholdMet = bassEnergy >= requiredBassLevel * 0.9f; // 90% of required (more lenient)
    bool dynamicThresholdMet = bassEnergy > noiseFloor * (dynamicThreshold * 0.8f); // 80% of dynamic threshold
    bool changeThresholdMet = bassChange > noiseFloor * relativeChangeThreshold;
    bool spikeThresholdMet = bassEnergy > peakBassEnergy * volumeSpikeThreshold;
    
    // Pass if either:
    // 1. Strong absolute bass level (primary path), OR
    if (absoluteThresholdMet) {
      isHit = true;
      if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: Hit due to absolute threshold\n");
    }
    // 2. Dynamic threshold with decent change, OR
    else if (dynamicThresholdMet && changeThresholdMet) {
      isHit = true;
      if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: Hit due to dynamic+change thresholds\n");
    }
    // 3. Not as strong but sudden spike
    else if (changeThresholdMet && spikeThresholdMet) {
      isHit = true;
      if (shouldDebug) Serial.printf("BASS-COMET-DEBUG: Hit due to change+spike thresholds\n");
    }
    
    if (isHit) {
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

  static uint32_t lastBeat = 0;
  static uint32_t lastDebugTime = 0;
  static uint8_t colorIndex = 0;
  static uint32_t lastTailPassTime = 0;
  static CometRateTracker rateTracker;

  // Map tail length from 0-255 to MAX_TAIL_LENGTH
  const uint8_t tailLength = map(SEGMENT.custom1, 0, 255, 1, MAX_TAIL_LENGTH);
  
  // Adjusted fade rate mapping to allow for slower fading (more visible tails)
  const uint8_t baseFade = map(tailLength, 1, MAX_TAIL_LENGTH, MAX_FADE_RATE, MIN_FADE_RATE);

  if (SEGENV.call == 0) {
    SEGMENT.fill(SEGCOLOR(1));
    new (SEGENV.data) std::vector<Comet>();
    SEGENV.aux0 = 0;
    lastBeat = 0;
    lastTailPassTime = 0;
    colorIndex = random8(); // Start with random color index
  }

  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !um_data) {
    if (DEBUG_MULTI_COMET) Serial.printf("BASS-COMET-DEBUG: Audio acquisition failed. hasAudio=%d um_data=%p\n", hasAudio, um_data);
    return FRAMETIME;
  }

  // Get additional audio metrics for verification
  float volumeSmth = 0;
  uint8_t samplePeak = 0;
  if (um_data->u_data[0] && um_data->u_data[3]) {
    volumeSmth = *(float*)um_data->u_data[0];      // General volume
    samplePeak = *(uint8_t*)um_data->u_data[3];    // Sample peak
  }

  float bassIntensity = getBassIntensity(um_data, DEBUG_MULTI_COMET);

  // First apply global fade like original comet
  SEGMENT.fade_out(baseFade);

  // Create new comet on bass hits when intensity is high enough
  if (bassIntensity > MIN_BASS_INTENSITY && rateTracker.canCreateComet(strip.now)) {
    uint8_t* fftData = (uint8_t*)um_data->u_data[2];
    
    // Double-check lowest bin (bass) is sufficiently strong
    if (fftData && fftData[0] >= MIN_FIRST_BIN_VALUE) {
      lastBeat = strip.now;
      
      // Alternate between primary and secondary colors
      uint8_t thisColorIndex = colorIndex++ % 2;
      comets->emplace_back(0, thisColorIndex, strip.now);
      rateTracker.recordCreation(strip.now);
      
      if (DEBUG_MULTI_COMET) {
        Serial.printf("BASS-COMET-DEBUG: Created new comet at %u ms - Color Index: %d FFT[%d,%d,%d] Bass=%.1f\n", 
          strip.now, thisColorIndex, fftData[0], fftData[1], fftData[2], bassIntensity);
      }
    }
  }

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
    
    // Draw tail with improved non-linear brightness calculation
    for (int16_t t = tailStart; t <= headIndex && t < SEGLEN; t++) {
      // Use quadratic curve for smoother looking fade (x²)
      float distanceRatio = (float)(headIndex - t) / tailLength;
      float brightness = 1.0f - (distanceRatio * distanceRatio);
      
      uint32_t color = SEGCOLOR(2) != 0 && (it->color_index == 1) ? 
                      SEGCOLOR(2) : SEGMENT.color_from_palette(t, true, PALETTE_SOLID_WRAP, 0);
      
      SEGMENT.setPixelColor(t, 
        (uint8_t)((color >> 16) * brightness),
        (uint8_t)(((color >> 8) & 0xFF) * brightness),
        (uint8_t)((color & 0xFF) * brightness));
    }

    // Set the comet head (just like original comet)
    if (headIndex < SEGLEN) {
      uint32_t color = SEGCOLOR(2) != 0 && (it->color_index == 1) ? 
                      SEGCOLOR(2) : SEGMENT.color_from_palette(headIndex, true, PALETTE_SOLID_WRAP, 0);
      
      SEGMENT.setPixelColor(headIndex, color);
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