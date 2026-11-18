// MUSIC CENTER: a WebDAV music folder (Nextcloud, ownCloud, Apache mod_dav,
// rclone serve webdav, a NAS...). PROPFIND Depth: 1 lists one folder; files
// are streamed and their tags read by HTTP Range, with HTTP Basic auth.
// The password is sent only in the Authorization header, never in a URL,
// and never logged. Every call BLOCKS: run it on a worker thread.
#ifndef NV_WEBDAV_H
#define NV_WEBDAV_H
#include <stddef.h>

typedef struct {
  char url[384];      // the music folder: "https://cloud.example.com/remote.php/dav/files/me/Music/"
  char usuario[96], senha[128];
} DavServidor;

typedef struct {
  char nome[256];     // decoded file/folder name
  char href[768];     // server path, percent-encoded as the server sent it
  int  pasta;
  long long tam;
  char mime[48];
} DavItem;

enum { DAV_OK = 0, DAV_ERRO_REDE, DAV_ERRO_LOGIN, DAV_ERRO_NAO_ACHOU, DAV_ERRO_SERVIDOR, DAV_ERRO_OUTRO };

// Multistatus XML -> items, without the folder itself (`self` href). Folders
// first, then files, each by name. -1 when not a multistatus.
int dav_parse_lista(const char *xml, const char *self, DavItem *out, int max);
// A file the Music Center can offer by its extension (flac, wav, mp3, m4a,
// aac, alac in m4a, ogg, opus, aif/aiff). Whether the TV decodes it is the
// player's question, not this one.
int dav_eh_musica(const char *nome);

// List `href` ("" = the configured folder). DAV_*.
int dav_listar(const DavServidor *s, const char *href, DavItem *out, int max, int *n);
// Bytes [ini, fim] of a file (tags). Caller frees. NULL on failure (*erro).
unsigned char *dav_trecho(const DavServidor *s, const char *href, long long ini, long long fim,
                          long *tam, long long *total, int *erro);
// Absolute URL for an href, and the auth header line ("Authorization: Basic
// ...") the player and the range reads send with it.
void dav_url(const DavServidor *s, const char *href, char *url, size_t n);
void dav_cabecalho(const DavServidor *s, char *linha, size_t n);
// Percent-decoding of a path segment (display names).
void dav_decodificar(const char *s, char *d, size_t n);
#endif
