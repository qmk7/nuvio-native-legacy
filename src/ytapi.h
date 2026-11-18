// YOUTUBE (QR TIME): the official YouTube Data API v3 and Google's OAuth 2.0
// for TV and limited-input devices. No watch-page scraping, no media URLs:
// this module only lists and describes; playback is handed to the official
// YouTube app on the TV (ondever_youtube_video).
//
// CREDENTIALS. A Data API key (search, trending, video pages) and, for the
// account (subscriptions, playlists), an OAuth client of type "TVs and
// Limited Input devices" (client id + secret) from the user's own Google
// Cloud project. Kept on this TV ("youtube.txt"); the account's tokens per
// profile ("youtube-account-p<N>.txt"). None of them is ever logged.
//
// QUOTA. The default project quota is 10,000 units/day; a search costs 100,
// a list call 1. Results are cached for the session and errors ("quota
// exceeded", "key not valid") are reported as such.
//
// Parsers take JSON bodies (tests feed fixtures); the yt_* calls BLOCK.
#ifndef NV_YTAPI_H
#define NV_YTAPI_H
#include <stddef.h>

enum { YT_VIDEO = 0, YT_CANAL, YT_PLAYLIST };
typedef struct {
  int  tipo;                 // YT_*
  char id[40];               // video id / channel id / playlist id
  char titulo[200];
  char canal[96], canalId[40];
  char thumb[256];           // https://i.ytimg.com/...
  char duracao[16];          // "4:13", "1:02:03"; "" unknown; "AO VIVO" live
  char publicado[16];        // "2026-11-17"
  long long views;           // -1 unknown
  int  itens;                // playlist size
  char desc[700];
} YtItem;

enum { YT_OK = 0, YT_ERRO_REDE, YT_ERRO_CHAVE, YT_ERRO_COTA, YT_ERRO_LOGIN, YT_ERRO_OUTRO, YT_SEM_CHAVE, YT_INDISPONIVEL };

// ---- parsers (no network) ----
// search / videos / playlistItems / subscriptions / playlists lists. Writes
// the next page token. Returns items written, -1 when not a list.
int yt_parse_lista(const char *json, YtItem *out, int max, char *prox, size_t nProx);
// API error body -> YT_ERRO_*.
int yt_parse_erro(const char *json, int http);
// "PT1H2M3S" -> "1:02:03"; "P0D" (live) -> "".
void yt_duracao(const char *iso, char *d, size_t n);
// An 11-character video id ([A-Za-z0-9_-]).
int yt_id_valido(const char *id);

// ---- configuration ----
void yt_definir_base(const char *api, const char *oauth);   // tests
void yt_carregar(void);                  // this TV's credentials, this profile's account
int  yt_tem_chave(void);
int  yt_tem_cliente(void);
int  yt_definir_chave(const char *chave);
int  yt_definir_cliente(const char *id, const char *segredo);   // NULL keeps that half

// ---- calls (BLOCK; worker thread). Return YT_OK or YT_ERRO_*; *n items ----
int yt_em_alta(const char *regiao, const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np);
int yt_buscar(const char *q, const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np);
// Search in YouTube's "Music" category (videoCategoryId 10): music videos,
// official audio and YouTube Music uploads.
int yt_buscar_musica(const char *q, const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np);
int yt_detalhes(YtItem *itens, int n);  // fills duration, views, description (videos)
// One video's page. YT_INDISPONIVEL: removed, private or blocked here.
int yt_video(const char *id, YtItem *out);
int yt_inscricoes(const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np);
int yt_playlists(YtItem *out, int max, int *n);
int yt_playlist_itens(const char *id, const char *pagina, YtItem *out, int max, int *n, char *prox, size_t np);
int yt_canal_videos(const char *canalId, YtItem *out, int max, int *n);

// ---- sign-in (device flow) ----
// Starts: user code and the URL to open on a phone. YT_OK or YT_ERRO_*.
int yt_login_iniciar(char *codigo, size_t nc, char *url, size_t nu, int *intervalo);
// Polls once: 1 signed in, 0 still waiting, -1 refused/expired/error.
int yt_login_conferir(void);
int yt_logado(void);
void yt_sair(void);
#endif
