#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include <cmath>

#define MIN_VOLUME_THRESHOLD 30.0f
#define BASS_DROP_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Define animation states
#define STATE_IDLE 0
#define STATE_BUILDUP 1
#define STATE_DROP 2
#define STATE_SUSTAIN 3
#define STATE_DECAY 4

// Animation timing parameters
#define DROP_DURATION 2000     // Duration of main effect in ms
#define BUILDUP_DURATION 1500  // Duration of buildup effect in ms
#define SUSTAIN_DURATION 1500  // How long to maintain peak effect after drop
#define DECAY_DURATION 1500    // How long to fade out effect after sustain

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
  
  // Allocate memory for effect state
  struct DropState {
    uint8_t state;            // Current animation state
    uint32_t stateStartTime;  // When current state began
    float intensity;          // Current effect intensity (0.0-1.0)
    float dropIntensity;      // Intensity of the detected drop (0.0-1.0)
    uint8_t colorOffset;      // Color offset for animation
  };
  
  if (!SEGENV.allocateData(sizeof(DropState))) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  DropState* state = reinterpret_cast<DropState*>(SEGENV.data);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    state->state = STATE_IDLE;
    state->stateStartTime = 0;
    state->intensity = 0;
    state->dropIntensity = 0;
    state->colorOffset = 0;
  }
  
  // Speed controls animation speed
  float speedFactor = map_float(SEGMENT.speed, 0, 255, 0.5f, 2.0f);
  
  // Sensitivity affects bass drop detection threshold
  float sensitivity = map_float(SEGMENT.intensity, 0, 255, 0.3f, 1.0f);
  
  // Get current time
  uint32_t now = millis();
  uint32_t stateElapsed = now - state->stateStartTime;
  
  // Detect bass drop
  float dropIntensity = detectBassDropIntensity(um_data) * sensitivity;
  
  // State machine for bass drop animation
  switch (state->state) {
    case STATE_IDLE:
      // In idle state, watch for buildup or drop
      if (dropIntensity >= 0.7f) {
        // Strong bass drop detected - go straight to drop state
        state->state = STATE_DROP;
        state->stateStartTime = now;
        state->dropIntensity = dropIntensity;
        state->colorOffset = random8(); // Random color for this drop
      } else if (dropIntensity >= 0.3f) {
        // Buildup detected
        state->state = STATE_BUILDUP;
        state->stateStartTime = now;
        state->intensity = dropIntensity;
      }
      break;
      
    case STATE_BUILDUP:
      // In buildup state, intensity grows gradually
      if (dropIntensity >= 0.7f) {
        // Buildup escalated to drop
        state->state = STATE_DROP;
        state->stateStartTime = now;
        state->dropIntensity = dropIntensity;
      } else if (stateElapsed > BUILDUP_DURATION) {
        // Buildup timed out without drop
        state->state = STATE_IDLE;
      } else {
        // Continue buildup with increasing intensity
        float progress = (float)stateElapsed / BUILDUP_DURATION;
        state->intensity = max(dropIntensity, state->intensity * (1.0f - progress) + progress * 0.7f);
      }
      break;
      
    case STATE_DROP:
      // Bass drop animation
      if (stateElapsed > DROP_DURATION) {
        // Move to sustain phase
        state->state = STATE_SUSTAIN;
        state->stateStartTime = now;
      } else {
        // During drop, intensity rises quickly to peak
        float progress = (float)stateElapsed / DROP_DURATION;
        state->intensity = state->dropIntensity * (1.0f - pow(1.0f - progress, 2));
      }
      break;
      
    case STATE_SUSTAIN:
      // Sustain the peak effect
      if (stateElapsed > SUSTAIN_DURATION) {
        // Begin decay
        state->state = STATE_DECAY;
        state->stateStartTime = now;
      }
      // Keep intensity at peak during sustain
      state->intensity = state->dropIntensity;
      break;
      
    case STATE_DECAY:
      // Decay the effect
      if (stateElapsed > DECAY_DURATION) {
        // Back to idle
        state->state = STATE_IDLE;
      } else {
        // Gradual fade out
        float progress = (float)stateElapsed / DECAY_DURATION;
        state->intensity = state->dropIntensity * (1.0f - progress);
      }
      break;
  }
  
  // Apply the effect based on current state
  switch (state->state) {
    case STATE_IDLE:
      // Background pulsing effect when idle
      {
        uint8_t baseBrightness = map(volume, MIN_VOLUME_THRESHOLD, 255, 20, 100);
        float pulse = (sin(now / 1000.0f) + 1.0f) / 2.0f;  // 0.0-1.0 pulse
        uint8_t brightness = baseBrightness + pulse * 30;
        
        for (int i = 0; i < SEGLEN; i++) {
          uint8_t hue = (i * 256 / SEGLEN + now / 100) % 256;
          uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
          
          // Apply brightness
          uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
          uint8_t g = ((color >> 8) & 0xFF) * brightness / 255;
          uint8_t b = (color & 0xFF) * brightness / 255;
          
          SEGMENT.setPixelColor(i, r, g, b);
        }
      }
      break;
      
    case STATE_BUILDUP:
      // Buildup effect - increasing waves from center
      {
        uint8_t baseBrightness = map(volume, MIN_VOLUME_THRESHOLD, 255, 50, 150);
        float intensity = state->intensity;
        
        // Calculate wave parameters
        float waveSpeed = 0.2f + intensity * 2.0f;  // Speed increases with intensity
        float waveFreq = 1.0f + intensity * 5.0f;   // Frequency increases with intensity
        
        for (int i = 0; i < SEGLEN; i++) {
          // Calculate distance from center (0.0-1.0)
          float posRatio = abs((float)(i - SEGLEN/2) / (SEGLEN/2));
          
          // Create waves moving outward from center
          float wave = sin(posRatio * PI * waveFreq + now / (1000.0f / waveSpeed));
          
          // Calculate brightness that increases with intensity
          uint8_t brightness = baseBrightness + intensity * 105 * (0.5f + 0.5f * wave);
          
          // Use hue that shifts with time and intensity
          uint8_t hue = (i * 128 / SEGLEN + (int)(now / 30) + (int)(intensity * 128)) % 256;
          uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
          
          // Apply brightness
          uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
          uint8_t g = ((color >> 8) & 0xFF) * brightness / 255;
          uint8_t b = (color & 0xFF) * brightness / 255;
          
          SEGMENT.setPixelColor(i, r, g, b);
        }
      }
      break;
      
    case STATE_DROP:
    case STATE_SUSTAIN:
    case STATE_DECAY:
      // Bass drop effect - intense pulsing spiral
      {
        uint8_t baseBrightness = map(volume, MIN_VOLUME_THRESHOLD, 255, 100, 255);
        float intensity = state->intensity;
        
        // Calculate animation parameters
        float rotationSpeed = intensity * 5.0f * speedFactor;  // Rotation speed increases with intensity
        float pulseSpeed = intensity * 15.0f * speedFactor;    // Pulse speed increases with intensity
        float width = 0.3f + intensity * 0.5f;                 // Width increases with intensity
        
        for (int i = 0; i < SEGLEN; i++) {
          // Calculate position ratio (0.0-1.0)
          float posRatio = (float)i / SEGLEN;
          
          // Create spiral effect
          float spiral = fmod(posRatio * 3.0f + (now / (1000.0f / rotationSpeed)), 1.0f);
          
          // Add pulsing
          float pulse = sin(now / (1000.0f / pulseSpeed)) * 0.5f + 0.5f;
          
          // Calculate brightness using spiral and pulse
          float brightnessFactor;
          if (spiral < width) {
            // In the bright part of the spiral
            brightnessFactor = 0.7f + 0.3f * pulse;
          } else {
            // In the dim part of the spiral
            brightnessFactor = 0.2f + 0.2f * pulse;
          }
          
          // Apply intensity to brightness
          uint8_t brightness = baseBrightness * brightnessFactor * intensity;
          
          // Use color based on drop pattern
          int temp = state->colorOffset;
          temp += i * 3;
          temp += (int)(now / 30);
          uint8_t hue = (i * 128 / SEGLEN + (int)(now / 30) + (int)(intensity * 128)) % 256;
          uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
          
          // Apply brightness
          uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
          uint8_t g = ((color >> 8) & 0xFF) * brightness / 255;
          uint8_t b = (color & 0xFF) * brightness / 255;
          
          SEGMENT.setPixelColor(i, r, g, b);
        }
      }
      break;
  }
  
  // Debug output
  if (BASS_DROP_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("BASS_DROP: Vol=%.1f Drop=%.2f State=%d Elapsed=%u Int=%.2f\n",
      volume, dropIntensity, state->state, stateElapsed, state->intensity);
  }
  
  return FRAMETIME;
} 