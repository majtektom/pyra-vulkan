// Implementacja dekodera Ogg Vorbis (stb_vorbis, public domain) w osobnej jednostce kompilacji,
// bo stb_vorbis.c definiuje krótkie makra, które nie powinny wyciekać do reszty silnika.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "feature/audio/stb_vorbis.c"
