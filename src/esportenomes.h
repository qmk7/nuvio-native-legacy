// QR TIME: Arabic team and league names for ESPN's English ones. See esportenomes.c.
#ifndef QRT_ESPORTENOMES_H
#define QRT_ESPORTENOMES_H
const char *esporte_nome_ar(const char *en);     // NULL = not in the dictionary
const char *esporte_liga_ar(const char *en);
// What the screen shows: the Arabic name when the app is in Arabic and the
// dictionary has it, else ESPN's name.
const char *esporte_nome_tela(const char *en);
const char *esporte_liga_tela(const char *en);
#endif
