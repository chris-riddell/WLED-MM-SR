#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>

// Increased time ranges for smoother transitions
#define BUILDUP_DURATION 4000  // Increased buildup time (4 seconds)
#define DROP_DURATION 5000     // Increased drop time (5 seconds)
#define RECOVERY_DURATION 6000 // Increased recovery time (6 seconds) for much slower fade
#define MIN_VOLUME_THRESHOLD 40.0f // Increased minimum threshold to prevent false triggers
#define BASS_DROP_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// State definitions
#define STATE_WAITING 0
#define STATE_BUILDUP 1
#define STATE_DROP 2
#define STATE_RECOVERY 3
#define STATE_CALM 4

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);
extern float detectBassDropIntensity(um_data_t *um_data);

uint16_t mode_bass_drop(void) {
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
  
  // Allocate memory for state
  struct DropState {
    uint8_t state;            // Current animation state
    uint32_t stateStartTime;  // When current state began
    float intensity;          // Current effect intensity (0.0-1.0)
    float dropIntensity;      // Intensity of the detected drop (0.0-1.0)
    uint8_t colorOffset;      // Color offset for animation
    float smoothedVolume;     // Smoothed volume
    float prevDropIntensity;  // Previous drop intensity for hysteresis
    float smoothedIntensity; // Add smoothed intensity to reduce flicker
  };
  
  if (!SEGENV.allocateData(sizeof(DropState))) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  DropState* state = reinterpret_cast<DropState*>(SEGENV.data);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    state->state = STATE_WAITING;
    state->stateStartTime = millis();
    state->intensity = 0.0f;
    state->dropIntensity = 0.0f;
    state->colorOffset = 0;
    state->smoothedVolume = 0.0f;
    state->prevDropIntensity = 0.0f;
    state->smoothedIntensity = 0.0f; // Initialize smoothed intensity
  }
  
  // Speed controls animation speed and transitions
  float speedFactor = map_float(SEGMENT.speed, 0, 255, 0.2f, 4.5f);
  
  // Intensity controls "calmness" - higher value = more calm, gradual transitions
  float calmness = map_float(SEGMENT.intensity, 0, 255, 0.2f, 1.5f);
  float sensitivity = map_float(SEGMENT.intensity, 0, 255, 1.5f, 0.3f);  // Increased sensitivity range further
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 25.0f, 100.0f); // Lowered volume threshold range
  
  // Get current time
  uint32_t now = millis();
  uint32_t stateElapsed = now - state->stateStartTime;
  
  // Smooth volume transition
  state->smoothedVolume = state->smoothedVolume * 0.7f + volume * 0.3f;
  
  // Detect bass drop with hysteresis
  float rawDropIntensity = detectBassDropIntensity(um_data) * sensitivity;
  
  // Apply smoother transitions to drop intensity
  float dropDiff = abs(rawDropIntensity - state->prevDropIntensity);
  float dropIntensity;
  
  // Only accept significant changes
  if (dropDiff > 0.1f || rawDropIntensity > state->prevDropIntensity) {
    // More responsive to increases, less to decreases
    if (rawDropIntensity > state->prevDropIntensity) {
      // 80% new, 20% old - even faster attack
      dropIntensity = rawDropIntensity * 0.8f + state->prevDropIntensity * 0.2f;
    } else {
      // 30% new, 70% old - slow decay
      dropIntensity = rawDropIntensity * 0.3f + state->prevDropIntensity * 0.7f;
    }
  } else {
    dropIntensity = state->prevDropIntensity;
  }
  
  state->prevDropIntensity = dropIntensity;
  
  // Scale timings by calmness and speed
  uint32_t scaledDropDuration = DROP_DURATION * calmness / speedFactor;
  uint32_t scaledBuildupDuration = BUILDUP_DURATION * calmness / speedFactor;
  uint32_t scaledRecoveryDuration = RECOVERY_DURATION * calmness / speedFactor;
  
  // State machine logic
  switch (state->state) {
    case STATE_WAITING: {
      // Waiting for buildup
      if (dropIntensity > 0.15f && state->smoothedVolume > volumeThreshold) {
        // Transition to buildup state
        state->state = STATE_BUILDUP;
        state->stateStartTime = now;
        state->dropIntensity = dropIntensity;
      }
      // Simple ambient pattern when waiting
      state->colorOffset = (state->colorOffset + 1) % 256;
      break;
    }
      
    case STATE_BUILDUP: {
      // During buildup
      if (dropIntensity > 0.55f && stateElapsed > 500) {
        // Strong drop detected, transition to drop state
        state->state = STATE_DROP;
        state->stateStartTime = now;
        state->dropIntensity = max(dropIntensity, state->dropIntensity);
        
        // Reset color offset for drop effect
        state->colorOffset = random8();
      } 
      else if (stateElapsed > scaledBuildupDuration) {
        // Buildup timeout without drop, return to waiting
        state->state = STATE_WAITING;
        state->stateStartTime = now;
      }
      
      // Update intensity during buildup - gradually increasing
      state->intensity = min(0.8f, stateElapsed / (float)scaledBuildupDuration * state->dropIntensity);
      
      // Smoother color movement during buildup (slower)
      if (stateElapsed % 4 == 0) state->colorOffset = (state->colorOffset + 1) % 256;
      break;
    }
      
    case STATE_DROP: {
      // During drop
      if (stateElapsed > scaledDropDuration) {
        // Drop phase complete, transition to recovery
        state->state = STATE_RECOVERY;
        state->stateStartTime = now;
      }
      
      // Maximum intensity during drop with slight decay
      float dropProgress = stateElapsed / (float)scaledDropDuration;
      if (dropProgress < 0.3f) {
        // Increase to peak during first 30% of drop
        state->intensity = min(1.0f, state->dropIntensity * (1.0f + dropProgress));
      } else {
        // Gradual decrease during remaining 70%
        state->intensity = max(0.2f, state->dropIntensity * (1.8f - dropProgress));
      }
      
      // Smoother color movement during drop (slightly slower)
      if (stateElapsed % 2 == 0) state->colorOffset = (state->colorOffset + 1) % 256;
      break;
    }
      
    case STATE_RECOVERY: {
      // Recovery phase after drop
      if (stateElapsed > scaledRecoveryDuration) {
        // Recovery complete, return to calm state
        state->state = STATE_CALM;
        state->stateStartTime = now;
      }
      
      // Gradually decrease intensity during recovery
      state->intensity = max(0.0f, state->dropIntensity * (1.0f - stateElapsed / (float)scaledRecoveryDuration));
      
      // Slower color movement during recovery
      if (stateElapsed % 5 == 0) state->colorOffset = (state->colorOffset + 1) % 256;
      break;
    }
      
    case STATE_CALM: {
      // Brief calm period before returning to waiting
      if (stateElapsed > 2000) {
        state->state = STATE_WAITING;
        state->stateStartTime = now;
      }
      
      // Minimal intensity during calm
      state->intensity = max(0.0f, 0.2f - (stateElapsed / 2000.0f * 0.2f));
      
      // Even slower color changes during calm
      if (stateElapsed % 8 == 0) state->colorOffset = (state->colorOffset + 1) % 256;
      break;
    }
  }
  
  // Smooth the intensity for rendering to reduce flicker
  float currentIntensity = state->intensity; // Use the raw intensity for logic
  state->smoothedIntensity = state->smoothedIntensity * 0.90f + currentIntensity * 0.10f; // Reduced smoothing slightly
  float displayIntensity = state->smoothedIntensity;
  
  // Increase intensity slightly to make effect more noticeable
  displayIntensity = min(1.0f, displayIntensity * 1.4f); // Increased visual boost
  
  // MODIFIED: Enhanced visualization optimized for mandala configuration
  // Create radial patterns that flow outward from center
  for (int i = 0; i < SEGLEN; i++) {
    // For mandala, calculate distance from center
    float distFromCenter = (float)i / SEGLEN;
    
    // Get base time factor adjusted by speed and intensity
    uint32_t timebase = now / (15 + (8 / speedFactor));
    
    // Create different patterns based on state
    uint32_t color;
    float brightness = 1.0f;
    
    switch (state->state) {
      case STATE_WAITING: {
        // ENHANCED: More dynamic waiting pattern to show more sound reactivity
        // Pulse from center that reacts to volume
        float volFactor = state->smoothedVolume / 255.0f;
        float pulse = (sin(timebase / 250.0f) + 1.0f) / 2.0f;
        float modPulse = pulse * (0.5f + volFactor * 0.5f); // Volume modulates pulse
        
        // Use inverted distance to make brighter toward center
        float invertDist = 1.0f - distFromCenter;
        brightness = 0.3f + invertDist * 0.7f * modPulse;
        
        // Subtle hue rotation based on volume and distance
        uint8_t hue = (state->colorOffset + (uint8_t)(distFromCenter * 64)) % 256;
        color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
        break;
      }
        
      case STATE_BUILDUP: {
        // ENHANCED: More visible buildup effect
        float buildupProgress = stateElapsed / (float)scaledBuildupDuration;
        
        // Create rippling waves that increase in frequency and brightness with buildup
        float freq = 1.0f + buildupProgress * 4.0f; // Increasing frequency
        float wavePhase = distFromCenter * 6.0f * freq - (timebase / (50.0f / buildupProgress));
        float wave = (sin(wavePhase) + 1.0f) / 2.0f;

        // Increasing brightness and movement with buildup
        brightness = 0.4f + (buildupProgress * 0.6f * (0.5f + wave * 0.5f));
        
        // Color shift that speeds up during buildup
        uint8_t hueShift = buildupProgress * 85; // Greater color shift as buildup progresses
        uint8_t hue = (state->colorOffset + (uint8_t)(distFromCenter * 128) + hueShift) % 256;
        color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
        break;
      }
        
      case STATE_DROP: {
        // ENHANCED: More dramatic drop effect
        float dropProgress = stateElapsed / (float)scaledDropDuration;
        
        // Create intense rippling waves that expand from center during drop
        float waveSpeed = 400.0f - dropProgress * 250.0f;
        float wavePhase = (distFromCenter * 10.0f) - (timebase / waveSpeed);
        float wave = (sin(wavePhase) + 1.0f) / 2.0f;
        
        // Pulse brightness with the beat during drop
        float beatPulse = (sin(timebase / 130.0f) + 1.0f) / 2.0f;
        float distEffect = state->dropIntensity * (1.0f - distFromCenter * 0.5f); // Brighter toward center
        brightness = distEffect * (0.7f + beatPulse * 0.3f);
        
        // Dramatic color shift during drop
        uint8_t hueOffset = (timebase / 15) % 256;
        uint8_t hue = (state->colorOffset + hueOffset + (uint8_t)(distFromCenter * 40)) % 256;
        color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
        break;
      }
        
      case STATE_RECOVERY: {
        // Calming outward waves during recovery
        // Recovery waves are slower and more spread out
        float recoveryProgress = stateElapsed / (float)scaledRecoveryDuration;
        
        // Gentle waves moving outward
        float wave = sin(distFromCenter * 3.0f * PI - timebase / 150.0f);
        
        // Brightness fades as recovery progresses
        brightness = 0.3f + wave * (0.7f - recoveryProgress * 0.5f) * state->intensity;
        
        // Color shift slows down during recovery
        uint8_t hueShift = distFromCenter * 40.0f;
        uint8_t hue = state->colorOffset + hueShift;
        color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
        break;
      }
        
      case STATE_CALM: {
        // Minimal animation during calm state
        // Very subtle movement
        float calmProgress = stateElapsed / 2000.0f;
        float wave = sin(distFromCenter * PI * 2.0f + timebase / 1000.0f) * 0.5f + 0.5f;
        
        // Low brightness that fades out
        brightness = (0.3f - calmProgress * 0.2f) * (0.7f + wave * 0.3f);
        
        // Gentle color gradient
        uint8_t hue = state->colorOffset + (distFromCenter * 25);
        color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
        break;
      }
    }
    
    // Apply final brightness adjustment based on drop intensity and volume
    brightness = constrain(brightness, 0.0f, 1.0f);
    float volumeFactor = max(0.5f, min(1.0f, state->smoothedVolume / 255.0f));
    brightness *= volumeFactor;
    
    // Apply brightness to color
    uint8_t r = ((color >> 16) & 0xFF) * brightness;
    uint8_t g = ((color >> 8) & 0xFF) * brightness;
    uint8_t b = (color & 0xFF) * brightness;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  // Debug output
  if (BASS_DROP_DEBUG && SEGENV.call % 32 == 0) {
    const char* stateLabels[] = {"Waiting", "Buildup", "Drop", "Recovery", "Calm"};
    Serial.printf("EXP-BASS-DROP: State=%s Vol=%.1f Drop=%.2f Int=%.2f Elapsed=%d\n",
      stateLabels[state->state], state->smoothedVolume, dropIntensity, state->intensity, stateElapsed);
  }
  
  return FRAMETIME;
} 