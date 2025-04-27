/*
 * Noisemeter 13.4 version (FX_fcn_noisemeter134.cpp)
 * 
 * Displays a classic volume meter visualization showing the amplitude of sound 
 * in real-time, similar to traditional VU meters with color variations based on intensity.
 * 
 * Best for: All music types, providing a simple but effective visualization for any 
 * audio content; particularly engaging with dynamic music that has both quiet and 
 * loud sections.
 * 
 * This is the WLED v0.13.4 version of the effect, maintained for compatibility.
 */

#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h" // for USERMOD_ID_AUDIOREACTIVE
#include <cmath> // Required for map_float if not otherwise included

#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Helper function forward declarations (ensure these are accessible, e.g., from audio_utils.h)
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
// extern bool isAudioDataValid(um_data_t *um_data); // Assuming basic check is done below

/*
 * Noisemeter 134 (Based on WS2812FX::mode_noisemeter by Andrew Tuline)
 * Adapted for WLED Segments and Usermod Audio Input
 */
uint16_t mode_noisemeter_134(void) {
  // Get audio data
  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);

  int16_t rawVol = 0;   // Raw audio value (e.g., ADC reading or similar)
  float smoothedVol = 0; // Smoothed audio value (e.g., volumeAgc)

  // Check if audio data is valid
  if (hasAudio && um_data && um_data->u_data[0] && um_data->u_data[1]) {
      smoothedVol = *(float*)um_data->u_data[0]; // Smoothed value (sampleAgc/volumeSmth)
      rawVol = *(int16_t*)um_data->u_data[1];    // Raw value (rawSampleAgc/volumeRaw)
      // Basic sanity check for raw value if it represents ADC
      if (rawVol < 0) rawVol = 0;
  } else {
      // Fallback pattern for no audio
      uint8_t x = millis() / 20;
      SEGMENT.fill(SEGMENT.color_from_palette(x, false, true, 0));
      return FRAMETIME;
  }

  // Calculate fade rate based on speed slider
  uint8_t fadeRate = map(SEGMENT.speed, 0, 255, 224, 255);
  // Fade out pixels using segment-specific function
  SEGMENT.fade_out(fadeRate);

  // --- Calculate Bar Length ---
  // Use raw volume for length calculation, scaled by intensity slider
  float tmpSoundLength = (float)rawVol; // Convert raw int16_t to float
  float tmpSoundScaled = tmpSoundLength * 2.0f * (float)SEGMENT.intensity / 255.0f;
  // Map the scaled value to the segment length
  // NOTE: The original used mapf(tmpSound2, 0, 255, ...). Check if rawVol range matches 0-255 expectation.
  // Assuming rawVol needs scaling if its range is different (e.g. ADC 0-4095).
  // For now, using 0-255 as assumed by original mapping. Adjust if needed.
  int maxLen = map_float(tmpSoundScaled, 0, 255, 0, SEGLEN);
  maxLen = constrain(maxLen, 0, SEGLEN); // Ensure it doesn't exceed segment length

  // --- Draw Noise Bar ---
  // Use smoothed volume for the noise pattern calculation
  float tmpSoundNoise = smoothedVol;

  // Initialize noise coordinates on first call for this segment
  if (SEGENV.call == 0) {
      SEGENV.aux0 = random16(); // Initialize noise x coordinate
      SEGENV.aux1 = random16(); // Initialize noise y coordinate
  }

  // Draw the active pixels of the soundbar
  for (int i = 0; i < maxLen; i++) {
    // Get noise value based on smoothed volume and position
    uint8_t index = inoise8(i * tmpSoundNoise + SEGENV.aux0, SEGENV.aux1 + i * tmpSoundNoise);
    // Set pixel color using segment palette function
    SEGMENT.setPixelColor(i, SEGMENT.color_from_palette(index, false, PALETTE_SOLID_WRAP, 0));
  }

  // Update noise coordinates for the next frame
  SEGENV.aux0 += beatsin8(5, 0, 10);
  SEGENV.aux1 += beatsin8(4, 0, 10);

  return FRAMETIME; // Return default frame delay
}