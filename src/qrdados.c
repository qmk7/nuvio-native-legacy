#include "qrdados.h"
#include "pularserie.h"
#include "dados.h"
#include "perfis.h"
#include "mcfila.h"
#include "mcoffline.h"
#include "mcfontes.h"
#include "esporte.h"
#include "traktdup.h"
#include "agenda.h"
#include "mclfm.h"
#include "ytapi.h"
#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------- schema
static int disco, maisNovo;
// v1 = the phase 1-8 layout. Nothing is converted; the only thing added is
// the "seen" mark of every what's-new card of the old brand's releases (its
// changelog and its developer's support page are not QR TIME's). Adds only.
static int passoV1(void) {
  static const char *const vistas[] = { "11", "12", "13", "131", "132", "133", "134", "139", "14-celebracao", "142", "150",
    "152-ui", "160-ui", "170-ui", "172-ui", "174-ui", "180-ui", "20-guia", "201", "202", "208", "guia", NULL };
  char n[48];
  for (int i = 0; vistas[i]; i++) {
    char *t;
    snprintf(n, sizeof n, "novidades-%s.txt", vistas[i]);
    if ((t = dados_ler(n))) { free(t); continue; }
    dados_gravar(n, "1\n");
  }
  // The diagnostics presentation and the log-sending question talk about the
  // old brand's support server; QR TIME has none configured.
  { char *t = dados_ler("diagnostico-otimizacao-intro.cfg"); if (t) free(t); else dados_gravar("diagnostico-otimizacao-intro.cfg", "versao=1\n"); }
  { char *t = dados_ler("aviso-log.txt"); if (t) free(t); else dados_gravar("aviso-log.txt", "1\n"); }
  return 1;
}
// v2 = maximum-quality playback defaults (LG B5 + eARC soundbar), applied
// once after the settings load (qrdados_qualidade_aplicar), and only to
// options still at their factory value. The step itself only leaves a mark.
static int passoV2(void) { return dados_gravar("qrtime-quality.txt", "pendente\n"); }
static const QrdPasso PASSOS[] = { passoV1, passoV2 };
static const QrdPasso *passos = PASSOS;
static int nPassos = (int)(sizeof PASSOS / sizeof *PASSOS), alvo = QRD_ESQUEMA;

static int lerVersao(void) {
  char *t = dados_ler(QRD_ARQ), *v;
  int r = 0;
  if (t && (v = strstr(t, "schema="))) r = atoi(v + 7);
  free(t);
  return r < 0 ? 0 : r;
}
static void gravarVersao(int v) {
  char b[96];
  snprintf(b, sizeof b, "schema=%d\n", v);
  dados_gravar(QRD_ARQ, b);
}
void qrdados_teste_passos(const QrdPasso *p, int n, int a) { passos = p ? p : PASSOS; nPassos = p ? n : (int)(sizeof PASSOS / sizeof *PASSOS); alvo = p ? a : QRD_ESQUEMA; }
int qrdados_esquema_iniciar(void) {
  int antes = lerVersao(), v = antes;
  maisNovo = 0;
  if (v > alvo) {
    // A newer QR TIME wrote this. Downgrading would mean guessing; the files
    // are left as they are and the version stays the newer one.
    maisNovo = 1;
    printf("[dados] schema %d on disk is newer than this app (%d): left untouched\n", v, alvo);
    fflush(stdout);
    disco = v;
    return antes;
  }
  if (v < 1 && alvo >= 1) v = 0;
  while (v < alvo) {
    int i = v;   // step i: v -> v+1 (v = 0 means the first QR TIME start)
    if (i >= nPassos || !passos[i] || !passos[i]()) {
      // Stop at the last good version; the next start tries this step again.
      printf("[dados] schema upgrade %d -> %d failed: kept at %d, nothing removed\n", v, v + 1, v);
      fflush(stdout);
      break;
    }
    v++;
    gravarVersao(v);   // one step at a time: an interrupted upgrade resumes here
  }
  if (v != antes) { printf("[dados] schema %d -> %d\n", antes, v); fflush(stdout); }
  disco = v;
  return antes;
}
int qrdados_esquema_disco(void) { return disco; }
#pragma weak ajustes_qualidade_maxima_padrao
int ajustes_qualidade_maxima_padrao(void);
int qrdados_qualidade_aplicar(void) {
  char *t = dados_ler("qrtime-quality.txt");
  int r = 0;
  if (t && !strncmp(t, "pendente", 8) && ajustes_qualidade_maxima_padrao) {
    r = ajustes_qualidade_maxima_padrao();
    dados_gravar("qrtime-quality.txt", "feito\n");
  }
  free(t);
  return r;
}
int qrdados_mais_novo(void) { return maisNovo; }

// ---------------------------------------------------------------- cache
// The same regenerable formats as androidcache.c's allowlist (catalogue,
// guide and metadata caches, trailers, extras, subtitles), plus the music
// and sport answers QR TIME keeps. Anything not listed is user data.
int qrdados_e_cache(const char *n) {
  static const char *const arquivos[] = {
    "epg-*.xml", "epg-*.xml.gz", "epg-*.ts", "epg-x-*-p[0-9]*.bin",
    "ltv-indice.txt", "ltv-indice-p[0-9]*.txt", "ltv-xt.bin", "ltv-xt-p[0-9]*.bin", "ltv-st.bin", "ltv-st-p[0-9]*.bin",
    "ltv-[max][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f].bin",
    "ltv-[max][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]-p[0-9]*.bin",
    "m3u-cache-p[0-9]*.txt", "plugins-c-*.txt", "mkvass-*.ass",
    "noticias-*.txt", "noticias2-*.txt", "extras-*.txt", "meta-*.txt",
    "ficha-*.json", "serieaud-*.txt", "catalogo-*.bin",
    "anime-fribb.idx", "anime-fribb.retry", "explorar-mapa.txt", "explorar-viz.txt",
    "explorar-climas.txt", "frases-*.txt", "recomendacoes-feed.json", "recomendacoes-amigo.json",
    "negcache.txt", "corviva.txt", "loc-texto.txt", "legenda-ar-*.srt", NULL };
  static const char *const pastas[] = { "cache", "trailer", "extras", "fontes", "legendas", "tendencia", NULL };
  if (!n || !*n || strchr(n, '/') || strstr(n, "..")) return 0;
  for (int i = 0; pastas[i]; i++) if (!strcmp(n, pastas[i])) return 1;
  for (int i = 0; arquivos[i]; i++) if (!fnmatch(arquivos[i], n, 0)) return 1;
  return 0;
}
// Walks one level of the data folder; a cache folder is emptied recursively
// (never following symlinks) but kept, since live writers reuse its path.
static int varrer(const char *dir, int apagar, uint64_t *bytes, int fundo) {
  DIR *d;
  struct dirent *e;
  int n = 0;
  if (fundo > 16 || !(d = opendir(dir))) return 0;
  while ((e = readdir(d))) {
    char p[1200];
    struct stat st;
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (!fundo && !qrdados_e_cache(e->d_name)) continue;
    if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p || lstat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) n += varrer(p, apagar, bytes, fundo + 1);
    else if (S_ISREG(st.st_mode)) {
      if (bytes) *bytes += (uint64_t)st.st_size;
      if (apagar && !unlink(p)) n++;
    }
  }
  closedir(d);
  return n;
}
uint64_t qrdados_cache_bytes(void) {
  uint64_t b = 0;
  const char *dir = dados_dir();
  if (dir && *dir) varrer(dir, 0, &b, 0);
  return b;
}
int qrdados_cache_limpar(void) {
  const char *dir = dados_dir();
  int n;
  if (!dir || !*dir) return -1;
  n = varrer(dir, 1, NULL, 0);
  printf("[dados] cache cleared: %d file(s) removed (user data untouched)\n", n);
  fflush(stdout);
  return n;
}

// ---------------------------------------------------------------- resets
static const struct { const char *nome, *desc; int porTv; } RESETS[QRR_N] = {
  [QRR_CACHE]          = { "Limpar cache", "Remove guias, catálogos e imagens guardados. Eles voltam da internet. Contas, perfis e progresso ficam.", 1 },
  [QRR_MUSICA_OFFLINE] = { "Apagar músicas offline", "Remove as cópias offline desta TV. As músicas continuam no seu servidor.", 1 },
  [QRR_MUSICA_HIST]    = { "Limpar histórico e fila de música", "Só deste perfil. Favoritas e playlists ficam.", 0 },
  [QRR_MUSICA_BIBLIO]  = { "Apagar favoritas e playlists locais", "Só deste perfil. Playlists do seu servidor não são tocadas.", 0 },
  [QRR_AGENDA_SEGUIR]  = { "Deixar de seguir todas as séries", "Só deste perfil. A Agenda volta a mostrar só o que está na sua lista.", 0 },
  [QRR_ESPORTE]        = { "Redefinir o Centro de Esportes", "Só deste perfil: ligas, times e canais ligados a eventos.", 0 },
  [QRR_TRAKT_REGISTRO] = { "Esquecer o que foi enviado ao Trakt", "Só deste perfil. Nada é apagado no Trakt; o app só deixa de lembrar o que já enviou.", 0 },
  [QRR_CONTAS_PERFIL]  = { "Desconectar YouTube e Last.fm", "Só deste perfil. As chaves de API desta TV ficam.", 0 },
  [QRR_CHAVES_TV]      = { "Apagar chaves de API desta TV", "YouTube, Last.fm e Spotify, para todos os perfis desta TV.", 1 },
  [QRR_PULO_SERIES]    = { "Esquecer as séries com pulo automático", "Só deste perfil. A abertura e o resumo voltam a ter só o botão Pular nessas séries.", 0 },
};
const char *qrdados_reset_nome(int op) { return op >= 0 && op < QRR_N ? RESETS[op].nome : ""; }
const char *qrdados_reset_desc(int op) { return op >= 0 && op < QRR_N ? RESETS[op].desc : ""; }
int qrdados_reset_por_tv(int op) { return op >= 0 && op < QRR_N && RESETS[op].porTv; }

static void apagarPerfil(const char *fmt) {
  char n[80];
  int p = perfis_ativo();
  snprintf(n, sizeof n, fmt, p > 0 ? p : 1);
  dados_apagar(n);
}
int qrdados_resetar(int op) {
  int ok = 1;
  switch (op) {
    case QRR_CACHE: ok = qrdados_cache_limpar() >= 0; break;
    case QRR_MUSICA_OFFLINE: {
      McFaixa f;
      for (int guarda = 0; mco_n() > 0 && guarda < 100000; guarda++) {
        if (!mco_item(0, &f) || !mco_apagar(f.id)) { ok = 0; break; }
      }
      break;
    }
    case QRR_MUSICA_HIST:
      apagarPerfil("music-history-p%d.txt"); apagarPerfil("music-queue-p%d.txt");
      mcfila_perfil_trocou();
      break;
    case QRR_MUSICA_BIBLIO:
      apagarPerfil("music-favorites-p%d.txt"); apagarPerfil("music-playlists-p%d.txt");
      mcfila_perfil_trocou();
      break;
    case QRR_AGENDA_SEGUIR: apagarPerfil("agenda-follows-p%d.txt"); agenda_seguidas_recarregar(); break;
    case QRR_ESPORTE: apagarPerfil("esporte-p%d.txt"); esporte_carregar(); break;
    case QRR_TRAKT_REGISTRO: traktdup_esquecer(); break;
    case QRR_CONTAS_PERFIL:
      yt_sair(); mclfm_sair();
      apagarPerfil("youtube-account-p%d.txt"); apagarPerfil("lastfm-sessao-p%d.txt"); apagarPerfil("lastfm-pendentes-p%d.txt");
      break;
    case QRR_CHAVES_TV:
      dados_apagar("youtube.txt"); dados_apagar("lastfm.txt"); dados_apagar("spotify.txt");
      mclfm_carregar();
      break;
    case QRR_PULO_SERIES: apagarPerfil("skip-series-p%d.txt"); pularserie_recarregar(); break;
    default: return 0;
  }
  printf("[dados] reset %d (%s) -> %s\n", op, qrdados_reset_por_tv(op) ? "this TV" : "this profile", ok ? "ok" : "partial");
  fflush(stdout);
  return ok;
}
