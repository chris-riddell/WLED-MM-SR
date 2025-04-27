/*
 * Plinth (FX_fcn_plinth.cpp)
 * 
 * A special non-audio effect optimized for dual strips of different lengths on one pin,
 * allowing smooth color cycling that repeats properly on shorter segments while extending
 * to longer ones. Designed for specific hardware configurations where strips of different
 * lengths need to display coordinated patterns.
 * 
 * For installations that have a "plinth" or base component with a specific LED layout
 * that differs from the main mandala configuration.
 */

#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"

#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)
#define PLINTH_DEBUG 0  // Setting debug flag to 0

// New Plinth mode optimized for dual strips of different lengths on one pin
uint16_t mode_plinth(void) {
  // Get current time for animation
  uint32_t now = millis();
  
  // Speed controls color movement speed
  uint8_t speed = SEGMENT.speed;
  
  // Calculate color offset based on time and speed
  uint8_t colorOffset = (now / (10 + (10 * (255 - speed) / 255))) % 256;
  
  // Number of complete color cycles across the shorter strip length
  // This is configurable via the intensity slider (1-5 cycles)
  uint8_t colorCycles = map(SEGMENT.intensity, 0, 255, 1, 5);
  
  // *** Define the length of the shorter physical strip ***
  // This should be the length you want the pattern to loop smoothly over.
  const uint16_t shortLength = 12;
  
  // Basic check to prevent division by zero if shortLength is somehow invalid
  if (shortLength == 0) {
      // Turn off LEDs in case of error
      for (int i = 0; i < SEGLEN; i++) {
          SEGMENT.setPixelColor(i, 0);
      }
      return FRAMETIME * 10; // Return a longer delay to indicate an issue
  }
  
  // Process all LEDs configured in the segment (up to SEGLEN = 17)
  for (int i = 0; i < SEGLEN; i++) {
    // Calculate the effective position within the shorter strip length, looping back around
    uint16_t effectivePos = i % shortLength;
    
    // Calculate normalized position (0.0-1.0) along the *shorter* strip length
    // Add a small epsilon to prevent potential floating point issues at the exact boundary
    float normalizedPos = (float)effectivePos / (float)shortLength;
    
    // Calculate color index with smooth distribution across the shorter strip length
    uint8_t colorIndex = (colorOffset + uint8_t(normalizedPos * 256 * colorCycles)) & 0xFF;
    
    // Get color from palette with blending support
    uint32_t color = SEGMENT.color_from_palette(colorIndex, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply to LED
    SEGMENT.setPixelColor(i, color);
  }
  
  return FRAMETIME;
} 