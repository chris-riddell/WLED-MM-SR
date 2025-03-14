#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include <cmath>

// Configuration
#define NUM_FFT_BINS 16         // Number of FFT bins to use for visualization
#define MIN_THRESHOLD 8         // Minimum FFT value to trigger visualization
#define MIN_VOLUME_THRESHOLD 30.0f // Absolute minimum volume to consider any activity
#define MAX_INTENSITY 255.0f    // Maximum expected intensity for scaling
#define FADE_RATE_MIN 2         // Minimum fade rate (slower)
#define FADE_RATE_MAX 10        // Maximum fade rate (faster)
#define COLOR_SPEED_MIN 1       // Minimum color change speed
#define COLOR_SPEED_MAX 5       // Maximum color change speed
#define DEBUG_FFT_SPECTRUM 1    // Enable debug output
#define DEBUG_INTERVAL 250      // Debug output interval in ms
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Frequency weighting factors (weights sum to 1.0)
// Gives more LEDs to bass (0-3) and mid (4-9) frequencies
const float FREQ_WEIGHTS[] = {
  0.10f, 0.10f, 0.09f, 0.08f,  // Bass (0-3): 37% total
  0.07f, 0.07f, 0.06f, 0.06f, 0.05f, 0.05f,  // Mid (4-9): 36% total
  0.04f, 0.04f, 0.04f, 0.03f, 0.03f, 0.03f   // High (10-15): 27% total
};

// Frequency sensitivity compensation - higher bins need more boost
// These values multiply the raw FFT values to compensate for natural bias
const float FREQ_COMPENSATION[] = {
  0.6f, 0.7f, 0.8f, 0.9f,      // Reduce bass sensitivity
  1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f,  // Normal/boosted mids
  1.7f, 1.9f, 2.1f, 2.3f, 2.5f, 2.8f   // Significantly boost highs
};

// Helper function for float mapping
static float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Helper to check if audio data is valid
static bool isAudioDataValid(um_data_t *um_data) {
  if (!um_data || !um_data->u_data) return false;
  if (!um_data->u_data[2]) return false;  // FFT data array
  return true;
}

uint16_t mode_fft_spectrum_ar(void) {
  // Get audio data
  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !isAudioDataValid(um_data)) {
    // Fallback pattern for no audio
    uint8_t x = millis() / 20;
    SEGMENT.fill(SEGMENT.color_from_palette(x, false, true, 0));
    return FRAMETIME;
  }
  
  static uint32_t lastDebugTime = 0;
  bool shouldDebug = DEBUG_FFT_SPECTRUM && (millis() - lastDebugTime > DEBUG_INTERVAL);
  if (shouldDebug) lastDebugTime = millis();
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
  }
  
  // Get FFT data and volume data
  uint8_t* fftData = (uint8_t*)um_data->u_data[2];
  float volumeSmth = 0;
  if (um_data->u_data[0]) {
    volumeSmth = *(float*)um_data->u_data[0];  // General volume
  }
  
  // Check if overall volume is above minimum threshold
  if (volumeSmth < MIN_VOLUME_THRESHOLD) {
    // Apply fade to all pixels but don't add new ones when volume is too low
    for (int i = 0; i < SEGLEN; i++) {
      SEGMENT.fadePixelColor(i, FADE_RATE_MAX);  // Fast fade when below threshold
    }
    
    if (shouldDebug) {
      Serial.printf("FFT-SPECTRUM: Volume too low: %.1f < %.1f\n", volumeSmth, MIN_VOLUME_THRESHOLD);
    }
    
    return FRAMETIME;
  }
  
  // Speed controls color change rate
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, COLOR_SPEED_MIN, COLOR_SPEED_MAX);
  // Separate fade rate for visual persistence
  uint8_t fadeRate = map(SEGMENT.speed, 0, 255, FADE_RATE_MIN, FADE_RATE_MAX);
  
  // Sensitivity affects threshold scaling
  // Higher intensity = MORE sensitivity (lower threshold)
  float sensitivityFactor = map_float(SEGMENT.intensity, 0, 255, 1.5f, 0.5f);
  uint8_t baseThreshold = MIN_THRESHOLD * sensitivityFactor;
  
  // Apply natural fade to all pixels
  for (int i = 0; i < SEGLEN; i++) {
    SEGMENT.fadePixelColor(i, fadeRate);
  }
  
  // Update color movement counter - more noticeable changes
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  uint8_t baseColorIndex = SEGENV.aux0;
  
  // Create section boundaries based on weighted distribution
  int sectionBoundaries[NUM_FFT_BINS + 1];
  sectionBoundaries[0] = 0;
  float totalLEDs = 0;
  
  for (int i = 0; i < NUM_FFT_BINS; i++) {
    totalLEDs += FREQ_WEIGHTS[i] * SEGLEN;
    sectionBoundaries[i + 1] = round(totalLEDs);
  }
  
  // Adjust the last boundary to ensure we cover the entire strip
  sectionBoundaries[NUM_FFT_BINS] = SEGLEN;
  
  // Debug - print FFT bin values before visualization
  if (shouldDebug) {
    String fftValues = "FFT: ";
    for (int i = 0; i < NUM_FFT_BINS; i += 2) {
      fftValues += String(fftData[i]) + " ";
    }
    Serial.println(fftValues);
  }
  
  // Visualize each FFT bin in its corresponding section
  bool anyActiveBin = false;
  int activeBins = 0;
  
  for (int bin = 0; bin < NUM_FFT_BINS; bin++) {
    int startPos = sectionBoundaries[bin];
    int endPos = sectionBoundaries[bin + 1];
    int sectionSize = endPos - startPos;
    
    if (sectionSize <= 0) continue;  // Skip empty sections
    
    // Get FFT value and apply frequency-specific compensation
    float rawBinValue = fftData[bin];
    float compensatedValue = rawBinValue * FREQ_COMPENSATION[bin];
    
    // Calculate bin-specific threshold based on bin number
    // Higher frequencies typically need lower thresholds
    float binThresholdMultiplier = 1.0f - (bin * 0.3f / NUM_FFT_BINS);  // Gradually reduce threshold for higher bins
    binThresholdMultiplier = constrain(binThresholdMultiplier, 0.7f, 1.0f);
    uint8_t binThreshold = baseThreshold * binThresholdMultiplier;
    
    // Skip if below threshold
    if (compensatedValue < binThreshold) continue;
    
    anyActiveBin = true;
    activeBins++;
    
    // Calculate intensity (how bright the LEDs should be)
    // Map the bin value to a percentage of full brightness
    float maxExpectedValue = MAX_INTENSITY * FREQ_COMPENSATION[bin];
    float intensity = map_float(compensatedValue, binThreshold, maxExpectedValue, 0.2f, 1.0f);
    intensity = constrain(intensity, 0.2f, 1.0f);  // Minimum 20% brightness when triggered
    
    // Generate a color for this bin based on palette
    uint8_t colorIndex = (baseColorIndex + (bin * 16)) % 256;  // Distinct color per bin
    uint32_t color = SEGMENT.color_from_palette(colorIndex, false, PALETTE_SOLID_WRAP, 0);
    
    // Light up inner LEDs first with more LEDs lit for higher intensity
    int ledsToLight = max(1, int(sectionSize * intensity));
    
    // Debug bin information
    if (shouldDebug) {
      Serial.printf("Bin %d: raw=%d comp=%.1f thresh=%d ledsToLight=%d/%d int=%.2f\n", 
        bin, (int)rawBinValue, compensatedValue, binThreshold, ledsToLight, sectionSize, intensity);
    }
    
    // Apply brightness gradient from inner to outer LEDs
    for (int i = 0; i < ledsToLight && i < sectionSize; i++) {
      int pixelPos = startPos + i;
      
      // Brightness decreases as we move outward
      float distanceRatio = float(i) / ledsToLight;
      float brightnessScale = 1.0f - (distanceRatio * 0.7f);  // At least 30% brightness at edge
      
      // Apply intensity and distance scaling to brightness
      uint8_t brightness = 255 * intensity * brightnessScale;
      
      // Apply brightness to color
      uint8_t r = (color >> 16) & 0xFF;
      uint8_t g = (color >> 8) & 0xFF;
      uint8_t b = color & 0xFF;
      
      r = (r * brightness) / 255;
      g = (g * brightness) / 255;
      b = (b * brightness) / 255;
      
      uint32_t adjustedColor = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
      
      // Set pixel color (don't add - replace for cleaner bin separation)
      SEGMENT.setPixelColor(pixelPos, adjustedColor);
    }
  }
  
  // Debug output
  if (shouldDebug) {
    Serial.printf("FFT-SPECTRUM: Vol=%.1f Speed=%d Fade=%d Color=%d Sens=%d Thresh=%d ActiveBins=%d/%d\n",
      volumeSmth, SEGMENT.speed, fadeRate, baseColorIndex, SEGMENT.intensity, baseThreshold, 
      activeBins, NUM_FFT_BINS);
  }
  
  return FRAMETIME;
} 