/*
 * Multi Comet: 13.4 version (FX_fcn_multi_comet_134.cpp)
 * 
 * Generates multiple comet-like animations that move across the LED strip, triggered 
 * by audio peaks with different colors.
 * 
 * Best for: Rhythmic music with distinct beats like pop, rock, hip-hop, and dance music.
 * 
 * This is the WLED v0.13.4 version of the effect, maintained for compatibility.
 */

#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE

/*
 * Creates random comets
 * Custom mode by Keith Lord: https://github.com/kitesurfer1404/WS2812FX/blob/master/src/custom/MultiComet.h
 */
#define MAX_COMETS 12 // was 8
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)
#define MULTI_COMET_DEBUG 0  // Setting debug flag to 0

uint16_t mode_static(void);  // Forward declaration

uint16_t mode_multi_comet_134(void) 
{
  uint32_t cycleTime = 10 + (uint32_t)(255 - SEGMENT.speed);
  uint32_t it = strip.now / cycleTime;
  if (!SEGENV.allocateData(sizeof(uint16_t) * MAX_COMETS)) return mode_static(); //allocation failed
  uint16_t* comets = reinterpret_cast<uint16_t*>(SEGENV.data);
  
  if (SEGENV.call == 0) { // do some initializations
    for(uint8_t i=0; i < MAX_COMETS; i++) comets[i] = SEGLEN;  // WLEDSR make sure comets are started individually
    SEGENV.aux0 = 0;
  }
  
  if (SEGENV.step == it) return FRAMETIME;
  uint16_t armed = SEGENV.aux0;   // WLEDSR allows to delay comet launch
  bool shotOne = false;           // WLEDSR avoids starting several comets at the same time (invisible due to overlap)
  SEGMENT.fade_out(SEGMENT.intensity);

  // Get audio data
  um_data_t *um_data;
  bool useAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  float sampleAgc = 0;
  float rawSampleAgc = 0;
  uint8_t samplePeak = 0;
  
  if (useAudio) {
    sampleAgc = *(float*)um_data->u_data[0];      // was volumeSmth
    rawSampleAgc = *(int16_t*)um_data->u_data[1]; // was volumeRaw
    samplePeak = *(uint8_t*)um_data->u_data[3];
  }

  for(uint8_t i=0; i < MAX_COMETS; i++) {
    if(comets[i] < SEGLEN) {
      uint16_t index = comets[i];
      uint32_t baseColor;
      if (SEGCOLOR(2) != 0)
      {
        baseColor = (i % 2 ? SEGMENT.color_from_palette(index, true, PALETTE_SOLID_WRAP, 0) : SEGCOLOR(2));
        //SEGMENT.setPixelColor(index, i % 2 ? SEGMENT.color_from_palette(index, true, PALETTE_SOLID_WRAP, 0) : SEGCOLOR(2));
      } else
      {
        baseColor = SEGMENT.color_from_palette(index, true, PALETTE_SOLID_WRAP, 0);
      }
      // Reduce potential whiteness by slightly lowering max brightness
      // Extract color components
      uint8_t r = (uint8_t)((baseColor >> 16) & 0xFF);
      uint8_t g = (uint8_t)((baseColor >> 8) & 0xFF);
      uint8_t b = (uint8_t)(baseColor & 0xFF);
      
      // Cap at 240 to reduce potential whiteness
      if (r > 240) r = 240;
      if (g > 240) g = 240;
      if (b > 240) b = 240;
      
      SEGMENT.setPixelColor(index, r, g, b);
      comets[i]++;
    } else {
      if (!useAudio) {
        if(!random(SEGLEN) && !shotOne) {
          comets[i] = 0;
          shotOne = true;         // WLEDSR avoid starting several comets at once (as they are invisible)
        }
      } else {                    // WLEDSR delay comet "launch" during silence, and wait until next beat
        if (random(SEGLEN) < 5) armed++;                                                   // new comet loaded and ready
        if (armed > 2) armed = 2;                                                          // max two armed at once (avoid overlap)
        if ((armed > 0) && (shotOne == false) 
            && (sampleAgc > 1.0) && ((samplePeak > 1) || (int(rawSampleAgc) > 112))) {   // delayed launch - wait until peak, don't launch in silence 
          comets[i] = 0; // start a new comet!
          armed--;       // un-arm one
          shotOne = true;
        }
      }
    }
  }
  
  SEGENV.aux0 = armed;            // WLEDSR
  SEGENV.step = it;
  return FRAMETIME;
}