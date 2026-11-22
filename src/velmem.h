// QR TIME: playback speed remembered PER SERIES and per profile
// (velocidade-p<N>.txt, "<imdb>\t<centesimos>"). A film still starts at 1x
// (#202); a series where the person chose 1.25x keeps it on its next episodes.
// Choosing 1x again forgets it.
#ifndef QRT_VELMEM_H
#define QRT_VELMEM_H
int  velmem_ler(const char *serie);                // centesimos, 0 = none
void velmem_gravar(const char *serie, int centesimos);
#endif
