// Music Center providers against tests/qrtime_music_mock.py: the Subsonic
// client (src/subsonic.c, Navidrome-shaped server) and the WebDAV client
// (src/webdav.c), plus tag reading through HTTP Range (src/mcfmt.c).
//
//   qrtime_subsonic <base url> <audio dir>            (server running)
//   qrtime_subsonic <base url> <audio dir> offline    (server down)
#include "subsonic.h"
#include "webdav.h"
#include "mcfmt.h"
#include "md5.h"
#include "rede.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

static McFaixa fx[200];
static McAlbum al[50];
static McArtista ar[50];
static const McFaixa *achar(int n, const char *id) { for (int i = 0; i < n; i++) if (!strcmp(fx[i].id, id)) return &fx[i]; return NULL; }

int main(int argc, char **argv) {
  SsServidor s;
  DavServidor dv;
  SsInfo info;
  int n = 0, e;
  char url[2048], red[2048];
  if (argc < 3) return 2;
  memset(&s, 0, sizeof s);
  snprintf(s.url, sizeof s.url, "%s/", argv[1]);   // trailing slash on purpose
  snprintf(s.usuario, sizeof s.usuario, "qrtuser");
  snprintf(s.senha, sizeof s.senha, "qrt pass+1");
  memset(&dv, 0, sizeof dv);
  snprintf(dv.url, sizeof dv.url, "%s/dav/Music/", argv[1]);
  snprintf(dv.usuario, sizeof dv.usuario, "davuser");
  snprintf(dv.senha, sizeof dv.senha, "davpass");

  if (argc > 3 && !strcmp(argv[3], "offline")) {
    DavItem it[4];
    CONFERE(ss_ping(&s, &info) == MC_ERRO_REDE, "offline: Subsonic ping -> network error");
    CONFERE(dav_listar(&dv, "", it, 4, &n) == DAV_ERRO_REDE && n == 0, "offline: WebDAV -> network error");
    return falhas ? 1 : 0;
  }

  // ---- auth ----
  ss_teste_sal("abcdef123456");
  ss_url_stream(&s, "s1", url, sizeof url);
  {
    char esperado[33], m[64];
    snprintf(m, sizeof m, "qrt pass+1abcdef123456");
    md5_hex(m, strlen(m), esperado);
    CONFERE(strstr(url, esperado) && strstr(url, "s=abcdef123456") && !strstr(url, "qrt pass"), "token = md5(password + salt); the password never travels");
  }
  CONFERE(strstr(url, "format=raw") && strstr(url, "maxBitRate=0"), "stream asks for the original file (format=raw, no transcode)");
  ss_redigir(url, red, sizeof red);
  CONFERE(!strstr(red, "qrtuser") && strstr(red, "t=***") && strstr(red, "s=***") && strstr(red, "id=s1"), "redacted URL hides u/t/s");
  ss_teste_sal(NULL);
  {
    char a[2048], b[2048];
    ss_url_stream(&s, "s1", a, sizeof a);
    ss_url_stream(&s, "s1", b, sizeof b);
    CONFERE(strcmp(a, b), "a fresh salt on every request");
  }
  CONFERE(ss_ping(&s, &info) == MC_OK && !strcmp(info.tipo, "navidrome") && info.openSubsonic && !strcmp(info.apiVersao, "1.16.1"),
          "ping: Navidrome, OpenSubsonic, API 1.16.1");
  {
    SsServidor x = s;
    snprintf(x.senha, sizeof x.senha, "wrong");
    CONFERE(ss_ping(&x, NULL) == MC_ERRO_LOGIN, "wrong password -> login error");
    snprintf(x.usuario, sizeof x.usuario, "ldapuser");
    snprintf(x.senha, sizeof x.senha, "ldappw");
    CONFERE(ss_ping(&x, NULL) == MC_ERRO_LOGIN, "token refused (41) over plain http: no legacy password fallback");
  }

  // ---- browse ----
  e = ss_artistas(&s, ar, 50, &n);
  CONFERE(e == MC_OK && n == 3, "artists (index -> artist)");
  e = ss_artista_albuns(&s, "ar1", al, 50, &n);
  CONFERE(e == MC_OK && n == 1 && !strcmp(al[0].nome, "Night Sessions") && al[0].faixas == 2, "artist -> albums");
  e = ss_albuns(&s, "alphabeticalByName", NULL, 0, al, 2, &n);
  CONFERE(e == MC_OK && n == 2 && !strcmp(al[0].nome, "Big Box"), "album list, page 1 (size 2)");
  e = ss_albuns(&s, "alphabeticalByName", NULL, 2, al, 2, &n);
  CONFERE(e == MC_OK && n == 1 && !strcmp(al[0].nome, "Night Sessions"), "album list, page 2 (offset)");
  {
    McAlbum info2;
    e = ss_album(&s, "al1", &info2, fx, 200, &n);
    CONFERE(e == MC_OK && n == 2 && !strcmp(info2.nome, "Night Sessions") && !strcmp(info2.artista, "QRT Quartet"), "album -> tracks");
    CONFERE(achar(n, "s2") && achar(n, "s2")->bits == 24 && achar(n, "s2")->taxa == 96000 && achar(n, "s2")->canais == 2
            && !strcmp(achar(n, "s2")->formato, "flac") && achar(n, "s2")->dur == 5, "track metadata incl. OpenSubsonic bit depth / rate");
    CONFERE(!strcmp(achar(n, "s1")->artistaId, "ar1") && !strcmp(achar(n, "s1")->albumId, "al1"),
            "nested OpenSubsonic arrays (artists[].id) do not shadow the track's own fields");
  }
  e = ss_album(&s, "al3", NULL, fx, 200, &n);
  CONFERE(e == MC_OK && n == 61, "a 61-track album");
  e = ss_album(&s, "nope", NULL, fx, 200, &n);
  CONFERE(e == MC_ERRO_NAO_SUPORTA, "missing album -> not found (70)");
  {
    McGenero g[20];
    e = ss_generos(&s, g, 20, &n);
    CONFERE(e == MC_OK && n == 3, "genres");
  }
  e = ss_genero_faixas(&s, "Classical", 50, fx, 20, &n);
  CONFERE(e == MC_OK && n == 11, "songs by genre, offset 50 -> the last 11");
  e = ss_album(&s, "al2", NULL, fx, 200, &n);
  CONFERE(achar(n, "s3") && achar(n, "s3")->bits == 0 && achar(n, "s3")->kbps == 320, "MP3: no bit depth invented, 320 kbps");
  CONFERE(achar(n, "s4") && achar(n, "s4")->canais == 6, "5.1 track: 6 channels as reported");

  // ---- search ----
  {
    int nAr, nAl, nFx;
    e = ss_buscar(&s, "night", 0, ar, 10, &nAr, al, 10, &nAl, fx, 10, &nFx);
    CONFERE(e == MC_OK && nAl == 1 && nFx == 2, "search: albums and songs");
    e = ss_buscar(&s, "track", 10, ar, 10, &nAr, al, 10, &nAl, fx, 10, &nFx);
    CONFERE(e == MC_OK && nFx == 10 && !strcmp(fx[0].titulo, "Track 15"), "search songs, second page (songOffset)");
  }

  // ---- favourites, scrobble, lyrics ----
  e = ss_favoritos(&s, fx, 50, &n);
  CONFERE(e == MC_OK && n == 1 && !strcmp(fx[0].id, "s3") && fx[0].fav, "starred songs");
  CONFERE(ss_favoritar(&s, "s1", 1) == MC_OK && ss_favoritos(&s, fx, 50, &n) == MC_OK && n == 2, "star a song");
  CONFERE(ss_favoritar(&s, "s3", 0) == MC_OK && ss_favoritos(&s, fx, 50, &n) == MC_OK && n == 1 && !strcmp(fx[0].id, "s1"), "unstar a song");
  CONFERE(ss_scrobble(&s, "s1", 1700000000000LL, 0) == MC_OK && ss_scrobble(&s, "s1", 1700000000000LL, 1) == MC_OK, "now playing + submission");
  e = ss_albuns(&s, "recent", NULL, 0, al, 10, &n);
  CONFERE(e == MC_OK && n == 1 && !strcmp(al[0].id, "al1"), "recently played comes from the server after a scrobble");
  {
    char lrc[2000];
    McFaixa f;
    memset(&f, 0, sizeof f);
    snprintf(f.id, sizeof f.id, "s1");
    e = ss_letra(&s, &f, lrc, sizeof lrc);
    CONFERE(e == MC_OK && strstr(lrc, "[00:00.00]First line") && strstr(lrc, "[00:02.50]Second line") && strstr(lrc, "[01:01.23]Later"),
            "OpenSubsonic synced lyrics -> LRC");
    snprintf(f.id, sizeof f.id, "s3");
    snprintf(f.artista, sizeof f.artista, "Sun Trio");
    snprintf(f.titulo, sizeof f.titulo, "Morning Tone");
    e = ss_letra(&s, &f, lrc, sizeof lrc);
    CONFERE(e == MC_OK && !strcmp(lrc, "Plain line one\nPlain line two"), "legacy getLyrics fallback");
    snprintf(f.id, sizeof f.id, "s4");
    snprintf(f.titulo, sizeof f.titulo, "Surround Test");
    CONFERE(ss_letra(&s, &f, lrc, sizeof lrc) == MC_ERRO_NAO_SUPORTA, "no lyrics -> reported as none");
  }

  // ---- playlists ----
  {
    const char *ids[] = { "s1", "s2", "s3" };
    const char *mais[] = { "s4" };
    int rem[] = { 0 };
    char pid[96];
    McPlaylist pl[10], info3;
    e = ss_playlist_criar(&s, "Road Trip", ids, 3, pid, sizeof pid);
    CONFERE(e == MC_OK && pid[0], "create a server playlist");
    e = ss_playlist_editar(&s, pid, "Road Trip 2", mais, 1, rem, 1);
    CONFERE(e == MC_OK, "edit: rename, add one, remove index 0");
    e = ss_playlist(&s, pid, &info3, fx, 50, &n);
    CONFERE(e == MC_OK && n == 3 && !strcmp(fx[0].id, "s2") && !strcmp(fx[2].id, "s4") && !strcmp(info3.nome, "Road Trip 2"), "playlist content after the edit");
    e = ss_playlists(&s, pl, 10, &n);
    CONFERE(e == MC_OK && n == 1 && pl[0].faixas == 3, "playlists list");
    CONFERE(ss_playlist_apagar(&s, pid) == MC_OK && ss_playlists(&s, pl, 10, &n) == MC_OK && n == 0, "delete a playlist");
  }

  // ---- streaming: the stream URL serves the original bytes ----
  {
    long tam = 0;
    char *b;
    ss_url_stream(&s, "s2", url, sizeof url);
    b = rede_baixar_trecho(url, 20, 0, 65535, &tam);
    {
      McFmt m; McFaixa f; size_t pr;
      memset(&f, 0, sizeof f);
      CONFERE(b && tam > 1000 && mcfmt_ler((unsigned char *)b, (size_t)tam, 0, &m, &f, &pr) == MCF_FLAC && m.bits == 24 && m.taxa == 96000,
              "stream: original FLAC 24/96 bytes (range read), no transcode");
    }
    free(b);
  }

  // ---- WebDAV ----
  {
    DavItem it[40];
    char line[600];
    e = dav_listar(&dv, "", it, 40, &n);
    CONFERE(e == DAV_OK && n == 2 && it[0].pasta && it[1].pasta && !strcmp(it[0].nome, "Jazz Folder") && !strcmp(it[1].nome, "Rock & Roll"),
            "WebDAV root: two folders (self, text, image and hidden files skipped)");
    e = dav_listar(&dv, it[0].href, it, 40, &n);
    CONFERE(e == DAV_OK && n == 3 && !strcmp(it[0].nome, "01 - Blue.flac") && it[0].tam > 1000 && !it[0].pasta, "WebDAV folder: 3 music files, with sizes");
    {   // tags through Range (the 24-bit file has a 200 KiB picture first)
      long tam;
      long long total;
      int er;
      unsigned char *b = dav_trecho(&dv, it[1].href, 0, 65535, &tam, &total, &er);
      McFmt m; McFaixa f; size_t pr = 0;
      memset(&f, 0, sizeof f);
      mcfmt_ler(b, (size_t)tam, total, &m, &f, &pr);
      free(b);
      CONFERE(pr > 65536, "Range read: tags beyond 64 KiB -> ask for more");
      b = dav_trecho(&dv, it[1].href, 0, (long long)pr - 1, &tam, &total, &er);
      memset(&f, 0, sizeof f);
      mcfmt_ler(b, (size_t)tam, total, &m, &f, &pr);
      free(b);
      CONFERE(m.bits == 24 && m.taxa == 96000 && !strcmp(f.titulo, "With Cover"), "WebDAV file: FLAC 24/96 tags read by Range");
    }
    dav_url(&dv, it[0].href, url, sizeof url);
    CONFERE(!strncmp(url, argv[1], strlen(argv[1])) && strstr(url, "Jazz%20Folder/01%20-%20Blue.flac"), "file URL on the configured server");
    dav_cabecalho(&dv, line, sizeof line);
    CONFERE(!strcmp(line, "Authorization: Basic ZGF2dXNlcjpkYXZwYXNz"), "Basic auth header");
    dav_url(&dv, "https://evil.example.com/x.flac", url, sizeof url);
    CONFERE(!url[0], "an href on another host is refused (credentials stay home)");
    e = dav_listar(&dv, "/dav/Music/Rock%20%26%20Roll/", it, 40, &n);
    CONFERE(e == DAV_OK && n == 2, "folder with & in its name");
    {
      DavServidor x = dv;
      snprintf(x.senha, sizeof x.senha, "nope");
      CONFERE(dav_listar(&x, "", it, 40, &n) == DAV_ERRO_LOGIN, "wrong WebDAV password -> login error");
    }
    CONFERE(dav_listar(&dv, "/dav/Music/Missing/", it, 40, &n) == DAV_ERRO_NAO_ACHOU, "missing folder -> not found");
  }
  return falhas ? 1 : 0;
}
