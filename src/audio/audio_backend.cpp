// Keep codec implementation macros local to this translation unit.
// Declaring stb_vorbis before miniaudio enables Vorbis for every ma_decoder,
// including streamed music, waveform analysis, seeking and music previews.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
