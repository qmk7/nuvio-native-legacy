// QR TIME Trakt FAVORITES (/sync/favorites: Trakt's own favourite movies and
// shows, separate from the watchlist and from ratings). Same identity gate as
// every Trakt write (traktid.h): local ids are never sent.
#ifndef NV_TRAKTFAV_H
#define NV_TRAKTFAV_H

// -1 not known yet (a load starts in the background), 0 no, 1 yes.
int  traktfav_estado(const char *id);
// Add/remove on a worker; the known state changes after HTTP 2xx. 1 queued.
int  traktfav_alternar(const char *id, const char *tipo);
void traktfav_esquecer(void);         // account switch
// Parser shared with the tests: one /sync/favorites/<type> page -> canonical ids.
int  traktfav_parse(const char *corpo, char (*ids)[24], int max);
// Tests: wait for the worker (ms). 1 idle.
int  traktfav_teste_esperar(int ms);
#endif
