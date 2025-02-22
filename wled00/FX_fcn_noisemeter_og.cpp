#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include <cmath>
#include <algorithm>  // For std::max/min

// Define palette wrap modes if not already defined
#define PALETTE_SOLID_WRAP   (strip.paletteBlend == 1 || strip.paletteBlend == 3)

#define LOG_INTERVAL 30 // Number of frames between logs (~0.5 seconds at 60fps)
#define DEBUG_NOISEMETER_OG 1  // Set to 1 to enable debug output
#define DEBUG_BUFFER_SIZE 512   // Size of debug buffer
#define MIN_LENGTH 0  // Minimum display length (base noise level)
#define MAX_LENGTH 5  // Maximum display length
#define VOLUME_HISTORY_SIZE 60  // About 1 second of history at 60fps
#define BASE_VOLUME_ALPHA 0.05f  // Base volume adaptation rate
#define COLOR_FADE_RATE 0.15f   // How fast colors blend (higher = faster)

// Helper function for float mapping (since mapf isn't available)
static float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Helper function for exponential fade
static uint8_t exponentialFade(uint8_t value, uint8_t fadeRate) {
    float factor = powf((255.0f - static_cast<float>(fadeRate)) / 255.0f, 3.0f);
    return static_cast<uint8_t>(static_cast<float>(value) * (1.0f - factor));
}

// Helper function for color interpolation
static uint32_t blendColors(uint32_t color1, uint32_t color2, float ratio) {
    uint8_t r1 = R(color1), g1 = G(color1), b1 = B(color1);
    uint8_t r2 = R(color2), g2 = G(color2), b2 = B(color2);
    
    return RGBW32(
        r1 + (r2 - r1) * ratio,
        g1 + (g2 - g1) * ratio,
        b1 + (b2 - b1) * ratio,
        0
    );
}

// Helper function to get audio data with fallback to simulation
static um_data_t* getAudioData() {
  um_data_t *um_data;
  bool success = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!success) {
    static char errBuf[DEBUG_BUFFER_SIZE];
    snprintf(errBuf, DEBUG_BUFFER_SIZE, "[AUDIO] No audio data available - check microphone connection\n");
    Serial.print(errBuf);
    return nullptr;
  }
  return um_data;
}

uint16_t mode_noisemeter_og(void) {
  um_data_t *um_data = getAudioData();
  if (!um_data) return FRAMETIME;

  float volumeSmth = *(float*)um_data->u_data[0];
  int16_t volumeRaw = *(int16_t*)um_data->u_data[1];

  // Initialize on first call
  static float baseVolume = 0;
  static float smoothedLen = 0;
  static uint32_t targetColors[MAX_LENGTH];
  static uint32_t currentColors[MAX_LENGTH];
  
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Time-based color offset
    baseVolume = std::abs(volumeRaw) * 2.0f;
    smoothedLen = 0;
    for (int i = 0; i < MAX_LENGTH; i++) {
      targetColors[i] = BLACK;
      currentColors[i] = BLACK;
    }
  }

  // Calculate base volume using exponential moving average
  float adaptRate = map_float(SEGMENT.speed, 0, 255, 0.01f, 0.05f);  // Speed affects adaptation rate
  baseVolume = baseVolume * (1.0f - adaptRate) + volumeRaw * adaptRate;
  *(float*)um_data->u_data[1] = baseVolume;

  // Scale volume relative to base with display width influence
  float relativeVolume = baseVolume > 15.0f ? volumeRaw / baseVolume : 0.0f;
  float widthFactor = map_float(SEGMENT.intensity, 0, 255, 0.5f, 2.0f);  // Display width affects scaling
  float scaledVolume = powf(relativeVolume * widthFactor, 2.0f);
  
  // Map to target length with dynamic thresholds based on display width
  uint8_t targetLen = 0;
  float baseThreshold = map_float(SEGMENT.intensity, 0, 255, 0.15f, 0.05f);  // Lower threshold at higher width
  if (scaledVolume > baseThreshold) targetLen = 1;
  if (scaledVolume > baseThreshold * 2.5f) targetLen = 2;
  if (scaledVolume > baseThreshold * 4.5f) targetLen = 3;
  if (scaledVolume > baseThreshold * 7.0f) targetLen = 4;
  if (scaledVolume > baseThreshold * 10.0f) targetLen = 5;

  // Smooth length transitions - speed affects transition rate
  float transitionSpeed = map_float(SEGMENT.speed, 0, 255, 0.15f, 0.4f);
  smoothedLen = smoothedLen + (targetLen - smoothedLen) * transitionSpeed;
  
  // Convert to integer for display
  int maxLen = static_cast<int>(smoothedLen + 0.5f);
  maxLen = std::max(MIN_LENGTH, std::min(maxLen, MAX_LENGTH));

  // Fade handling - speed affects fade rates
  uint8_t baseFadeRate = map(SEGMENT.speed, 0, 255, 252, 180);  // Faster fade at lower speeds
  uint8_t fadeRate = baseFadeRate;
  if (relativeVolume < baseThreshold) {
    fadeRate = baseFadeRate + 3;  // Quick fade when below threshold
  } else if (maxLen > 1) {
    fadeRate = baseFadeRate - 40;  // Slower fade for active display
  }

  // Generate new target colors with speed-based movement
  if (maxLen > 0) {
    // Color movement speed based on speed slider
    uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, 1, 4);
    SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 65535;
    
    // Generate base color index from time
    uint8_t baseIndex = inoise8(SEGENV.aux0, SEGENV.aux0 / 2);
    
    // Apply color to pixels with width-based variation
    float colorSpread = map_float(SEGMENT.intensity, 0, 255, 2.0f, 8.0f);
    for (int i = 0; i < maxLen; i++) {
      uint8_t index = static_cast<uint8_t>(fmod(baseIndex + i * colorSpread, 256.0f));
      targetColors[i] = SEGMENT.color_from_palette(
        index,
        false,
        PALETTE_SOLID_WRAP,
        0
      );
    }
    
    // Clear remaining pixels
    for (int i = maxLen; i < MAX_LENGTH; i++) {
      targetColors[i] = BLACK;
    }
  }

  // Interpolate colors with speed-based blend rate
  float blendRate = map_float(SEGMENT.speed, 0, 255, 0.05f, 0.25f);
  for (int i = 0; i < MAX_LENGTH; i++) {
    currentColors[i] = blendColors(
      currentColors[i],
      targetColors[i],
      blendRate
    );
  }

  // Apply colors and fade
  for (int i = 0; i < SEGLEN; i++) {
    uint8_t fadeAmount = fadeRate;
    if (i < maxLen) {
      fadeAmount = std::max(20u, fadeAmount - 100u);
    }
    
    // Only apply fade to pixels beyond the current length
    if (i >= maxLen) {
      uint32_t color = SEGMENT.getPixelColor(i);
      SEGMENT.setPixelColor(i,
        exponentialFade(R(color), fadeAmount),
        exponentialFade(G(color), fadeAmount),
        exponentialFade(B(color), fadeAmount)
      );
    } else {
      // For active pixels, use the interpolated colors
      SEGMENT.setPixelColor(i, currentColors[i]);
    }
  }

  // Debug output
  if (DEBUG_NOISEMETER_OG && (SEGENV.call % 15 == 0)) {
    Serial.printf("NOISE-OG: Vol[raw=%d base=%.2f rel=%.2f scaled=%.2f] Len[%d/5] Fade[%d]\n",
      volumeRaw, baseVolume, relativeVolume, scaledVolume, maxLen, fadeRate);
  }

  return FRAMETIME;
}