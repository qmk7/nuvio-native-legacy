// MUSIC CENTER: Last.fm (API 2.0) — scrobbling, now playing, artist info and
// similar artists. Authentication is Last.fm's own flow for applications:
// auth.getToken -> the person approves on last.fm (phone QR) ->
// auth.getSession -> a session key that does not expire. Calls are signed
// (api_sig = md5 of the sorted parameters + the shared secret).
//
// The API key and secret are the user's own Last.fm API account, kept on
// this TV ("lastfm.txt"); the session per profile ("lastfm-sessao-p<N>.txt").
// None of them is logged. A scrobble that cannot be sent waits in
// "lastfm-pendentes-p<N>.txt" (up to 500) and goes with the next one.
// Calls BLOCK; mccat.c calls the scrobble hooks on worker threads.
#ifndef NV_MCLFM_H
#define NV_MCLFM_H
#include "mcmodelo.h"

void mclfm_definir_base(const char *url);       // tests
void mclfm_carregar(void);
int  mclfm_tem_chave(void);
int  mclfm_definir_chave(const char *chave, const char *segredo);   // NULL keeps that half
int  mclfm_conectado(void);
void mclfm_usuario(char *d, size_t n);
// Sign-in: the URL to open on the phone. MC_OK / MC_ERRO_*.
int  mclfm_entrar_iniciar(char *url, size_t n);
// 1 signed in, 0 not yet approved, -1 token expired/refused.
int  mclfm_entrar_conferir(void);
void mclfm_sair(void);

// Hooks (mccat.c).
void mclfm_agora(const McFaixa *f);
void mclfm_tocou(const McFaixa *f, long long inicioMs);
int  mclfm_pendentes(void);

// Metadata (no session needed, key only).
int  mclfm_artista(const char *nome, char *bio, size_t nb, char *imagem, size_t ni);
int  mclfm_similares(const char *artista, char nomes[][128], int max);

// Signature of a parameter list "k=v" (NULL-terminated), exposed for the tests.
void mclfm_assinar(const char *const *pares, const char *segredo, char out[33]);
#endif
