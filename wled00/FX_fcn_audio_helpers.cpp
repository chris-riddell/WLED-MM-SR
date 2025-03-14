#include "wled.h"
#include "FX.h"
#include "fcn_declare.h"
#include "palettes.h"
#include <cmath>

// Common helper functions for audio reactive effects

// Helper function for float mapping
float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Check if audio data is valid
bool isAudioDataValid(um_data_t *um_data) {
  if (!um_data || !um_data->u_data) return false;
  return true;
}

// Check if FFT data is available
bool hasFFTData(um_data_t *um_data) {
  if (!isAudioDataValid(um_data)) return false;
  if (!um_data->u_data[2]) return false;  // FFT data array
  return true;
}

// Calculate spectral centroid from FFT data
// Returns a value from 0-1 representing "brightness" of sound
// 0 = mostly bass, 1 = mostly treble
float calculateSpectralCentroid(uint8_t* fftData, int numBins) {
  float weightedSum = 0.0f;
  float sum = 0.0f;
  
  for (int i = 0; i < numBins; i++) {
    float binValue = fftData[i];
    if (binValue < 5) continue; // Ignore very low values to reduce noise
    
    // Weight by bin index (higher index = higher frequency)
    weightedSum += binValue * i;
    sum += binValue;
  }
  
  if (sum <= 0) return 0.0f;
  
  // Normalize to 0-1 range
  float centroid = weightedSum / (sum * numBins);
  return constrain(centroid, 0.0f, 1.0f);
}

// Detect bass drops - returns intensity from 0-1
// Uses a combination of bass energy, spectral flux, and peak detection
float detectBassDropIntensity(um_data_t *um_data) {
  static float prevBassEnergy = 0;
  static float prevVolume = 0;
  static float maxBassEnergy = 20.0f;
  static float buildupIntensity = 0;
  static uint32_t lastDropTime = 0;
  static const uint16_t DROP_COOLDOWN = 3000; // Min time between drops in ms
  
  if (!hasFFTData(um_data)) return 0;
  
  uint8_t* fftData = (uint8_t*)um_data->u_data[2];
  float volume = um_data->u_data[0] ? *(float*)um_data->u_data[0] : 0;
  
  // Calculate bass energy (first 3 bins with emphasis on first bin)
  float bassEnergy = fftData[0] * 1.5f;
  if (um_data->u_data[2] && fftData) {
    bassEnergy += (fftData[1] * 0.7f) + (fftData[2] * 0.3f);
  }
  
  // Update max bass energy for normalization
  maxBassEnergy = max(bassEnergy, maxBassEnergy * 0.999f);
  
  // Calculate bass increase and volume change
  float bassChange = bassEnergy - prevBassEnergy;
  float volumeChange = volume - prevVolume;
  
  // Normalize values
  float normalizedBass = bassEnergy / max(1.0f, maxBassEnergy);
  float normalizedBassChange = bassChange / max(1.0f, maxBassEnergy);
  
  // Check for drop conditions
  bool isBassStrong = normalizedBass > 0.5f;
  bool isBassIncreasing = normalizedBassChange > 0.1f;
  bool isVolumeIncreasing = volumeChange > 2.0f;
  bool isCooldownOver = (millis() - lastDropTime) > DROP_COOLDOWN;
  
  // Bass drop detection
  float dropIntensity = 0;
  
  // Buildup detection (steadily increasing bass and volume)
  if (bassChange > 0 && volumeChange > 0) {
    buildupIntensity = min(1.0f, buildupIntensity + 0.01f);
  } else {
    buildupIntensity = max(0.0f, buildupIntensity - 0.02f);
  }
  
  // Drop detection
  if (isBassStrong && isBassIncreasing && isVolumeIncreasing && isCooldownOver) {
    dropIntensity = 0.5f + (normalizedBass * 0.5f); // 0.5-1.0 range for drop intensity
    lastDropTime = millis();
    buildupIntensity = 0; // Reset buildup after drop
  } else {
    // Slowly fade out drop intensity
    dropIntensity = 0;
  }
  
  // Update previous values
  prevBassEnergy = bassEnergy;
  prevVolume = volume;
  
  return max(dropIntensity, buildupIntensity * 0.4f); // Combine drop and buildup with priority on drop
}

// Simple musical note detection
// Returns a note index (0-11) for C, C#, D, D#, E, F, F#, G, G#, A, A#, B
// Returns -1.0f if no clear note is detected
float detectMusicalNote(um_data_t *um_data) {
  if (!hasFFTData(um_data)) return -1.0f;
  
  uint8_t* fftData = (uint8_t*)um_data->u_data[2];
  float volume = um_data->u_data[0] ? *(float*)um_data->u_data[0] : 0;
  
  // Return -1 if volume is too low
  if (volume < 30.0f) return -1.0f;
  
  // Find the strongest frequency bin
  int maxBin = 0;
  uint8_t maxVal = 0;
  
  for (int i = 0; i < 16; i++) {
    if (fftData[i] > maxVal) {
      maxVal = fftData[i];
      maxBin = i;
    }
  }
  
  // Simple mapping of bins to note indices
  // This is very rough and not accurate for real music note detection
  // Just for visual effect purposes
  float note = float(maxBin % 12);
  
  // Only return a note if the bin has significant energy
  return (maxVal > 30) ? note : -1.0f;
}

// Detect major/minor tonality (very simplified)
// Returns: -1.0f for minor, 0.0f for neutral/unknown, 1.0f for major
float detectTonality(um_data_t *um_data) {
  if (!hasFFTData(um_data)) return 0.0f;
  
  uint8_t* fftData = (uint8_t*)um_data->u_data[2];
  
  // This is an extreme simplification
  // Real tonality detection would analyze harmonic content over time
  
  // Get energy in mid-high frequencies (bins 8-15)
  float highFreqEnergy = 0;
  for (int i = 8; i < 16; i++) {
    highFreqEnergy += fftData[i];
  }
  
  // Get energy in low-mid frequencies (bins 0-7)
  float lowFreqEnergy = 0;
  for (int i = 0; i < 8; i++) {
    lowFreqEnergy += fftData[i];
  }
  
  // High energy in higher frequencies often correlates with major keys
  // This is a very rough approximation
  if (highFreqEnergy > lowFreqEnergy * 1.5f) {
    return 1.0f; // Major
  } else if (lowFreqEnergy > highFreqEnergy * 1.2f) {
    return -1.0f; // Minor
  }
  
  return 0.0f; // Neutral/unknown
} 