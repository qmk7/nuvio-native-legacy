// MUSIC CENTER: the play queue and what this profile keeps on the TV:
// recently played, local favourites (sources without server favourites),
// local playlists (any mix of sources) and the resume point. Pure logic and
// files; no network, no audio. UI-thread only.
//
// Files (per profile): music-queue-p<N>.txt (queue, position, shuffle,
// repeat, seconds into the current track), music-history-p<N>.txt (200),
// music-favorites-p<N>.txt, music-playlists-p<N>.txt.
#ifndef NV_MCFILA_H
#define NV_MCFILA_H
#include "mcmodelo.h"

#define MCQ_MAX 2000
#define MCH_MAX 200
enum { MCR_NAO = 0, MCR_TODAS, MCR_UMA };

// ---- queue ----
void mcq_limpar(void);
// Replaces the queue with `f[0..n)` and points at `inicio`.
void mcq_definir(const McFaixa *f, int n, int inicio);
int  mcq_adicionar(const McFaixa *f);        // at the end; 1 ok
int  mcq_tocar_a_seguir(const McFaixa *f);   // right after the current one
int  mcq_remover(int i);                     // index in PLAY order; the current track cannot be removed
int  mcq_mover(int de, int para);
int  mcq_n(void);
int  mcq_atual(void);                        // index in play order, -1 none
const McFaixa *mcq_item(int i);              // in play order
const McFaixa *mcq_faixa_atual(void);
int  mcq_ir(int i);
// Next / previous by the rules: repeat one stays, repeat all wraps, shuffle
// follows its own order. `automatico`: the track ended by itself (repeat
// one replays); a user "next" always moves. Returns the new index or -1.
int  mcq_proxima(int automatico);
int  mcq_anterior(void);
void mcq_aleatorio(int liga);                // keeps the current track first
int  mcq_eh_aleatorio(void);
void mcq_repetir(int modo);
int  mcq_repeticao(void);
// Resume: seconds into the current track (saved with the queue).
void mcq_posicao(double seg);
double mcq_posicao_salva(void);
int  mcq_salvar(void);
int  mcq_carregar(void);                     // this profile's queue, 1 when there was one

// ---- history ----
void mch_tocou(const McFaixa *f);            // most recent first, no duplicates
int  mch_n(void);
const McFaixa *mch_item(int i);

// ---- local favourites ----
int  mcv_fav(const char *id);
int  mcv_alternar(const McFaixa *f);         // returns the new state
int  mcv_n(void);
const McFaixa *mcv_item(int i);

// ---- local playlists ----
#define MCL_MAX 50
int  mcl_n(void);
const char *mcl_nome(int i);
int  mcl_faixas(int i, const McFaixa **out);  // count
int  mcl_criar(const char *nome);             // index, -1 bad name/full
int  mcl_renomear(int i, const char *nome);
int  mcl_apagar(int i);
int  mcl_adicionar(int i, const McFaixa *f);
int  mcl_remover(int i, int faixa);
int  mcl_mover(int i, int de, int para);

// Reload everything for the active profile (profile switch).
void mcfila_perfil_trocou(void);
// Line format shared with the tests.
int  mcfila_linha(const McFaixa *f, char *d, size_t n);
int  mcfila_ler_linha(const char *s, McFaixa *f);
#endif
