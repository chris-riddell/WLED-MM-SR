#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"

#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)
#define PLINTH_DEBUG 0  // Setting debug flag to 0

uint16_t mode_plinth(void) {
  // Get current time for animation
  uint32_t now = millis();
  
  // Speed controls color movement speed
  uint8_t speed = SEGMENT.speed;
  
  // Calculate color offset based on time and speed
  uint8_t colorOffset = (now / (10 + (10 * (255 - speed) / 255))) % 256;
  
  // Number of complete color cycles across the strip
  // This is configurable via the intensity slider (1-5 cycles)
  uint8_t colorCycles = map(SEGMENT.intensity, 0, 255, 1, 5);
  
  // Process all LEDs
  for (int i = 0; i < SEGLEN; i++) {
    // Calculate normalized position (0.0-1.0) along the strip
    float normalizedPos = (float)i / SEGLEN;
    
    // Calculate color index with smooth distribution across the strip
    // This ensures continuous color flow regardless of strip length
    uint8_t colorIndex = colorOffset + (normalizedPos * 256 * colorCycles);
    
    // Get color from palette with blending support
    uint32_t color = SEGMENT.color_from_palette(colorIndex, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply to LED
    SEGMENT.setPixelColor(i, color);
  }
  
  return FRAMETIME;
} 