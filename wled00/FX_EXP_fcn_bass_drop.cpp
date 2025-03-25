#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>

// Increased time ranges for smoother transitions
#define BUILDUP_DURATION 3000  // Maximum buildup time (3 seconds) - will be scaled by intensity/speed
#define DROP_DURATION 4000     // Maximum drop time (4 seconds) - will be scaled by intensity/speed
#define RECOVERY_DURATION 3000 // Maximum recovery time (3 seconds) - will be scaled by intensity/speed
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
  }
  
  // Speed controls animation speed and transitions
  float speedFactor = map_float(SEGMENT.speed, 0, 255, 0.3f, 4.0f);  // Significantly expanded range
  
  // Intensity controls "calmness" - higher value = more calm, gradual transitions
  float calmness = map_float(SEGMENT.intensity, 0, 255, 0.2f, 1.5f);
  float sensitivity = map_float(SEGMENT.intensity, 0, 255, 1.2f, 0.25f);  // More sensitivity range, reversed
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 30.0f, 120.0f);  // Upper limit increased
  
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
      // 70% new, 30% old - fast attack
      dropIntensity = rawDropIntensity * 0.7f + state->prevDropIntensity * 0.3f;
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
      if (dropIntensity > 0.3f && state->smoothedVolume > volumeThreshold) {
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
      if (dropIntensity > 0.7f && stateElapsed > 500) {
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
      
      // Slow color movement during buildup
      if (stateElapsed % 2 == 0) state->colorOffset = (state->colorOffset + 1) % 256;
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
      
      // Fast color movement during drop
      state->colorOffset = (state->colorOffset + 2) % 256;
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
      
      // Moderate color movement during recovery
      if (stateElapsed % 3 == 0) state->colorOffset = (state->colorOffset + 1) % 256;
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
      
      // Slow color changes
      if (stateElapsed % 5 == 0) state->colorOffset = (state->colorOffset + 1) % 256;
      break;
    }
  }
  
  // MODIFIED: Enhanced visualization optimized for mandala configuration
  // Create radial patterns that flow outward from center
  for (int i = 0; i < SEGLEN; i++) {
    // For mandala, calculate distance from center
    float distFromCenter = (float)i / SEGLEN;
    
    // Get base time factor adjusted by speed and intensity
    uint32_t timebase = now / (10 + (5 / speedFactor));
    
    // Create different patterns based on state
    uint32_t color;
    float brightness = 1.0f;
    
    switch (state->state) {
      case STATE_WAITING: {
        // Subtle ambient pattern when waiting
        // Gentle pulse from center
        float pulse = (sin(timebase / 200.0f) + 1.0f) / 2.0f;
        float wave = sin(distFromCenter * PI + timebase / 500.0f) * 0.5f + 0.5f;
        
        brightness = 0.3f + (wave * pulse * 0.2f);
        uint8_t hue = state->colorOffset + (distFromCenter * 20);
        color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
        break;
      }
        
      case STATE_BUILDUP: {
        // Building anticipation - waves moving outward faster as buildup increases
        // Create accelerating waves moving outward
        float buildupProgress = stateElapsed / (float)scaledBuildupDuration;
        float waveSpeed = 300.0f + buildupProgress * 700.0f;
        float wave = sin(distFromCenter * 5.0f * PI + timebase / (1000.0f - waveSpeed));
        
        // Pulse brightness with increasing intensity
        float pulse = (sin(timebase / (500.0f - 300.0f * buildupProgress)) + 1.0f) / 2.0f;
        brightness = 0.4f + (wave * pulse * state->intensity * 0.6f);
        
        // Color gradually shifts during buildup
        uint8_t hue = state->colorOffset + (distFromCenter * 30);
        color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
        break;
      }
        
      case STATE_DROP: {
        // Dynamic outward flowing waves during drop
        float dropProgress = stateElapsed / (float)scaledDropDuration;
        float baseFreq = 4.0f + state->intensity * 5.0f; // Frequency increases with intensity
        
        // Primary wave - fast outward movement
        float wave1 = sin(distFromCenter * baseFreq * PI - timebase / (50.0f / state->intensity));
        
        // Secondary wave - slower, phase-shifted
        float wave2 = sin(distFromCenter * (baseFreq * 0.7f) * PI - timebase / (80.0f / state->intensity) + PI/2);
        
        // Combine waves with varying influence
        float combinedWave = (wave1 * 0.7f + wave2 * 0.3f);
        
        // Add radial brightness variation - center pulses brighter during drop
        float centerEffect = (1.0f - distFromCenter) * 0.5f * (sin(timebase / 100.0f) + 1.0f);
        
        // Final brightness with strong center pulse
        brightness = 0.4f + (combinedWave * 0.3f + centerEffect) * state->intensity;
        
        // Dynamic color movement based on drop intensity
        uint8_t hueShift = distFromCenter * 60.0f + dropProgress * 128.0f;
        uint8_t hue = state->colorOffset + hueShift;
        color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
        break;
      }
        
      case STATE_RECOVERY: {
        // Calming outward waves during recovery
        // Recovery waves are slower and more spread out
        float recoveryProgress = stateElapsed / (float)scaledRecoveryDuration;
        
        // Gentle waves moving outward
        float wave = sin(distFromCenter * 3.0f * PI - timebase / 120.0f);
        
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
        float wave = sin(distFromCenter * PI * 2.0f + timebase / 800.0f) * 0.5f + 0.5f;
        
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