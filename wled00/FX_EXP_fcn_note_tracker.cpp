/*
 * Note Tracker (FX_EXP_fcn_note_tracker.cpp)
 * 
 * Detects musical notes in audio and displays them as colored segments that 
 * persist briefly, creating patterns based on the melody and musical flow. 
 * This effect tries to detect and visualize notes like "C", "C#", "D", "D#", etc.
 * 
 * Best for: Solo instrumental pieces, vocal performances, and music with clear, 
 * distinct melodies like piano solos, flute music, or songs with prominent vocal lines.
 */

#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>
#include <algorithm>  // for std::min

#define MAX_NOTES 12  // C, C#, D, D#, E, F, F#, G, G#, A, A#, B
#define NOTE_HISTORY_SIZE 4  // Reduced from 6 for faster response
#define MIN_VOLUME_THRESHOLD 20.0f  // Further reduced threshold for better detection
#define NOTE_TRACKER_DEBUG 0  // Setting debug flag to 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Animation constants
#define MIN_FADE_RATE 0.85f   // Slower fade for longer trails
#define MAX_FADE_RATE 0.60f   // Faster fade at high speeds
#define MIN_MOVE_SPEED 1      // Minimum movement speed
#define MAX_MOVE_SPEED 5      // Increased maximum movement speed
#define NOTE_DISPLAY_MODE_FIXED 0    // Fixed note positions
#define NOTE_DISPLAY_MODE_CIRCULAR 1  // Circular/moving notes

// Musical note frequencies (for C4 through B4)
const float NOTE_FREQUENCIES[] = {
  261.63f,  // C4
  277.18f,  // C#4
  293.66f,  // D4
  311.13f,  // D#4
  329.63f,  // E4
  349.23f,  // F4
  369.99f,  // F#4
  392.00f,  // G4
  415.30f,  // G#4
  440.00f,  // A4
  466.16f,  // A#4
  493.88f   // B4
};

const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);

// Data structure to track note activity
struct NoteActivity {
  float intensity;      // Current brightness (0.0-1.0)
  float targetIntensity; // Target brightness for smooth transitions
  float volume;        // Current volume for this note
  uint8_t age;        // How many frames this note has been active
  uint8_t stability;   // How stable the note detection is
};

// Helper function to find the closest musical note to a frequency
int findClosestNote(float frequency) {
  if (frequency <= 0) return -1;
  
  // Find which octave we're in
  float octave = log2f(frequency / NOTE_FREQUENCIES[0]);
  float baseFreq = frequency / powf(2.0f, floorf(octave));
  
  // Find closest note in the scale
  float minDiff = 1000000;
  int closestNote = -1;
  
  for (int i = 0; i < MAX_NOTES; i++) {
    float diff = fabsf(baseFreq - NOTE_FREQUENCIES[i]);
    if (diff < minDiff) {
      minDiff = diff;
      closestNote = i;
    }
  }
  
  // More lenient threshold for note detection
  if (minDiff > 30.0f) return -1;  // Increased from 25.0f for even more leniency
  return closestNote;
}

uint16_t mode_note_tracker(void) {
  // Get audio data
  um_data_t *um_data;
  bool hasAudio = usermods.getUMData(&um_data, USERMOD_ID_AUDIOREACTIVE);
  
  if (!hasAudio || !isAudioDataValid(um_data)) {
    // Fallback pattern for no audio
    uint8_t x = millis() / 20;
    SEGMENT.fill(SEGMENT.color_from_palette(x, false, true, 0));
    return FRAMETIME;
  }
  
  // Get volume and frequency data
  float volume = *(float*)um_data->u_data[0];
  float frequency = *(float*)um_data->u_data[8];  // Use FFT_MajorPeak for better accuracy
  
  // Allocate memory for note activity and history
  if (!SEGENV.allocateData(sizeof(NoteActivity) * MAX_NOTES + sizeof(int) * NOTE_HISTORY_SIZE)) {
    return FRAMETIME;
  }
  
  NoteActivity* noteActivity = reinterpret_cast<NoteActivity*>(SEGENV.data);
  int* noteHistory = reinterpret_cast<int*>(SEGENV.data + sizeof(NoteActivity) * MAX_NOTES);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    SEGENV.aux0 = 0;  // Position for circular mode
    SEGENV.aux1 = NOTE_DISPLAY_MODE_FIXED;  // Default to fixed mode
    
    // Initialize note activity
    for (int i = 0; i < MAX_NOTES; i++) {
      noteActivity[i].intensity = 0;
      noteActivity[i].targetIntensity = 0;
      noteActivity[i].volume = 0;
      noteActivity[i].age = 0;
      noteActivity[i].stability = 0;
    }
    
    // Initialize note history
    for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
      noteHistory[i] = -1;
    }
  }
  
  // Map controls with improved ranges
  float fadeRate = map_float(SEGMENT.speed, 0, 255, MIN_FADE_RATE, MAX_FADE_RATE);
  uint8_t moveSpeed = map(SEGMENT.speed, 0, 255, MIN_MOVE_SPEED, MAX_MOVE_SPEED);
  float sensitivity = map_float(SEGMENT.intensity, 0, 255, 1.5f, 5.0f);  // Increased range for higher sensitivity
  float minVolume = map_float(SEGMENT.custom1, 0, 255, 10.0f, 60.0f);  // Lower range for earlier triggering
  
  // Set display mode based on custom2
  SEGENV.aux1 = (SEGMENT.custom2 < 128) ? NOTE_DISPLAY_MODE_FIXED : NOTE_DISPLAY_MODE_CIRCULAR;
  
  // Update position for circular mode (much slower movement)
  if (SEGENV.aux1 == NOTE_DISPLAY_MODE_CIRCULAR && SEGENV.call % 4 == 0) {
    SEGENV.aux0 = (SEGENV.aux0 + moveSpeed) % SEGLEN;
  }
  
  // Detect current note with volume-based sensitivity
  int currentNote = -1;
  if (volume >= minVolume) {
    currentNote = findClosestNote(frequency);
  }
  
  // Update note history
  static uint8_t historyIndex = 0;
  noteHistory[historyIndex] = currentNote;
  historyIndex = (historyIndex + 1) % NOTE_HISTORY_SIZE;
  
  // Count occurrences of each note in history for stability
  int noteCounts[MAX_NOTES] = {0};
  int maxCount = 0;
  int dominantNote = -1;
  
  for (int i = 0; i < NOTE_HISTORY_SIZE; i++) {
    if (noteHistory[i] >= 0) {
      noteCounts[noteHistory[i]]++;
      if (noteCounts[noteHistory[i]] > maxCount) {
        maxCount = noteCounts[noteHistory[i]];
        dominantNote = noteHistory[i];
      }
    }
  }
  
  // Reduced stability requirement based on sensitivity
  int requiredCount = (sensitivity > 3.0f) ? 1 : (sensitivity > 2.0f) ? 2 : 3;  // More sensitive = fewer matches needed
  if (maxCount < requiredCount) dominantNote = -1;
  
  // Update note activity with improved response
  for (int i = 0; i < MAX_NOTES; i++) {
    if (i == dominantNote) {
      // Active note - increase intensity based on volume with more aggressive scaling
      float volumeRatio = (volume - minVolume) / (255.0f - minVolume);
      volumeRatio = constrain(volumeRatio * sensitivity * 2.0f, 0.0f, 1.0f);  // More aggressive scaling
      
      noteActivity[i].targetIntensity = 0.5f + (volumeRatio * 0.5f);  // Higher minimum intensity
      noteActivity[i].volume = volume;
      noteActivity[i].age++;
      noteActivity[i].stability = min(255, noteActivity[i].stability + 2); // Faster stability gain
      
      // Faster attack for more responsiveness
      float attack = (noteActivity[i].age < 3) ? 0.7f : 0.4f;  // Increased attack rates
      noteActivity[i].intensity += (noteActivity[i].targetIntensity - noteActivity[i].intensity) * attack;
    } else {
      // Inactive note - fade out
      noteActivity[i].targetIntensity *= 0.8f; // More gradual target decrease
      noteActivity[i].intensity *= fadeRate;
      
      // Reduce age and stability for inactive notes
      if (noteActivity[i].age > 0) noteActivity[i].age--;
      if (noteActivity[i].stability > 0) noteActivity[i].stability--;
      
      // Clear very dim notes
      if (noteActivity[i].intensity < 0.01f) {
        noteActivity[i].intensity = 0;
        noteActivity[i].volume = 0;
      }
    }
  }
  
  // Clear the segment
  SEGMENT.fill(BLACK);
  
  // Render notes based on display mode
  if (SEGENV.aux1 == NOTE_DISPLAY_MODE_FIXED) {
    // Fixed mode - each note has its own section
    int noteSectionWidth = SEGLEN / MAX_NOTES;
    
    for (int note = 0; note < MAX_NOTES; note++) {
      if (noteActivity[note].intensity > 0) {
        // Calculate section boundaries
        int noteStart = note * noteSectionWidth;
        int noteCenter = noteStart + (noteSectionWidth / 2);
        int noteEnd = noteStart + noteSectionWidth - 1;
        
        // Get color for this note
        uint8_t colorIndex = (note * 21) % 256;  // Good color separation
        
        // Enhance: Add stability-based hue shift for visual interest
        uint8_t hueShift = (noteActivity[note].stability > 10) ? (noteActivity[note].stability / 8) : 0;
        colorIndex = (colorIndex + hueShift) % 256;
        
        uint32_t noteColor = SEGMENT.color_from_palette(colorIndex, false, PALETTE_SOLID_WRAP, 0);
        
        // Draw note with intensity gradient
        for (int pos = noteStart; pos <= noteEnd; pos++) {
          // Calculate distance from center
          float distRatio = fabsf(pos - noteCenter) / (noteSectionWidth / 2.0f);
          distRatio = constrain(distRatio, 0.0f, 1.0f);
          
          // Apply bell curve falloff - enhanced for more visibility
          float falloff = exp(-2.5f * distRatio * distRatio); // Less steep falloff (was -3.0f)
          
          // Calculate final brightness with volume boost
          float volBoost = map_float(noteActivity[note].volume, minVolume, 255, 1.0f, 1.5f);
          float brightness = noteActivity[note].intensity * falloff * volBoost;
          brightness = constrain(brightness, 0.0f, 1.0f);
          
          // Apply color with brightness
          uint8_t r = ((noteColor >> 16) & 0xFF) * brightness;
          uint8_t g = ((noteColor >> 8) & 0xFF) * brightness;
          uint8_t b = (noteColor & 0xFF) * brightness;
          
          // Add to existing color (additive blending)
          uint32_t existing = SEGMENT.getPixelColor(pos);
          r = std::min(255u, static_cast<unsigned int>(r) + ((existing >> 16) & 0xFF));
          g = std::min(255u, static_cast<unsigned int>(g) + ((existing >> 8) & 0xFF));
          b = std::min(255u, static_cast<unsigned int>(b) + (existing & 0xFF));
          
          SEGMENT.setPixelColor(pos, r, g, b);
        }
      }
    }
  } else {
    // Circular mode - notes move around the strip
    for (int note = 0; note < MAX_NOTES; note++) {
      if (noteActivity[note].intensity > 0) {
        // Calculate note position and width - wider for better visibility
        int noteWidth = max(4, SEGLEN / 5);  // Wider sections for better visibility
        
        // Add slight randomization to position based on stability for vibration effect
        int notePosition = (note * SEGLEN / MAX_NOTES + SEGENV.aux0);
        if (noteActivity[note].stability > 20 && noteActivity[note].intensity > 0.6f) {
          notePosition += (millis() % 3) - 1; // Small jitter on strong notes
        }
        notePosition = notePosition % SEGLEN;
        
        // Get color for this note
        uint8_t colorIndex = (note * 21) % 256;
        uint32_t noteColor = SEGMENT.color_from_palette(colorIndex, false, PALETTE_SOLID_WRAP, 0);
        
        // Draw note with smooth falloff
        for (int offset = -noteWidth/2; offset <= noteWidth/2; offset++) {
          int pos = (notePosition + offset + SEGLEN) % SEGLEN;
          
          // Calculate falloff based on distance from center
          float distRatio = fabsf(offset) / (float)(noteWidth/2);
          float falloff = exp(-4.0f * distRatio * distRatio);
          
          // Calculate final brightness
          float brightness = noteActivity[note].intensity * falloff;
          
          // Apply color with brightness
          uint8_t r = ((noteColor >> 16) & 0xFF) * brightness;
          uint8_t g = ((noteColor >> 8) & 0xFF) * brightness;
          uint8_t b = (noteColor & 0xFF) * brightness;
          
          // Add to existing color
          uint32_t existing = SEGMENT.getPixelColor(pos);
          r = std::min(255u, static_cast<unsigned int>(r) + ((existing >> 16) & 0xFF));
          g = std::min(255u, static_cast<unsigned int>(g) + ((existing >> 8) & 0xFF));
          b = std::min(255u, static_cast<unsigned int>(b) + (existing & 0xFF));
          
          SEGMENT.setPixelColor(pos, r, g, b);
        }
      }
    }
  }
  
  if (NOTE_TRACKER_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("NOTE-TRACKER: Vol=%.1f Freq=%.1f Note=%d (%s) Mode=%d Sens=%.1f\n",
      volume, frequency, dominantNote,
      (dominantNote >= 0) ? NOTE_NAMES[dominantNote] : "None",
      SEGENV.aux1, sensitivity);
  }
  
  return FRAMETIME;
} 