// QR TIME PHONE PAGES: one small HTTP server on the home network, for the
// pages a phone opens from a QR code on the TV (remote control, music setup,
// party requests, backup, profile photo). celular.c stays the one-shot
// "type on the phone" box; this one lives while the app runs.
//
// SECURITY (same rules as celular.h, adapted to a server that stays up):
//   - each page ("app") has its own random token (12 symbols, ~59 bits) in the
//     path: http://<tv-ip>:<port>/<token>/...; a guest who has the party link
//     cannot reach the remote control or the backup;
//   - Host must be the address the QR shows (or 127.0.0.1 for tests) and an
//     Origin, when sent, must be the same: no CORS, no DNS rebinding;
//   - only clients on a private network (10/8, 172.16/12, 192.168/16, 127/8);
//   - LW_ERROS_MAX wrong paths stop the server (guessing is not cheap);
//   - bodies have a declared size within the app's limit; nothing received is
//     logged (only "N bytes for <app>");
//   - the TV never calls out from here: it only answers.
//   - a token can be renewed (lanweb_renovar): old links stop working.
// Requests are queued for the APP THREAD (lanweb_proximo); the server thread
// never touches app state. What a page shows comes from a JSON snapshot the
// app publishes (lanweb_publicar), served at <token>/estado.
#ifndef QRT_LANWEB_H
#define QRT_LANWEB_H
#include <stddef.h>

#define LW_APPS       8
#define LW_ERROS_MAX  40
#define LW_FILA       64
#define LW_CORPO_PADRAO 8192

typedef struct {
  char   app[16];
  char   rota[64];      // path after the token, without the leading '/', "" for the page
  char   cliente[48];   // the phone's address (one vote per phone in party mode)
  char  *corpo;         // malloc'd, NUL-terminated; the caller frees with lanweb_liberar
  size_t n;
} LwPedido;

int  lanweb_disponivel(void);
// Registers (or replaces) an app's page. `maxCorpo` = largest POST accepted
// (0 = LW_CORPO_PADRAO). Starts the server if needed. 1 = ok.
int  lanweb_registrar(const char *app, const char *html, size_t maxCorpo);
void lanweb_remover(const char *app);
const char *lanweb_url(const char *app);       // "http://ip:port/<token>/" or ""
void lanweb_renovar(const char *app);          // new token: old links stop
// QR TIME: a page whose token is kept by the caller (bookmarkable link), and
// a fixed port to try first (set before the first page). 1 = ok.
int  lanweb_registrar_token(const char *app, const char *html, size_t maxCorpo, const char *token);
int  lanweb_gerar_token(char *dst, size_t n);  // dst > 12 bytes
void lanweb_porta_preferida(int porta);
// JSON served at <token>/estado (and the page polls it).
void lanweb_publicar(const char *app, const char *json);
// A file served at <token>/arquivo (backup download). Copied.
void lanweb_arquivo(const char *app, const void *dados, size_t n, const char *nome);
int  lanweb_proximo(LwPedido *p);              // app thread: 1 = one request taken
void lanweb_liberar(LwPedido *p);
void lanweb_parar(void);
// Field `nome` of an x-www-form-urlencoded body, decoded into dst (always
// NUL-terminated). 1 = present.
int  lanweb_campo(const char *corpo, const char *nome, char *dst, size_t n);
// HTML-escapes `s` into dst (for text the app puts into a page).
void lanweb_escapar(char *dst, size_t n, const char *s);
int  lanweb_porta(void);                       // tests
// Tests: the token of an app, to build URLs against 127.0.0.1.
const char *lanweb_teste_token(const char *app);
#endif
