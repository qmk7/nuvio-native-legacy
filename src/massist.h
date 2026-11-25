// QR TIME + MUSIC ASSISTANT (the music server in the owner's Home Assistant).
// Music Assistant signs in to YouTube Music and plays on the home's speakers
// (soundbar, Google speakers, Echos, groups); QR TIME only talks to it on the
// home network: browse the library, search, pick a speaker, play, control.
//
// API: POST <url>/api {"command": "...", "args": {...}} with
// "Authorization: Bearer <token>" (a long-lived token made in Music
// Assistant's settings). The address and token stay on this TV in
// music-assistant.txt (0600 like every data file) and never reach a log.
//
// Every function but the config ones BLOCKS (network): call from a worker.
#ifndef QRT_MASSIST_H
#define QRT_MASSIST_H
#include <stddef.h>

#define MA_URI 384

typedef struct {
  char uri[MA_URI];       // "ytmusic://track/..." / "library://playlist/12": what play_media takes
  char item_id[160], provider[64];
  char tipo[16];          // track, album, artist, playlist, radio
  char nome[200], sub[160];   // title; artist (or owner) under it
  char imagem[512];       // an http(s) picture, "" none
  int  dur;               // seconds, 0 unknown
} MaItem;

typedef struct {
  char id[96], nome[96], estado[16];   // estado: playing, paused, idle, off
  int  disponivel, grupo;
} MaPlayer;

typedef struct {
  char titulo[200], artista[160], imagem[512], estado[16];   // estado: playing, paused, idle
  int  dur;          // seconds, 0 unknown
  double pos;        // seconds into the track when asked
} MaAgora;

enum { MA_OK = 0, MA_SEM_CONFIG, MA_REDE, MA_TOKEN, MA_SERVIDOR };

// Config (any thread). url like "http://192.168.100.104:8095".
int  massist_configurado(void);
int  massist_ler(char *url, size_t nu, char *token, size_t nt);
int  massist_gravar(const char *url, const char *token);

// The speaker QR TIME plays on (its player_id and name), kept on this TV.
int  massist_alto_ler(char *id, size_t ni, char *nome, size_t nn);
int  massist_alto_gravar(const char *id, const char *nome);
// MA_OK + "Music Assistant 2.x" in desc when the address and token work.
int  massist_testar(char *desc, size_t n);
// tipo: "playlists", "albums", "artists", "tracks", "radios"
int  massist_biblioteca(const char *tipo, int offset, MaItem *out, int max, int *n);
int  massist_recentes(MaItem *out, int max, int *n);
int  massist_buscar(const char *q, MaItem *out, int max, int *n);
// A playlist's or an album's tracks.
int  massist_faixas_de(const MaItem *pai, MaItem *out, int max, int *n);
int  massist_players(MaPlayer *out, int max, int *n);
// Play `uri` on `player` (replacing its queue); radio = keep going with similar songs.
int  massist_tocar(const char *player, const char *uri, int radio);
// What `player` is playing now (players/all: current_media, elapsed_time).
int  massist_agora(const char *player, MaAgora *a);
int  massist_ler_agora(const char *json, const char *player, MaAgora *a);   // tests
// play, pause, play_pause, stop, next, previous, volume_up, volume_down
int  massist_comando(const char *player, const char *cmd);

// ---- pure (tests) ----
int  massist_ler_itens(const char *json, const char *chave, MaItem *out, int max);
int  massist_ler_players(const char *json, MaPlayer *out, int max);
void massist_teste_transporte(char *(*f)(const char *url, const char *corpo, const char *token, int *status));
#endif
