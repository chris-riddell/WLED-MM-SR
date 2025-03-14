#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include <cmath>
#include <algorithm>  // For std::max/min

// Define palette wrap modes if not already defined
#define PALETTE_SOLID_WRAP   (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Debug settings
#define LOG_INTERVAL 30        // Number of frames between logs (~0.5 seconds at 60fps)
#define DEBUG_NOISEMETER_OG 1  // Set to 1 to enable debug output
#define DEBUG_BUFFER_SIZE 512  // Size of debug buffer

// Effect settings
#define MIN_LENGTH 0          // Minimum display length (base noise level)
#define MAX_LENGTH 5          // Maximum display length
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
  static float smoothedVol = 0; // Add smoothing for raw volume
  static float lastScaledVol = 0; // Track last scaled volume for additional smoothing
  
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Time-based color offset
    baseVolume = std::max(60.0f, static_cast<float>(std::abs(volumeRaw)) * 1.5f);  // Increased from 40.0f to 60.0f
    smoothedLen = 0;
    smoothedVol = 0;
    lastScaledVol = 0;
  }

  // Clamp raw volume to prevent extreme values
  int16_t clampedVolumeRaw = std::max(static_cast<int16_t>(0), std::min(volumeRaw, static_cast<int16_t>(255)));
  
  // Smooth the raw volume first to reduce jitter
  float volSmoothing = 0.6f;
  smoothedVol = (smoothedVol * volSmoothing) + (std::abs(clampedVolumeRaw) * (1.0f - volSmoothing));

  // Calculate base volume using exponential moving average - slower adaptation
  float adaptRateUp = map_float(SEGMENT.speed, 0, 255, 0.001f, 0.01f);
  float adaptRateDown = map_float(SEGMENT.speed, 0, 255, 0.005f, 0.02f);
  float adaptRate = (smoothedVol > baseVolume) ? adaptRateUp : adaptRateDown;
  
  baseVolume = baseVolume * (1.0f - adaptRate) + smoothedVol * adaptRate;
  baseVolume = std::max(baseVolume, 60.0f);  // Increased from 40.0f to 60.0f
  
  *(float*)um_data->u_data[1] = baseVolume;

  // Scale volume relative to base with display width influence
  float relativeVolume = smoothedVol / baseVolume;
  
  // Cap relative volume to prevent excessive scaling
  relativeVolume = std::min(relativeVolume, 2.5f);  // Reduced from 3.0f to 2.5f
  
  float widthFactor = map_float(SEGMENT.intensity, 0, 255, 0.3f, 1.0f);  // Reduced upper range from 1.2f to 1.0f
  
  // Use a gentler power function
  float scaledVolume = powf(relativeVolume * widthFactor, 1.5f);  // Reduced from 1.8f to 1.5f
  
  // Additional smoothing on scaled volume to prevent jumps
  float scaleSmoothing = 0.4f;
  scaledVolume = (lastScaledVol * scaleSmoothing) + (scaledVolume * (1.0f - scaleSmoothing));
  lastScaledVol = scaledVolume;
  
  // Map to target length with more evenly distributed thresholds
  uint8_t targetLen = 0;
  
  // Check for objectively loud volumes (absolute threshold)
  const float absoluteThreshold = 160.0f;  // Reduced from 180.0f to 160.0f for better absolute detection
  boolean isObjLoud = smoothedVol > absoluteThreshold;
  
  if (isObjLoud) {
    // If objectively loud, go straight to 5/5 LEDs
    targetLen = 5;
  } else {
    // Less sensitive thresholds, except for beat detection
    if (scaledVolume > 0.6f) targetLen = 1;  // Higher threshold for first LED (less background noise)
    if (scaledVolume > 1.1f) targetLen = 2;
    if (scaledVolume > 1.8f) targetLen = 3;
    if (scaledVolume > 2.6f) targetLen = 4;  // Slightly lowered from 2.8f to 2.6f
    if (scaledVolume > 3.5f) targetLen = 5;  // Lowered from 4.0f to 3.5f for better beat detection
  }

  // Add beat detection based on sudden volume change
  static float prevVolume = 0;
  float volumeChange = smoothedVol - prevVolume;
  prevVolume = smoothedVol;
  
  // If we detect a significant volume spike (beat), boost the level
  if (volumeChange > 30.0f && smoothedVol > 80.0f) {
    // Strong beat detected - increase level
    targetLen = std::max(targetLen, (uint8_t)4);  // At least level 4 for strong beats
    
    // If it's a really strong beat, go to level 5
    if (volumeChange > 60.0f && smoothedVol > 100.0f) {
      targetLen = 5;
    }
  }

  // Faster transitions for better beat response
  float transitionSpeed = map_float(SEGMENT.speed, 0, 255, 0.3f, 0.6f); // Increased from 0.25f-0.45f to 0.3f-0.6f
  
  // Different transition speeds for up vs down
  if (targetLen > smoothedLen) {
    // Going up - make it nearly instant
    // Just jump directly to the target with minimal smoothing
    smoothedLen = targetLen - 0.01f; // Almost instant (just slightly below target for visual smoothness)
  } else {
    // Going down - smooth fade out using the regular transition speed
    smoothedLen = smoothedLen + (targetLen - smoothedLen) * transitionSpeed;
  }
  
  int maxLen = static_cast<int>(smoothedLen + 0.5f);
  maxLen = std::max(MIN_LENGTH, std::min(maxLen, MAX_LENGTH));

  // Color movement speed based on speed slider
  uint8_t colorSpeed = map(SEGMENT.speed, 0, 255, 1, 4);
  SEGENV.aux0 = (SEGENV.aux0 + colorSpeed) % 65535;
  
  // Generate base color index from time
  uint8_t baseIndex = inoise8(SEGENV.aux0, SEGENV.aux0 / 2);

  // Apply colors with faster fade for inactive pixels
  uint8_t fadeRate = map(SEGMENT.speed, 0, 255, 240, 180); // Increased lower bound for faster fade at slow speeds
  if (relativeVolume < 0.2f) {
    fadeRate += 10;  // Even quicker fade when below threshold
  } else if (maxLen > 1) {
    fadeRate -= 20;  // Reduced fade difference for active display
  }

  // Apply color to pixels with width-based variation
  float colorSpread = map_float(SEGMENT.intensity, 0, 255, 2.0f, 8.0f);
  for (int i = 0; i < SEGLEN; i++) {
    if (i < maxLen) {
      // Active pixels use palette colors with full brightness
      uint8_t index = static_cast<uint8_t>(fmod(baseIndex + i * colorSpread, 256.0f));
      SEGMENT.setPixelColor(i, SEGMENT.color_from_palette(index, false, PALETTE_SOLID_WRAP, 0));
    } else {
      // Faster fade for inactive pixels
      uint32_t color = SEGMENT.getPixelColor(i);
      SEGMENT.setPixelColor(i,
        exponentialFade(R(color), fadeRate),
        exponentialFade(G(color), fadeRate),
        exponentialFade(B(color), fadeRate)
      );
    }
  }

  // Debug output - add absolute threshold info
  if (DEBUG_NOISEMETER_OG && (SEGENV.call % LOG_INTERVAL == 0)) {
    Serial.printf("NOISE-OG: Vol[raw=%d smooth=%.2f base=%.2f rel=%.2f scaled=%.2f] AbsLoud[%s] Len[%d/5] Fade[%d]\n",
      volumeRaw, smoothedVol, baseVolume, relativeVolume, scaledVolume, 
      isObjLoud ? "YES" : "no", maxLen, fadeRate);
  }

  return FRAMETIME;
}