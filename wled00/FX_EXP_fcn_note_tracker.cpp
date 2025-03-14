#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include "const.h"  // for USERMOD_ID_AUDIOREACTIVE
#include "audio_utils.h"
#include <cmath>

#define NOTE_PERSISTENCE 10      // How many frames a note persists
#define NOTE_FADE_RATE 10        // How quickly notes fade
#define MIN_VOLUME_THRESHOLD 30.0f  // Minimum volume to detect notes
#define NOTE_TRACKER_DEBUG 0
#define PALETTE_SOLID_WRAP (strip.paletteBlend == 1 || strip.paletteBlend == 3)

// Musical notes
const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Forward declaration of helper functions
extern float map_float(float x, float in_min, float in_max, float out_min, float out_max);
extern bool isAudioDataValid(um_data_t *um_data);
extern float detectMusicalNote(um_data_t *um_data);

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
  
  // Get volume data
  float volume = *(float*)um_data->u_data[0];
  
  // Structure to hold note activity
  struct NoteActivity {
    uint8_t intensity;    // Current brightness of note
    uint8_t persistTimer; // How many frames left before note starts fading
  };
  
  // Allocate memory for note activity (12 notes)
  if (!SEGENV.allocateData(sizeof(NoteActivity) * 12)) {
    return FRAMETIME; // Failed to allocate memory
  }
  
  NoteActivity* noteActivity = reinterpret_cast<NoteActivity*>(SEGENV.data);
  
  // Initialize on first call
  if (SEGENV.call == 0) {
    SEGMENT.fill(BLACK);
    
    // Initialize note activity
    for (int i = 0; i < 12; i++) {
      noteActivity[i].intensity = 0;
      noteActivity[i].persistTimer = 0;
    }
  }
  
  // Speed controls flow speed and fade rate
  uint8_t flowSpeed = map(SEGMENT.speed, 0, 255, 1, 5);
  uint8_t fadeRate = map(SEGMENT.speed, 0, 255, 5, 20);
  
  // Sensitivity affects minimum volume threshold
  float volumeThreshold = map_float(SEGMENT.intensity, 0, 255, 80.0f, MIN_VOLUME_THRESHOLD);
  
  // Detect current note (if volume is sufficient)
  int currentNote = -1;
  if (volume > volumeThreshold) {
    currentNote = detectMusicalNote(um_data);
  }
  
  // Update note activity
  for (int i = 0; i < 12; i++) {
    // Activate note if it matches current note
    if (currentNote == i) {
      // Set intensity based on volume
      noteActivity[i].intensity = constrain(volume, 0, 255);
      noteActivity[i].persistTimer = NOTE_PERSISTENCE;
    } 
    // Update existing notes
    else {
      // If timer is still running, maintain intensity
      if (noteActivity[i].persistTimer > 0) {
        noteActivity[i].persistTimer--;
      } 
      // Otherwise fade out
      else if (noteActivity[i].intensity > 0) {
        noteActivity[i].intensity = max(0, noteActivity[i].intensity - fadeRate);
      }
    }
  }
  
  // Now visualize the notes
  for (int i = 0; i < SEGLEN; i++) {
    // Calculate which note corresponds to this LED
    int noteIndex = (i * 12) / SEGLEN;
    
    // Get note intensity and color
    uint8_t intensity = noteActivity[noteIndex].intensity;
    
    // Map note index to a specific color in palette (spread evenly)
    uint8_t colorIndex = (noteIndex * 256) / 12;
    uint32_t color = SEGMENT.color_from_palette(colorIndex, false, PALETTE_SOLID_WRAP, 0);
    
    // Apply intensity to color
    uint8_t r = ((color >> 16) & 0xFF) * intensity / 255;
    uint8_t g = ((color >> 8) & 0xFF) * intensity / 255;
    uint8_t b = (color & 0xFF) * intensity / 255;
    
    // Add flowing effect
    uint8_t pos = (i + (millis() / (50 - flowSpeed * 2))) % SEGLEN;
    
    SEGMENT.setPixelColor(pos, r, g, b);
  }
  
  // Debug output
  if (NOTE_TRACKER_DEBUG && SEGENV.call % 32 == 0) {
    Serial.printf("NOTE_TRACKER: Vol=%.1f Note=%d (%s) Thresh=%.1f\n",
      volume, currentNote, (currentNote >= 0) ? NOTE_NAMES[currentNote] : "None", volumeThreshold);
  }
  
  return FRAMETIME;
} 