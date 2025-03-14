#ifndef WLED_AUDIO_UTILS_H
#define WLED_AUDIO_UTILS_H

#include "wled.h"

// Audio data validation functions
bool isAudioDataValid(um_data_t *um_data);
bool hasFFTData(um_data_t *um_data);

// Audio analysis functions
float detectTonality(um_data_t *um_data);
float detectMusicalNote(um_data_t *um_data);
float calculateSpectralCentroid(uint8_t* fftData, int numBins);

// Utility functions
float map_float(float x, float in_min, float in_max, float out_min, float out_max);

#endif // WLED_AUDIO_UTILS_H 