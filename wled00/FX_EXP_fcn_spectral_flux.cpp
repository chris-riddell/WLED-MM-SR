#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include <cmath>

#define NUM_FFT_BINS 16
#define FLUX_HISTORY_SIZE 6
#define MIN_VOLUME_THRESHOLD 30.0f
#define FLUX_THRESHOLD 50.0f
#define SPECTRAL_FLUX_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);
extern bool hasFFTData(um_data_t *um_data);

uint16_t mode_spectral_flux(void) {
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
  
  // Allocate memory for flux history and previous FFT
  const int dataSize = sizeof(float) * FLUX_HISTORY_SIZE + sizeof(uint8_t) * NUM_FFT_BINS;
  if (!SEGENV.allocateData(dataSize)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  // Parse the data - first FLUX_HISTORY_SIZE floats are for flux history
  float* fluxHistory = reinterpret_cast<float*>(SEGENV.data);
  
  // The remaining space is for the previous FFT data
  uint8_t* prevFFT = reinterpret_cast<uint8_t*>(SEGENV.data) + sizeof(float) * FLUX_HISTORY_SIZE;
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
    
    // Initialize flux history
    for (int i = 0; i < FLUX_HISTORY_SIZE; i++) {
      fluxHistory[i] = 0;
    }
    
    // Initialize previous FFT
    for (int i = 0; i < NUM_FFT_BINS; i++) {
      prevFFT[i] = 0;
    }
  }
  
  // Speed controls effect responsiveness and pattern flow
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, 1, 5);
  float responsiveness = map_float(SEGMENT.speed, 0, 255, 0.1f, 0.5f);
  
  // Sensitivity affects minimum volume threshold and flux detection
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 80.0f, MIN_VOLUME_THRESHOLD);
  float fluxSensitivity = map_float(SEGMENT.intensity, 0, 255, 1.5f, 0.5f);
  float fluxThreshold = FLUX_THRESHOLD * fluxSensitivity;
  
  // Update color movement counter
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  
  // Calculate spectral flux (sum of differences between current and previous FFT)
  float currentFlux = 0;
  
  if (volume > volumeThreshold) {
    for (int i = 0; i < NUM_FFT_BINS; i++) {
      // Only consider positive changes (increases in energy)
      float diff = max(0.0f, (float)fftData[i] - (float)prevFFT[i]);
      currentFlux += diff;
    }
    
    // Update previous FFT for next frame
    for (int i = 0; i < NUM_FFT_BINS; i++) {
      prevFFT[i] = fftData[i];
    }
  }
  
  // Update flux history
  static uint8_t historyIndex = 0;
  fluxHistory[historyIndex] = currentFlux;
  historyIndex = (historyIndex + 1) % FLUX_HISTORY_SIZE;
  
  // Calculate average flux from history for stability
  float avgFlux = 0;
  for (int i = 0; i < FLUX_HISTORY_SIZE; i++) {
    avgFlux += fluxHistory[i];
  }
  avgFlux /= FLUX_HISTORY_SIZE;
  
  // Detect significant spectral flux (sonic transitions)
  bool fluxEvent = avgFlux > fluxThreshold;
  
  // Static variables for tracking flux events
  static uint32_t lastFluxEvent = 0;
  static float fluxEventIntensity = 0;
  
  // Update flux event 
  uint32_t now = millis();
  
  if (fluxEvent && now - lastFluxEvent > 500) {  // Minimum 500ms between events
    fluxEventIntensity = map_float(avgFlux, fluxThreshold, fluxThreshold * 3.0f, 0.5f, 1.0f);
    fluxEventIntensity = constrain(fluxEventIntensity, 0.5f, 1.0f);
    lastFluxEvent = now;
  } else {
    // Decay flux event intensity
    fluxEventIntensity *= 0.95f;
  }
  
  // Apply spectral flux visualization
  for (int i = 0; i < SEGLEN; i++) {
    // Base color moves slowly
    uint8_t baseHue = SEGENV.aux0;
    
    // Apply positional effects
    float posRatio = (float)i / SEGLEN;
    
    // Create flowing pattern that changes direction on flux events
    float flowDirection = (avgFlux > fluxThreshold * 0.5f) ? -1.0f : 1.0f;
    float flowOffset = (now / 50.0f) * flowDirection;
    float flowPosition = fmod(posRatio * 5.0f + flowOffset, 2.0f);
    if (flowPosition > 1.0f) flowPosition = 2.0f - flowPosition;  // Triangle wave
    
    // Modify hue based on flow position and flux events
    uint8_t hue = (int)(baseHue + (flowPosition * 128) + (fluxEventIntensity * 85)) % 256;
    
    // Calculate brightness based on flux
    uint8_t brightness;
    
    if (fluxEventIntensity > 0.1f) {
      // During flux events, create wave patterns
      float wave = sin(posRatio * TWO_PI * 3 + now / 100.0f);
      brightness = map(volume, volumeThreshold, 255, 64, 255) * (0.8f + 0.2f * wave);
      
      // Add flash effect during strong flux events
      if (fluxEventIntensity > 0.7f) {
        brightness = brightness * (1.0f + fluxEventIntensity * 0.3f);
      }
    } else {
      // Normal brightness when no flux events
      brightness = map(volume, volumeThreshold, 255, 64, 255);
    }
    
    // Get color from palette
    uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply brightness
    uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
    uint8_t g = ((color >> 8) & 0xFF) * brightness / 255;
    uint8_t b = (color & 0xFF) * brightness / 255;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  // Debug output
  if (SPECTRAL_FLUX_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("SPECTRAL_FLUX: Vol=%.1f Flux=%.1f Threshold=%.1f Event=%d Intensity=%.2f\n",
      volume, avgFlux, fluxThreshold, fluxEvent ? 1 : 0, fluxEventIntensity);
  }
  
  return FRAMETIME;
} 