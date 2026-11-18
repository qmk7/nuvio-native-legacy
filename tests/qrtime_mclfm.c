// Music Center: Last.fm (src/mclfm.c) and lyrics (src/mclrc.c) against
// tests/qrtime_music_mock.py, which checks api_sig like the real service.
//
//   qrtime_mclfm <data dir> <base url> <work dir>
#include "mclfm.h"
#include "mclrc.h"
#include "dados.h"
#include "md5.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int perfil = 1;
uint64_t perfis_sessao_geracao(void) { return (uint64_t)perfil; }
int perfis_sessao_atual(uint64_t s) { return s == (uint64_t)perfil; }
int perfis_sessao_travar(uint64_t s) { return s == (uint64_t)perfil; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return perfil; }

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)

static McFaixa faixa(const char *art, const char *tit, const char *alb, int dur) {
  McFaixa f;
  memset(&f, 0, sizeof f);
  snprintf(f.id, sizeof f.id, "ss00000000:x");
  snprintf(f.artista, sizeof f.artista, "%s", art);
  snprintf(f.titulo, sizeof f.titulo, "%s", tit);
  snprintf(f.album, sizeof f.album, "%s", alb);
  f.dur = dur;
  return f;
}
static int linhasNoLog(const char *dir, const char *txt) {
  char c[600], l[600];
  FILE *f;
  int n = 0;
  snprintf(c, sizeof c, "%s/lastfm.log", dir);
  if (!(f = fopen(c, "r"))) return 0;
  while (fgets(l, sizeof l, f)) if (strstr(l, txt)) n++;
  fclose(f);
  return n;
}

int main(int argc, char **argv) {
  char url[400], b[400], lrc[2000];
  if (argc < 4) return 2;
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(argv[1]);
  snprintf(b, sizeof b, "%s/lfm/", argv[2]);
  mclfm_definir_base(b);
  snprintf(b, sizeof b, "%s/lrclib/api", argv[2]);
  mclrc_definir_base(b);

  // ---- signature (Last.fm rule: sorted k+v, no format, + secret) ----
  {
    const char *p[] = { "method=track.scrobble", "api_key=k1", "format=json", "artist=A", NULL };
    char s[33], e[33];
    mclfm_assinar(p, "sec", s);
    md5_hex("api_keyk1artistAmethodtrack.scrobblesec", strlen("api_keyk1artistAmethodtrack.scrobblesec"), e);
    CONFERE(!strcmp(s, e), "api_sig = md5(sorted params without format + secret)");
  }
  // ---- sign-in ----
  CONFERE(!mclfm_tem_chave() && mclfm_entrar_iniciar(url, sizeof url) == MC_ERRO_LOGIN, "without an API key: asks for it");
  CONFERE(!mclfm_definir_chave("bad key!", NULL), "a key with symbols is refused");
  mclfm_definir_chave("qrtlfmkey", NULL);
  mclfm_definir_chave(NULL, "wrongsecret");
  CONFERE(mclfm_entrar_iniciar(url, sizeof url) == MC_ERRO_LOGIN, "wrong secret -> the signature is refused");
  mclfm_definir_chave(NULL, "qrtsecret");
  CONFERE(mclfm_entrar_iniciar(url, sizeof url) == MC_OK && !strcmp(url, "https://www.last.fm/api/auth/?api_key=qrtlfmkey&token=TOK1"),
          "token + the approval URL for the phone");
  CONFERE(mclfm_entrar_conferir() == 0, "not yet approved");
  CONFERE(mclfm_entrar_conferir() == 1 && mclfm_conectado(), "approved: session kept");
  {
    char u[96];
    mclfm_usuario(u, sizeof u);
    CONFERE(!strcmp(u, "qrtlistener"), "the Last.fm user name");
  }
  // ---- scrobbling ----
  {
    McFaixa f = faixa("Sun Trio", "Morning Tone", "Day Album", 245);
    mclfm_agora(&f);
    CONFERE(linhasNoLog(argv[3], "track.updateNowPlaying|Sun Trio|Morning Tone|") == 1, "now playing sent (signed)");
    mclfm_tocou(&f, 1763400000000LL);
    CONFERE(linhasNoLog(argv[3], "track.scrobble|Sun Trio|Morning Tone|1763400000") == 1, "scrobble sent with the start time (seconds)");
    // Service down: kept and sent with the next one.
    {
      char c[600];
      FILE *x;
      McFaixa g = faixa("QRT Quartet", "Blue Hour", "Night Sessions", 300);
      snprintf(c, sizeof c, "%s/lfm_down", argv[3]);
      x = fopen(c, "w"); fclose(x);
      mclfm_tocou(&g, 1763400300000LL);
      CONFERE(mclfm_pendentes() == 1 && linhasNoLog(argv[3], "Blue Hour") == 0, "Last.fm down: the scrobble waits on disk");
      remove(c);
      f.titulo[0] = 0; snprintf(f.titulo, sizeof f.titulo, "Midnight Line");
      mclfm_tocou(&f, 1763400600000LL);
      CONFERE(mclfm_pendentes() == 0 && linhasNoLog(argv[3], "track.scrobble|QRT Quartet|Blue Hour|1763400300") == 1
              && linhasNoLog(argv[3], "Midnight Line") == 1, "back up: the waiting scrobble goes first, then the new one");
    }
    perfil = 2;
    CONFERE(!mclfm_conectado(), "profile 2: not connected (session per profile)");
    mclfm_agora(&f);
    CONFERE(linhasNoLog(argv[3], "updateNowPlaying") == 1, "profile 2 sends nothing");
    perfil = 1;
  }
  // ---- metadata ----
  {
    char bio[600], img[200], sim[5][128];
    CONFERE(mclfm_artista("Sun Trio", bio, sizeof bio, img, sizeof img) == MC_OK && !strcmp(bio, "A test band from the fixture."),
            "artist bio (link tail removed)");
    CONFERE(mclfm_similares("QRT Quartet", sim, 5) == 2 && !strcmp(sim[0], "Sun Trio"), "similar artists (recommendations)");
  }
  mclfm_sair();
  CONFERE(!mclfm_conectado() && !dados_ler("lastfm-sessao-p1.txt"), "sign out removes the session");

  // ---- lyrics ----
  {
    McFaixa f = faixa("Sun Trio", "Surround Test", "Day Album", 3);
    McLinha v[50];
    int sinc, n;
    CONFERE(mclrc_buscar(&f, lrc, sizeof lrc) == MC_OK && strstr(lrc, "[00:00.50] Round we go"), "LRCLIB synced lyrics");
    n = mclrc_parse(lrc, v, 50, &sinc);
    CONFERE(n == 2 && sinc && v[0].ms == 500 && v[1].ms == 1750 && !strcmp(v[1].texto, " And back"), "LRC parsed into timed lines");
    CONFERE(mclrc_linha_em(v, n, 100) == -1 && mclrc_linha_em(v, n, 600) == 0 && mclrc_linha_em(v, n, 5000) == 1, "current line by position");
    n = mclrc_parse("[ar:X]\n[00:10.5]a\n[00:05.123][00:20.00]b\n", v, 50, &sinc);
    CONFERE(n == 3 && v[0].ms == 5123 && !strcmp(v[0].texto, "b") && v[1].ms == 10500 && v[2].ms == 20000, "multi-timestamp lines, sorted; tags skipped");
    n = mclrc_parse("one\ntwo\n\n", v, 50, &sinc);
    CONFERE(n == 2 && !sinc, "plain lyrics: untimed lines");
    f = faixa("QRT Quartet", "Blue Hour", "Night Sessions", 5);
    CONFERE(mclrc_buscar(&f, lrc, sizeof lrc) == MC_OK && !strcmp(lrc, "\xe2\x99\xaa"), "instrumental: shown as such, not 'no lyrics'");
    f = faixa("Nobody", "Nothing", "", 0);
    CONFERE(mclrc_buscar(&f, lrc, sizeof lrc) == MC_ERRO_NAO_SUPORTA, "not found -> none");
  }
  return falhas ? 1 : 0;
}
