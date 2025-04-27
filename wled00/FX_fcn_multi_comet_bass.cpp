/*
 * Multi Comet Bass (FX_fcn_multi_comet_bass.cpp)
 * 
 * Creates bass-responsive comets that are launched in response to bass beats, 
 * with intensity, color, and frequency controlled by bass energy.
 * 
 * Best for: Bass-heavy music including hip-hop, R&B, dubstep, and electronic 
 * dance music with strong bass lines.
 */

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

// Configuration constants
#define MAX_TAIL_LENGTH 30      // Maximum possible tail length
#define MIN_TAIL_LENGTH 2       // IMPROVED: Minimum possible tail length (was implicit at 2)
#define MIN_FIRST_BIN_VALUE 10  // Minimum value in first FFT bin to consider
#define MIN_NOISE_FLOOR 25.0f   // Absolute minimum noise floor
#define MIN_BASS_INTENSITY 0.3f // Minimum intensity to create a comet
#define MIN_BEAT_TIME_SLOW 450  // Minimum time between beats at lowest speed (ms)
#define MIN_BEAT_TIME_FAST 125  // Minimum time between beats at highest speed (ms)
#define DEBUG_MULTI_COMET 0     // Set to 0 to disable debug output
#define DEBUG_OUTPUT_INTERVAL 500 // Output debug 
#define REQUIRED_AUDIO_CHANNELS 18 // Number of required audio channels (0-17) used by audio reactive usermod

// Bass detection constants
#define NUM_FFT_BINS 3           // Number of FFT bins to analyze for bass
#define MIN_BIN_THRESHOLD 6     // Minimum value for a bin to be counted

// Noise floor constants
#define NOISE_FLOOR_DECAY 0.97f  // Default noise floor decay
#define NOISE_FLOOR_ATTACK 0.01f // Default noise floor attack
#define MAX_NOISE_FLOOR 80.0f    // Maximum noise floor
#define HIGH_NOISE_LEVEL 60.0f   // Level at which noise is considered "high"
#define FAST_DECAY_RATE 0.92f    // Fast decay rate for high noise

// Animation constants
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Rate tracker for comet creation
struct CometRateTracker {
    uint32_t lastCreationTime = 0;
    uint16_t lastCometPos = 0;  // IMPROVED: Track position of last comet
    bool hasPendingComet = false; // IMPROVED: Track if we have comets in flight
    
    bool canCreateComet(uint32_t now, uint8_t speed, uint16_t segLen, const std::vector<Comet>* comets) {
        uint16_t minBeatTime = map(speed, 0, 255, MIN_BEAT_TIME_SLOW, MIN_BEAT_TIME_FAST);
        
        // IMPROVED: Check if minimum time has passed
        bool timeElapsed = (now - lastCreationTime >= minBeatTime);
        
        // IMPROVED: Add position-based limit - only create new comets when 
        // previous ones have traveled at least 50% of the strip
        bool positionAllowed = true;
        
        if (!comets->empty()) {
            hasPendingComet = true;
            // Find the position of the first (oldest) comet
            uint16_t oldestCometPos = comets->front().position;
            
            // Calculate how far the oldest comet has traveled as a percentage
            float travelPercentage = (float)oldestCometPos / segLen;
            
            // Only allow new comets when oldest has traveled at least 50% of the strip
            positionAllowed = (travelPercentage >= 0.5f);
        } else {
            hasPendingComet = false;
        }
        
        return timeElapsed && positionAllowed;
    }
    
    void recordCreation(uint32_t now, uint16_t pos = 0) {
        lastCreationTime = now;
        lastCometPos = pos;
    }
};

// Helper function for float mapping
static float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Helper function to safely check if audio data is available and valid
static bool isAudioDataValid(um_data_t *um_data, bool shouldDebug = false) {
  if (!um_data) {
    if (shouldDebug) Serial.println("EXP-BASS-COMET: um_data is NULL!");
    return false;
  }
  if (!um_data->u_data) {
    if (shouldDebug) Serial.println("EXP-BASS-COMET: um_data->u_data is NULL!");
    return false;
  }
  if (!um_data->u_data[2]) {
    if (shouldDebug) Serial.println("EXP-BASS-COMET: FFT data array (u_data[2]) is NULL!");
    return false;
  }
  return true;
}

// Debug output function to consolidate format and style
static void debugOutput(const char* format, ...) {
  if (!DEBUG_MULTI_COMET) return;
  
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  Serial.print("EXP-BASS-COMET: ");
  Serial.println(buffer);
}

// Bass detection using FFT data
float getBassIntensity(um_data_t* um_data) {
  static uint32_t lastDebugTime = 0;
  static float noiseFloor = MIN_NOISE_FLOOR;
  static uint8_t lastSensitivity = 255;
  static uint8_t lastMinVolume = 255;
  static uint32_t lastLoudPassageTime = 0;
  static float lastBassEnergy = 0.0f;
  static float peakBassEnergy = 0.0f;
  static float smoothedBass = 0.0f;
  static uint32_t lastHitTime = 0;
  
  const uint32_t now = millis();
  bool shouldDebug = false;
  
  // Check if we should output debug based on sensitivity changes or interval
  const bool sensitivityChanged = (lastSensitivity != SEGMENT.intensity);
  const bool minVolumeChanged = (lastMinVolume != SEGMENT.custom2);
  
  if (sensitivityChanged) {
    debugOutput("Bass sensitivity changed from %d to %d", lastSensitivity, SEGMENT.intensity);
  }
  
  if (minVolumeChanged) {
    debugOutput("Min volume changed from %d to %d", lastMinVolume, SEGMENT.custom2);
  }
  
  if (sensitivityChanged || minVolumeChanged || now - lastDebugTime >= DEBUG_OUTPUT_INTERVAL) {
    shouldDebug = true;
    lastDebugTime = now;
    if (sensitivityChanged) lastSensitivity = SEGMENT.intensity;
    if (minVolumeChanged) lastMinVolume = SEGMENT.custom2;
  }

  // Validate audio data
  if (!isAudioDataValid(um_data)) {
    return 0.0f;
  }

  float volume = *(float*)um_data->u_data[0];
  // REMOVED: Explicit volume check - rely on usermod squelch
  // if (volume < MIN_BASS_VOLUME) {
  //     return 0.0f;
  // }
  
  uint8_t* fftData = (uint8_t*)um_data->u_data[2];
  
  // Early return if bass is too weak
  if (fftData[0] < MIN_FIRST_BIN_VALUE) {
    if (shouldDebug) debugOutput("First FFT bin too low: %d < %d", fftData[0], MIN_FIRST_BIN_VALUE);
    return 0.0f;
  }
  
  // Check if all bins are below threshold (silence)
  bool hasSignificantBin = false;
  for (int i = 0; i < NUM_FFT_BINS; i++) {
    if (fftData[i] >= MIN_BIN_THRESHOLD) {
      hasSignificantBin = true;
      break;
    }
  }
  
  if (!hasSignificantBin) {
    if (shouldDebug) debugOutput("No significant frequency bins found");
    return 0.0f;
  }
  
  // Frequency specific weights for bass detection
  // Focus more heavily on first bin (lowest freqs) for better punch detection
  // MODIFIED: Adjusted bin weights to better detect drum and bass style bass
  const float binWeights[NUM_FFT_BINS] = {1.0f, 0.6f, 0.3f};
  
  float weightedSum = 0.0f;
  float totalWeight = 0.0f;
  
  for (int i = 0; i < NUM_FFT_BINS; i++) {
    if (fftData[i] >= MIN_BIN_THRESHOLD) {
      weightedSum += fftData[i] * binWeights[i];
      totalWeight += binWeights[i];
    }
  }
  
  // Calculate weighted average of FFT bins
  const float averageBassValue = (totalWeight > 0) ? (weightedSum / totalWeight) : 0.0f;
  
  // Apply min volume threshold (slider)
  const float minVolumeThreshold = map_float(SEGMENT.custom2, 0, 255, 0, 100);
  
  if (averageBassValue < minVolumeThreshold) {
    if (shouldDebug) debugOutput("Average bass value too low: %.1f < %.1f (min volume)", 
                              averageBassValue, minVolumeThreshold);
    return 0.0f;
  }
  
  // MODIFIED: Enhanced noise floor adaptation for faster response 
  // to short bass transients common in drum and bass music
  const bool isLoudPassage = (averageBassValue > noiseFloor * 1.3f); // Reduced from 1.5f for faster response
  const bool isHighNoiseFloor = (noiseFloor > HIGH_NOISE_LEVEL);
  const bool isLongQuietPeriod = (now - lastLoudPassageTime > 2000 && noiseFloor > MIN_NOISE_FLOOR * 1.5f); // Reduced from 3000ms
  
  if (isLoudPassage) {
    // Update noise floor on loud passage (with adaptive attack rate)
    // MODIFIED: Faster attack rate for better tracking of rapid bass
    const float adaptiveAttack = isHighNoiseFloor ? NOISE_FLOOR_ATTACK * 0.7f : NOISE_FLOOR_ATTACK * 1.2f;
    noiseFloor = noiseFloor * (1.0f - adaptiveAttack) + averageBassValue * adaptiveAttack;
    lastLoudPassageTime = now;
  } else {
    // Decay noise floor based on conditions
    float decayRate = NOISE_FLOOR_DECAY;
    if (isHighNoiseFloor) decayRate = FAST_DECAY_RATE;
    if (isLongQuietPeriod) decayRate = FAST_DECAY_RATE * 0.95f;
    
    noiseFloor = max(noiseFloor * decayRate, MIN_NOISE_FLOOR);
  }
  
  // Apply upper limit to noise floor
  noiseFloor = min(noiseFloor, MAX_NOISE_FLOOR);
  
  // Calculate bass threshold with sensitivity adjustment
  // Fix: Correct sensitivity mapping (lower intensity = less sensitive = higher threshold)
  float sensitivityFactor = map_float(SEGMENT.intensity, 0, 255, 1.8f, 0.7f); // Lower intensity = higher factor = less sensitive
  
  // Bass must exceed threshold to be considered a "hit"
  // MODIFIED: Lower threshold for more responsiveness to quick, punchy bass
  float bassThreshold = noiseFloor * sensitivityFactor * 1.1f; // Reduced from default multiplier for more hits
  
  // Check if we hit the threshold
  bool isHit = (averageBassValue > bassThreshold);
  
  // Calculate bass intensity (0.0-1.0) based on how much bass exceeds threshold
  float bassIntensityRaw = 0.0f;
  
  if (isHit) {
    // Calculate excess bass above threshold (0.0-1.0 range)
    // MODIFIED: Increase scaling to better differentiate bass intensity levels
    float excess = (averageBassValue - bassThreshold) / (bassThreshold * 0.5f);
    bassIntensityRaw = constrain(excess, 0.0f, 1.0f);
    
    // Record hit time
    lastHitTime = now;
    
    // Update peak bass energy for this "group" of hits
    peakBassEnergy = max(peakBassEnergy, bassIntensityRaw);
  } else {
    // If we recently had a hit, retain some intensity for smoother experience
    const uint32_t hitThreshold = 150; // Recovery time in ms
    if (now - lastHitTime < hitThreshold) {
      // Linear fade out based on time since last hit
      bassIntensityRaw = peakBassEnergy * (1.0f - (float)(now - lastHitTime) / hitThreshold);
    } else {
      // Reset peak energy if we're fully decayed
      peakBassEnergy = 0.0f;
    }
  }
  
  // Apply smoothing for stability (with stronger weighting toward new values for responsiveness)
  // MODIFIED: Less smoothing for faster response to quick bass hits
  smoothedBass = smoothedBass * 0.5f + bassIntensityRaw * 0.5f;
  
  // Debug output
  if (shouldDebug) {
    debugOutput("BASS-COMET: [BassSens=%d, MinVol=%d] FFT[%d,%d,%d] Avg=%.1f Noise=%.1f Thresh=%.1f Hit=%d Smooth=%.3f",
      SEGMENT.intensity, SEGMENT.custom2,
      fftData[0], fftData[1], fftData[2],
      averageBassValue, noiseFloor, bassThreshold, isHit ? 1 : 0,
      smoothedBass);
  }

  return smoothedBass;
}

uint16_t mode_multi_comet_bass_ar(void) {
  // Map speed from 0-255 to slower-faster (wider range for better control)
  const uint8_t remappedSpeed = map(SEGMENT.speed, 0, 255, 40, 240);  // Fixed direction: higher values = faster
  const uint32_t cycleTime = (uint32_t)((255 - remappedSpeed) * 0.4);  // Even lower multiplier for much faster overall movement

  // Allocate memory for comet vector if needed
  if (!SEGENV.allocateData(sizeof(std::vector<Comet>))) return FX_MODE_STATIC;
  std::vector<Comet>* comets = reinterpret_cast<std::vector<Comet>*>(SEGENV.data);

  // Static variables for state tracking
  static uint8_t colorIndex = 0;
  static CometRateTracker rateTracker;

  // IMPROVED: Map tail length from 0-255 to MIN_TAIL_LENGTH-MAX_TAIL_LENGTH (0->MIN for much shorter tails)
  const uint8_t tailLength = map(SEGMENT.custom1, 0, 255, MIN_TAIL_LENGTH, MAX_TAIL_LENGTH);
  
  // Fade rate based on tail length (longer tails need slower fade)
  const uint8_t baseFade = map(tailLength, MIN_TAIL_LENGTH, MAX_TAIL_LENGTH, 240, 80);  // More extreme fade difference for visibility

  // First time initialization
  if (SEGENV.call == 0) {
    SEGMENT.fill(SEGCOLOR(1));
    new (SEGENV.data) std::vector<Comet>();
    colorIndex = random8(); // Start with random color index
  }

  // Get audio data
  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !isAudioDataValid(um_data)) {
    debugOutput("Audio acquisition failed");
    return FRAMETIME;
  }

  // Get bass intensity - this internally handles all the FFT analysis
  float bassIntensity = getBassIntensity(um_data);

  // Apply global fade like original comet
  SEGMENT.fade_out(baseFade);

  // IMPROVED: Create new comet on bass hits when intensity is high enough and position/rate limiting allows
  if (bassIntensity > MIN_BASS_INTENSITY && rateTracker.canCreateComet(strip.now, SEGMENT.speed, SEGLEN, comets)) {
    // Alternate between primary and secondary colors
    uint8_t thisColorIndex = colorIndex++ % 2;
    comets->emplace_back(0, thisColorIndex, strip.now);
    rateTracker.recordCreation(strip.now);
    
    debugOutput("Created new comet at %u ms - Color Index: %d, Speed: %d, Tail: %d", 
        strip.now, thisColorIndex, SEGMENT.speed, tailLength);
  }

  // Update and draw all comets
  for (auto it = comets->begin(); it != comets->end();) {
    uint16_t headIndex = it->position;
    int16_t tailStart = (headIndex >= tailLength) ? headIndex - (tailLength - 1) : 0;
    
    // MODIFIED: Improved tail fading for smoother transitions at low brightness
    // Draw tail with improved brightness calculation for smoother fade
    for (int16_t t = tailStart; t <= headIndex && t < SEGLEN; t++) {
      // Use a combination of quadratic and linear curves for smoother fade
      // This prevents abrupt disappearance at the end of the tail
      float distanceRatio = (float)(headIndex - t) / tailLength;
      
      // Blend between quadratic and linear fade curves based on distance
      // At the end of the tail (high distance ratio), use more linear fade
      // This gives smoother transitions at low brightness levels
      float quadFade = 1.0f - (distanceRatio * distanceRatio);
      float linearFade = 1.0f - distanceRatio;
      
      // Blend more toward linear as we get closer to the end of the tail
      float blendFactor = distanceRatio * 0.6f; // Adjust this value to control blend point
      float brightness = quadFade * (1.0f - blendFactor) + linearFade * blendFactor;
      
      // Apply minimum brightness threshold to prevent abrupt disappearance
      // This helps ensure the very end of the tail fades smoothly
      brightness = max(brightness, 0.02f);
      
      // Get color - alternate between primary and palette colors
      uint32_t color = SEGCOLOR(2) != 0 && (it->color_index == 1) ? 
                     SEGCOLOR(2) : SEGMENT.color_from_palette(t, true, PALETTE_SOLID_WRAP, 0);
      
      // Apply brightness to color components and handle rounding properly
      uint8_t r = ((color >> 16) & 0xFF) * brightness + 0.5f; // Added 0.5f for better rounding
      uint8_t g = ((color >> 8) & 0xFF) * brightness + 0.5f;
      uint8_t b = (color & 0xFF) * brightness + 0.5f;
      
      SEGMENT.setPixelColor(t, r, g, b);
    }

    // Set comet head to full brightness
    if (headIndex < SEGLEN) {
      uint32_t color = SEGCOLOR(2) != 0 && (it->color_index == 1) ? 
                     SEGCOLOR(2) : SEGMENT.color_from_palette(headIndex, true, PALETTE_SOLID_WRAP, 0);
      
      SEGMENT.setPixelColor(headIndex, color);
    }

    // Update position and remove if past end of strip
    if (++it->position >= SEGLEN) {
      it = comets->erase(it);
    } else {
      ++it;
    }
  }

  return cycleTime;
}
