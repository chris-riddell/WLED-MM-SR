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
// IMPROVED: Even slower fade rates for better persistence
#define FADE_RATE_MIN 2         // Minimum fade rate (faster)
#define FADE_RATE_MAX 10        // Maximum fade rate (slower)
// IMPROVED: Much slower fade rates for spectrum visualization
#define SPECTRUM_FADE_RATE_MIN 0.98f   // Faster fade at minimum speed (was 0.97f)
#define SPECTRUM_FADE_RATE_MAX 0.999f // Ultra-slow fade at maximum speed (was 0.9975f)
#define SPECTRUM_MIN_THRESHOLD 30.0f   // Minimum threshold for spectrum display
#define COLOR_SPEED_MIN 1       // Minimum color change speed
#define COLOR_SPEED_MAX 5       // Maximum color change speed
// IMPROVED: Peak hold constants for better visualization
#define PEAK_HOLD_MIN 4        // Minimum frames to hold peak at lowest speed
#define PEAK_HOLD_MAX 48       // Maximum frames to hold peak at highest speed
#define DEBUG_FFT_SPECTRUM 0    // Setting debug flag to 0
#define DEBUG_INTERVAL 250      // Debug output interval in ms
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Frequency weighting factors (weights sum to 1.0)
// ENHANCED: Redistributed to give more space to mid and high frequencies
// Original weights: Bass: 37%, Mid: 36%, High: 27%
// New weights: Bass: 35%, Mid: 38%, High: 27% (higher frequency bins use more LEDs)
const float FREQ_WEIGHTS[] = {
  0.10f, 0.09f, 0.08f, 0.08f,  // Bass (0-3): 35% total
  0.07f, 0.07f, 0.06f, 0.06f, 0.06f, 0.06f,  // Mid (4-9): 38% total
  0.05f, 0.05f, 0.05f, 0.04f, 0.04f, 0.04f   // High (10-15): 27% total
};

// Frequency sensitivity compensation - higher bins need more boost
// ENHANCED: More aggressive compensation for higher frequencies
const float FREQ_COMPENSATION[] = {
  0.6f, 0.7f, 0.8f, 0.9f,      // Reduce bass sensitivity
  1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f,  // Normal/boosted mids
  2.0f, 2.4f, 2.8f, 3.2f, 3.6f, 4.0f   // FURTHER BOOSTED highs (index 10-15)
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

// Improved FFT spectrum analyzer with better peak tracking and fade control
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
  
  // Get FFT data
  uint8_t* fftResult = (uint8_t*)um_data->u_data[2];
  float volumeSmth = 0;
  if (um_data->u_data[0]) {
    volumeSmth = *(float*)um_data->u_data[0];  // General volume
  }
  
  static uint32_t lastDebugTime = 0;
  bool shouldDebug = DEBUG_FFT_SPECTRUM && (millis() - lastDebugTime > DEBUG_INTERVAL);
  if (shouldDebug) lastDebugTime = millis();
  
  // Allocate memory for peak and height tracking
  uint16_t dataSize = sizeof(uint16_t) * 2 * NUM_FFT_BINS + sizeof(uint8_t) * NUM_FFT_BINS;
  if (!SEGENV.allocateData(dataSize)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  uint16_t* heights = reinterpret_cast<uint16_t*>(SEGENV.data);
  uint16_t* peaks = heights + NUM_FFT_BINS;
  uint8_t* peakAges = reinterpret_cast<uint8_t*>(peaks + NUM_FFT_BINS);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    memset(heights, 0, sizeof(uint16_t) * NUM_FFT_BINS);
    memset(peaks, 0, sizeof(uint16_t) * NUM_FFT_BINS);
    memset(peakAges, 0, sizeof(uint8_t) * NUM_FFT_BINS);
    SEGENV.aux0 = 0;  // Color movement counter
  }
  
  // Always update color movement counter, even when quiet, for background animation
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, COLOR_SPEED_MIN, COLOR_SPEED_MAX);
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  uint8_t baseColorIndex = SEGENV.aux0;
  
  // Check if overall volume is above minimum threshold
  if (volumeSmth < MIN_VOLUME_THRESHOLD) {
    // Volume is too low: Fade towards a dim background instead of black
    uint32_t dimColor = SEGMENT.color_from_palette(baseColorIndex, true, PALETTE_SOLID_WRAP, 0); // Use palette color
    uint8_t dimLevel = 10; // Very dim level (out of 255)

    // Extract dim RGB
    uint8_t rDim = ((dimColor >> 16) & 0xFF) * dimLevel / 255;
    uint8_t gDim = ((dimColor >> 8) & 0xFF) * dimLevel / 255;
    uint8_t bDim = (dimColor & 0xFF) * dimLevel / 255;
    uint32_t targetDimColor = RGBW32(rDim, gDim, bDim, 0);

    // Fade each pixel towards the dim color
    for (int i = 0; i < SEGLEN; i++) {
      uint32_t currentColor = SEGMENT.getPixelColor(i);
      uint32_t fadedColor = color_blend(currentColor, targetDimColor, FADE_RATE_MAX);
      SEGMENT.setPixelColor(i, fadedColor);
    }
    
    // Reset heights and peaks when volume is low to prevent lingering artifacts
    memset(heights, 0, sizeof(uint16_t) * NUM_FFT_BINS);
    memset(peaks, 0, sizeof(uint16_t) * NUM_FFT_BINS);
    memset(peakAges, 0, sizeof(uint8_t) * NUM_FFT_BINS);
    
    if (shouldDebug) {
      Serial.printf("FFT-SPECTRUM: Volume too low: %.1f < %.1f\n", volumeSmth, MIN_VOLUME_THRESHOLD);
    }
    
    return FRAMETIME;
  }

  // IMPROVED: Speed controls color change rate and fade rate
  // Higher speed value = SLOWER fade, for better persistence of visualization
  
  // IMPROVED: Exponential mapping for fade rate to get much slower fades at high speeds
  // At speed=0: fade is faster (0.97f)
  // At speed=255: fade is extremely slow (0.9975f)
  float fadeRate = map_float(SEGMENT.speed, 0, 255, SPECTRUM_FADE_RATE_MIN, SPECTRUM_FADE_RATE_MAX);
  
  // IMPROVED: Custom1 now adds additional fade slowdown at high values
  // At custom1=0: no change
  // At custom1=255: extra slow fade (square the fade rate)
  if (SEGMENT.custom1 > 128) {
    // Apply extra slowdown to fade rate (more effective at higher values)
    // Makes fade even slower for extended persistence
    float extraSlowdown = map_float(SEGMENT.custom1, 128, 255, 1.0f, 1.5f);
    fadeRate = pow(fadeRate, 1.0f / extraSlowdown);
  }
  
  // IMPROVED: Peak hold time now controlled by custom2 slider
  // At custom2=0: peaks hold for a short time (PEAK_HOLD_MIN frames)
  // At custom2=255: peaks hold for a long time (PEAK_HOLD_MAX frames)
  uint8_t peakHoldTime = map(SEGMENT.custom2, 0, 255, PEAK_HOLD_MIN, PEAK_HOLD_MAX);
  
  // Sensitivity affects threshold scaling
  // Higher intensity = MORE sensitivity (lower threshold)
  float sensitivityFactor = map_float(SEGMENT.intensity, 0, 255, 1.6f, 0.4f);
  uint8_t baseThreshold = MIN_THRESHOLD * sensitivityFactor;
  
  // Create section boundaries based on weighted distribution
  int sectionBoundaries[NUM_FFT_BINS + 1];
  sectionBoundaries[0] = 0;
  float totalLEDs = 0;
  
  // ENHANCED: Now using 14 instead of 16 frequency bins for display
  // This drops the highest 2 bins which often have little energy and creates dead space
  const int DISPLAY_BINS = 14; // Drop the last 2 bins
  
  // Calculate total weight for the bins we'll actually use
  float usedWeightsSum = 0;
  for (int i = 0; i < DISPLAY_BINS; i++) {
    int reverseIndex = DISPLAY_BINS - 1 - i;
    usedWeightsSum += FREQ_WEIGHTS[reverseIndex];
  }
  
  // MODIFIED: For mandala configuration, we want to map frequencies from center outward
  // Reverse the order of bin mapping so bass (low index) is at center (low LED index)
  // and treble (high index) is at edge (high LED index)
  for (int i = 0; i < DISPLAY_BINS; i++) {
    // Use reverse index to put low frequencies at center
    int reverseIndex = DISPLAY_BINS - 1 - i;
    
    // Normalize the weight to ensure we use the full strip
    float normalizedWeight = FREQ_WEIGHTS[reverseIndex] / usedWeightsSum;
    totalLEDs += normalizedWeight * SEGLEN;
    
    sectionBoundaries[i + 1] = round(totalLEDs);
  }
  
  // Ensure the last boundary exactly matches the segment length
  sectionBoundaries[DISPLAY_BINS] = SEGLEN;
  
  // Fade existing pixels slightly to allow for background animation or dimming
  uint32_t dimColor = SEGMENT.color_from_palette(baseColorIndex, true, PALETTE_SOLID_WRAP, 0); // Use palette color
  uint8_t dimLevel = 5; // Very dim level for background
  uint8_t rDim = ((dimColor >> 16) & 0xFF) * dimLevel / 255;
  uint8_t gDim = ((dimColor >> 8) & 0xFF) * dimLevel / 255;
  uint8_t bDim = (dimColor & 0xFF) * dimLevel / 255;
  uint32_t targetBgColor = RGBW32(rDim, gDim, bDim, 0);

  for(int i=0; i<SEGLEN; i++) {
    uint32_t current = SEGMENT.getPixelColor(i);
    SEGMENT.setPixelColor(i, color_blend(current, targetBgColor, FADE_RATE_MAX + 5)); // Fade slightly faster than volume fade
  }
  
  // Calculate bar heights from FFT data and handle peaks
  for (uint16_t i = 0; i < DISPLAY_BINS; i++) {
    // Get the FFT bin value 
    uint8_t fftBin = fftResult[i];
    
    // Apply multiplier from intensity
    float compensatedValue = fftBin * FREQ_COMPENSATION[i];
    
    // Apply threshold filtering based on sensitivity
    compensatedValue = compensatedValue < baseThreshold ? 0 : compensatedValue - baseThreshold;
    
    // Get old height and apply slow fade
    uint16_t oldHeight = heights[i];
    uint16_t fadedHeight = oldHeight * fadeRate;
    
    // Keep max of faded old value and new value
    uint16_t newHeight = max(fadedHeight, (uint16_t)compensatedValue);
    heights[i] = newHeight;
    
    // Handle peaks with improved hold time and fade
    if (newHeight > peaks[i]) {
      // New peak found
      peaks[i] = newHeight;
      peakAges[i] = 0;
    } else if (peakAges[i] < peakHoldTime) {
      // Peak is still in hold phase
      peakAges[i]++;
    } else {
      // Apply peak fall with slower fade than bars
      // IMPROVED: Peaks fall more slowly than bars for better visualization
      float peakFadeRate = min(fadeRate + 0.005f, 0.999f);
      peaks[i] = peaks[i] * peakFadeRate;
    }
    
    // Get section boundaries for this frequency bin
    // Use reverse mapping to put bass at center
    int reverseIndex = DISPLAY_BINS - 1 - i;
    int startPos = sectionBoundaries[reverseIndex];
    int endPos = sectionBoundaries[reverseIndex + 1] - 1;
    int sectionWidth = endPos - startPos + 1;
    
    // Map bin values to section pixels with gradient
    if (sectionWidth > 0 && newHeight > 0) {
      float sectionNewHeight = (float)sectionWidth * newHeight / MAX_INTENSITY;
      uint16_t heightPx = min((int)sectionNewHeight, sectionWidth);
      
      // Map peak value to section
      float sectionPeakHeight = (float)sectionWidth * peaks[i] / MAX_INTENSITY;
      uint16_t peakPx = min((int)sectionPeakHeight, sectionWidth);
      
      // Draw frequency bar with colorful gradient
      for (int j = 0; j < heightPx; j++) {
        // Calculate pixel position 
        uint16_t pos = startPos + j;
        if (pos >= SEGLEN) continue;  // Safety check
        
        // Calculate color based on position within the WHOLE strip
        // This creates more color variety across the entire visualization
        float posFrac = pos / (float)SEGLEN;
        
        // Create color gradient with hue based on position and base color
        uint8_t colorIndex = (uint8_t)(baseColorIndex + (int)(posFrac * 255)) % 256;
        uint32_t color = SEGMENT.color_from_palette(colorIndex, false, PALETTE_SOLID_WRAP, 0);
        
        // Apply brightness gradient - brighter towards end of each bar
        float brightnessFactor = (j + 1) / (float)sectionWidth;
        brightnessFactor = 0.7f + 0.3f * brightnessFactor;  // Range 0.7-1.0
        
        // ENHANCED: For higher bins, boost brightness to make them more visible
        if (i >= DISPLAY_BINS/2) {
          // Gradually increase brightness for higher frequency bins
          float binBoost = map_float(i, DISPLAY_BINS/2, DISPLAY_BINS-1, 1.0f, 1.3f);
          brightnessFactor *= binBoost;
          brightnessFactor = min(brightnessFactor, 1.3f); // Cap at 1.3
        }
        
        // ADDITIVE BLENDING: Add new bar color to existing background
        uint32_t existingColor = SEGMENT.getPixelColor(pos);
        uint8_t rExist = (existingColor >> 16) & 0xFF;
        uint8_t gExist = (existingColor >> 8) & 0xFF;
        uint8_t bExist = existingColor & 0xFF;

        // Apply brightness scaling
        uint8_t r = min(255, (int)(((color >> 16) & 0xFF) * brightnessFactor));
        uint8_t g = min(255, (int)(((color >> 8) & 0xFF) * brightnessFactor));
        uint8_t b = min(255, (int)((color & 0xFF) * brightnessFactor));
        
        // Combine with existing color
        r = qadd8(r, rExist);
        g = qadd8(g, gExist);
        b = qadd8(b, bExist);
        
        SEGMENT.setPixelColor(pos, r, g, b);
      }
      
      // Draw peak with brighter color if it's visible
      if (peakPx > 0 && peakPx <= sectionWidth) {
        uint16_t peakPos = startPos + peakPx - 1;
        if (peakPos < SEGLEN) {  // Safety check
          // Get current pixel color
          uint32_t baseColor = SEGMENT.getPixelColor(peakPos);
          
          // Make peak brighter than the bar
          // ENHANCED: Make peaks even more visible with higher contrast
          uint8_t r = min(255, (int)(((baseColor >> 16) & 0xFF) * 1.8f));
          uint8_t g = min(255, (int)(((baseColor >> 8) & 0xFF) * 1.8f));
          uint8_t b = min(255, (int)((baseColor & 0xFF) * 1.8f));
          
          SEGMENT.setPixelColor(peakPos, r, g, b);
        }
      }
    }
  }
  
  if (shouldDebug) {
    Serial.printf("EXP-FFT-SPECTRUM: Vol=%.1f Speed=%d FadeRate=%.6f Custom1=%d Custom2=%d PeakHold=%d UsedBins=%d\n",
                 volumeSmth, SEGMENT.speed, fadeRate, SEGMENT.custom1, SEGMENT.custom2, peakHoldTime, DISPLAY_BINS);
  }
  
  return FRAMETIME;
} 