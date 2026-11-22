// QR TIME PHONE FEATURES: the pages a phone opens from a QR on the TV, served
// by lanweb.c. Each one is opened from Settings > Accounts, providers and data
// > "Phone" (and some from their own screens), and lives while the app runs.
//   CA_CONTROLE  remote control: arrows, OK, back, play/pause; type on the
//                phone into the open field, or search the TV
//   CA_MUSICA    music setup: Navidrome/Subsonic, WebDAV, Last.fm key,
//                Spotify playlist import
//   CA_FESTA     party mode: guests search and request songs, the host
//                approves on the TV, votes to skip (mcfesta.c)
//   CA_BACKUP    encrypted backup of what the account does not sync, and
//                restore (qrbackup.c)
//   CA_FOTO      a real photo for the current profile (perfilfoto.c)
#ifndef QRT_CELAPPS_H
#define QRT_CELAPPS_H
#include <SDL2/SDL.h>
#include <stddef.h>
#include "lanweb.h"

enum { CA_CONTROLE, CA_MUSICA, CA_FESTA, CA_BACKUP, CA_FOTO, CA_CASA, CA_N };

int  celapps_abrir(int app);            // page up; 1 ok (0 = no home network)
const char *celapps_url(int app);       // "" while closed
const char *celapps_nome(int app);      // i18n key
const char *celapps_desc(int app);      // i18n key
const char *celapps_icone(int app);
void celapps_renovar(int app);          // new link; the old one stops
void celapps_atualizar(Uint32 agora);   // app thread, once per frame

// Page skeleton shared by every phone page: QR TIME colours, RTL for Arabic,
// and a small `post(route, fields)` / `estado(fn)` helper in JS. malloc'd.
char *celapps_pagina(const char *titulo, const char *corpo, const char *js);

// Key names from the phone remote ("up", "ok", "back", "play"...), queued
// here and taken by main.c each frame (the injected-keys path). 1 = one taken.
int  celapps_tecla_remota(char *dst, size_t n);

// Handlers the other phone features register (festa, backup, foto).
typedef struct {
  char *(*montar)(void);                  // the page (malloc'd)
  void  (*pedido)(const LwPedido *p);
  void  (*quadro)(Uint32 agora);
  size_t maxCorpo;
} CelAppDef;
void celapps_definir(int app, const CelAppDef *d);
#endif
