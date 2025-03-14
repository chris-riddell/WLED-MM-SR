#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>

#define TONALITY_HISTORY_SIZE 10
#define MIN_VOLUME_THRESHOLD 30.0f
#define HARMONIC_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);
extern float detectTonality(um_data_t *um_data);

uint16_t mode_harmonic_viz(void) {
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
  
  // Allocate memory for tonality history
  if (!SEGENV.allocateData(sizeof(int) * TONALITY_HISTORY_SIZE)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  int* tonalityHistory = reinterpret_cast<int*>(SEGENV.data);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Color movement counter
    
    // Initialize tonality history
    for (int i = 0; i < TONALITY_HISTORY_SIZE; i++) {
      tonalityHistory[i] = 0;
    }
  }
  
  // Speed controls color change rate and effect responsiveness
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, 1, 3);
  
  // Sensitivity affects minimum volume threshold
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 80.0f, MIN_VOLUME_THRESHOLD);
  
  // Update color movement counter
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 256;
  
  // Only detect tonality if volume is sufficient
  static uint8_t historyIndex = 0;
  
  if (volume > volumeThreshold) {
    // Detect tonality
    float tonality = detectTonality(um_data);
    
    // Update history
    tonalityHistory[historyIndex] = tonality;
    historyIndex = (historyIndex + 1) % TONALITY_HISTORY_SIZE;
  }
  
  // Calculate dominant tonality over history
  int majorCount = 0;
  int minorCount = 0;
  int neutralCount = 0;
  
  for (int i = 0; i < TONALITY_HISTORY_SIZE; i++) {
    if (tonalityHistory[i] > 0) majorCount++;
    else if (tonalityHistory[i] < 0) minorCount++;
    else neutralCount++;
  }
  
  // Determine dominant tonality
  int dominantTonality = 0;
  if (majorCount > minorCount && majorCount > neutralCount) {
    dominantTonality = 1;  // Major
  } else if (minorCount > majorCount && minorCount > neutralCount) {
    dominantTonality = -1;  // Minor
  }
  
  // Set up color schemes based on tonality
  uint8_t baseHue = SEGENV.aux0;
  uint8_t saturation = 255;
  uint8_t brightness = map(volume, volumeThreshold, 255, 64, 255);
  
  // Adjust color scheme based on tonality
  if (dominantTonality > 0) {
    // Major: Bright complementary colors (analogous)
    saturation = 220;
    // Use base hue and analogous colors
  } else if (dominantTonality < 0) {
    // Minor: Deeper, more saturated colors (complementary)
    saturation = 255;
    // Use base hue and complementary colors (opposite on wheel)
    baseHue = (baseHue + 128) % 256;
  } else {
    // Neutral: Desaturated colors
    saturation = 150;
  }
  
  // Visualize with flowing pattern
  for (int i = 0; i < SEGLEN; i++) {
    // Calculate position-based parameters
    float posRatio = (float)i / SEGLEN;
    
    // Create flowing waves using sine
    float wave = sin(posRatio * TWO_PI * 3 + millis() / 1000.0f);
    
    // Adjust hue based on position and tonality
    uint8_t hueOffset;
    if (dominantTonality > 0) {
      // Major: small hue shifts (analogous colors)
      hueOffset = 30 * wave;
    } else if (dominantTonality < 0) {
      // Minor: larger hue shifts
      hueOffset = 60 * wave;
    } else {
      // Neutral: middle ground
      hueOffset = 45 * wave;
    }
    
    // Calculate final hue
    uint8_t hue = (baseHue + hueOffset) % 256;
    
    // Adjust brightness based on wave
    uint8_t waveBrightness = brightness * (0.7f + 0.3f * wave);
    
    // Get color from palette 
    uint32_t color = SEGMENT.color_from_palette(hue, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply saturation and brightness adjustments
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    // Apply saturation (blend with gray of same brightness)
    uint8_t gray = (r + g + b) / 3;
    r = r + ((gray - r) * (255 - saturation)) / 255;
    g = g + ((gray - g) * (255 - saturation)) / 255;
    b = b + ((gray - b) * (255 - saturation)) / 255;
    
    // Apply brightness
    r = (r * waveBrightness) / 255;
    g = (g * waveBrightness) / 255;
    b = (b * waveBrightness) / 255;
    
    SEGMENT.setPixelColor(i, r, g, b);
  }
  
  // Debug output
  if (HARMONIC_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("HARMONIC_VIZ: Vol=%.1f Tonality=%d (Maj:%d Min:%d Neu:%d) Sat=%d\n",
      volume, dominantTonality, majorCount, minorCount, neutralCount, saturation);
  }
  
  return FRAMETIME;
}