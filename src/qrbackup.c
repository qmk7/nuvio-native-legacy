// QR TIME BACKUP of what the Nuvio account does NOT sync (IPTV accounts,
// music servers, YouTube/Last.fm/Spotify links and keys, Sport Center,
// followed series, per-series skips, Jellyfin/Emby/Plex, TheSportsDB key),
// as ONE file encrypted with a password (qrcripto.h). Made and restored from
// the phone page (Settings > Accounts, providers and data > Phone):
//   - "Create": the person types a password (8+ characters) twice; the TV
//     builds the file and the phone downloads it;
//   - "Restore": the phone sends the file and the password; nothing is written
//     unless the password and the file check out; only names on the list
//     below are written (no paths); the app asks for a restart.
// The password never leaves the request that carries it and is never stored.
#include "qrbackup.h"
#include "celapps.h"
#include "qrcripto.h"
#include "dados.h"
#include "idioma.h"
#include "ilha.h"
#include <dirent.h>
#include <fnmatch.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *const LISTA[] = {
  "livetv-contas-p*.txt", "xtream-p*.txt", "m3u-p*.txt", "stalker-p*.txt", "listas-p*.txt",
  "music-sources-p*.txt", "music-favorites-p*.txt", "music-playlists-p*.txt", "music-settings-p*.txt",
  "lastfm.txt", "lastfm-sessao-p*.txt", "youtube.txt", "youtube-account-p*.txt", "spotify.txt",
  "esporte-p*.txt", "agenda-follows-p*.txt", "skip-series-p*.txt", "tsdb.txt",
  "jellyfin-p*.txt", "emby-p*.txt", "plex-p*.txt", NULL };

int qrbackup_nome_ok(const char *n) {
  if (!n || !n[0] || strchr(n, '/') || strchr(n, '\\') || strstr(n, "..") || strlen(n) > 80) return 0;
  for (int i = 0; LISTA[i]; i++) if (!fnmatch(LISTA[i], n, 0)) return 1;
  return 0;
}

// "QRTIME-BACKUP 1\n" then, per file, "F <len> <name>\n<bytes>\n".
char *qrbackup_montar(size_t *n, int *arquivos) {
  DIR *d = opendir(dados_dir());
  struct dirent *e;
  size_t cap = 1 << 16, u = 0;
  char *b = malloc(cap);
  if (arquivos) *arquivos = 0;
  if (!b || !d) { free(b); if (d) closedir(d); return NULL; }
  u = (size_t)snprintf(b, cap, "QRTIME-BACKUP 1\n");
  while ((e = readdir(d)) != NULL) {
    char *t;
    size_t l;
    if (!qrbackup_nome_ok(e->d_name) || !(t = dados_ler(e->d_name))) continue;
    l = strlen(t);
    while (u + l + 200 > cap) { char *nb = realloc(b, cap * 2); if (!nb) { free(t); free(b); closedir(d); return NULL; } b = nb; cap *= 2; }
    u += (size_t)snprintf(b + u, cap - u, "F %zu %s\n", l, e->d_name);
    memcpy(b + u, t, l); u += l; b[u++] = '\n';
    memset(t, 0, l); free(t);
    if (arquivos) (*arquivos)++;
  }
  closedir(d);
  b[u] = 0;
  *n = u;
  return b;
}
int qrbackup_aplicar(const char *a, size_t n) {
  const char *p = a, *fimA = a + n;
  int k = 0;
  if (n < 16 || strncmp(a, "QRTIME-BACKUP 1\n", 16)) return -1;
  p += 16;
  while (p < fimA && *p == 'F') {
    size_t l;
    char nome[96];
    const char *nl = memchr(p, '\n', (size_t)(fimA - p));
    if (!nl || sscanf(p, "F %zu %95s", &l, nome) != 2 || (size_t)(fimA - nl - 1) < l + 1) return k ? k : -1;
    if (qrbackup_nome_ok(nome) && !memchr(nl + 1, 0, l)) {
      char *t = malloc(l + 1);
      if (t) { memcpy(t, nl + 1, l); t[l] = 0; if (dados_gravar(nome, t)) k++; memset(t, 0, l); free(t); }
    }
    p = nl + 1 + l + 1;
  }
  return k;
}

// ---------------------------------------------------------------- phone page
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int ocupado, pronto, erro, restaurados = -2;
static unsigned char *resultado; static size_t nResultado;
typedef struct { int restaurar; char senha[128]; unsigned char *dados; size_t n; } Trabalho;

static void *fio(void *arg) {
  Trabalho *t = arg;
  unsigned char *o = NULL;
  size_t no = 0;
  int r = 0;
  if (!t->restaurar) o = qrc_cifrar(t->senha, t->dados, t->n, &no), r = o ? 1 : 0;
  else r = qrc_decifrar(t->senha, t->dados, t->n, &o, &no);
  memset(t->senha, 0, sizeof t->senha);
  if (t->dados) { memset(t->dados, 0, t->n); free(t->dados); }
  pthread_mutex_lock(&trava);
  free(resultado);
  resultado = r == 1 ? o : NULL; nResultado = r == 1 ? no : 0;
  if (r != 1) free(o);
  erro = r == 1 ? 0 : r == 0 ? 1 : 2;   // 1 wrong password/damaged, 2 not a backup
  pronto = 1; ocupado = 0;
  pthread_mutex_unlock(&trava);
  free(t);
  return NULL;
}
static int b64v(int c) {
  return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1;
}
static unsigned char *b64dec(const char *s, size_t *n) {
  size_t l = strlen(s), k = 0;
  unsigned char *o = malloc(l * 3 / 4 + 4);
  unsigned v = 0; int bits = 0;
  if (!o) return NULL;
  for (; *s; s++) {
    int x = b64v((unsigned char)*s);
    if (x < 0) continue;
    v = (v << 6) | (unsigned)x; bits += 6;
    if (bits >= 8) { bits -= 8; o[k++] = (unsigned char)(v >> bits); }
  }
  *n = k;
  return o;
}
static void comecar(int restaurar, const char *senha, unsigned char *dados, size_t n) {
  pthread_t t;
  Trabalho *w = calloc(1, sizeof *w);
  if (!w) { free(dados); return; }
  w->restaurar = restaurar; snprintf(w->senha, sizeof w->senha, "%s", senha); w->dados = dados; w->n = n;
  pthread_mutex_lock(&trava);
  ocupado = 1; pronto = 0; erro = 0; restaurados = -2;
  pthread_mutex_unlock(&trava);
  lanweb_publicar("backup", "{\"ocupado\":1}");
  if (pthread_create(&t, NULL, fio, w)) { free(dados); free(w); ocupado = 0; return; }
  pthread_detach(t);
}
static int modoRestaurar;
static void pedido(const LwPedido *p) {
  char senha[128];
  if (ocupado) return;
  lanweb_campo(p->corpo, "senha", senha, sizeof senha);
  if (strlen(senha) < 8) { lanweb_publicar("backup", "{\"erro\":\"senha\"}"); memset(senha, 0, sizeof senha); return; }
  if (!strcmp(p->rota, "criar")) {
    size_t n = 0; int arq = 0;
    char *a = qrbackup_montar(&n, &arq);
    if (!a) { lanweb_publicar("backup", "{\"erro\":\"tv\"}"); return; }
    printf("[backup] criando: %d arquivo(s)\n", arq);
    modoRestaurar = 0;
    comecar(0, senha, (unsigned char *)a, n);
  } else if (!strcmp(p->rota, "restaurar")) {
    size_t n = 0;
    char *b64 = malloc(p->n + 1);
    unsigned char *d;
    if (!b64) return;
    if (!lanweb_campo(p->corpo, "dados", b64, p->n + 1)) { free(b64); return; }
    d = b64dec(b64, &n);
    free(b64);
    if (!d) return;
    modoRestaurar = 1;
    comecar(1, senha, d, n);
  }
  memset(senha, 0, sizeof senha);
}
static void quadro(Uint32 agora) {
  unsigned char *r = NULL; size_t n = 0; int e, pr;
  (void)agora;
  pthread_mutex_lock(&trava);
  pr = pronto; e = erro;
  if (pr) { r = resultado; n = nResultado; resultado = NULL; nResultado = 0; pronto = 0; }
  pthread_mutex_unlock(&trava);
  if (!pr) return;
  if (!modoRestaurar) {
    char nome[64];
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(nome, sizeof nome, "qrtime-backup-%Y%m%d.qrtb", &lt);
    if (r) { lanweb_arquivo("backup", r, n, nome); lanweb_publicar("backup", "{\"pronto\":1}"); puts("[backup] pronto para baixar"); }
    else lanweb_publicar("backup", "{\"erro\":\"tv\"}");
  } else {
    if (!r) {
      lanweb_publicar("backup", e == 2 ? "{\"erro\":\"arquivo\"}" : "{\"erro\":\"senha\"}");
      ilha_avisar("backup", ILHA_ERRO, "aj_hard-drive", i18n(e == 2 ? "Este arquivo não é um backup do QR TIME." : "Senha errada ou arquivo danificado. Nada foi mudado."), 4000u, 0);
    } else {
      char j[64], t[200];
      int k = qrbackup_aplicar((const char *)r, n);
      snprintf(j, sizeof j, "{\"restaurados\":%d}", k);
      lanweb_publicar("backup", j);
      snprintf(t, sizeof t, i18n("Backup restaurado (%d arquivos). Feche e abra o app para usar."), k < 0 ? 0 : k);
      ilha_avisar("backup", k > 0 ? ILHA_OK : ILHA_ERRO, "aj_hard-drive", t, 8000u, 0);
      printf("[backup] restaurado: %d arquivo(s)\n", k);
    }
  }
  if (r) { memset(r, 0, n); free(r); }
  fflush(stdout);
}
static char *pagina(void) {
  char corpo[3000], s[11][220];
  const char *k[11] = { "Criar backup", "Senha (8 caracteres ou mais)", "Repita a senha", "Criar e baixar",
                        "Restaurar", "Arquivo de backup", "Restaurar na TV", "Guarde a senha: sem ela o arquivo não abre.",
                        "Senha errada ou arquivo danificado. Nada foi mudado.", "As senhas não são iguais.", "Pronto." };
  for (int i = 0; i < 11; i++) lanweb_escapar(s[i], sizeof s[i], i18n(k[i]));
  snprintf(corpo, sizeof corpo,
    "<h2>%s</h2><label>%s</label><input id=\"s1\" type=\"password\" autocomplete=\"new-password\"><label>%s</label>"
    "<input id=\"s2\" type=\"password\" autocomplete=\"new-password\"><button id=\"cr\">%s</button><p><small>%s</small></p>"
    "<h2>%s</h2><label>%s</label><input id=\"f\" type=\"file\"><label>%s</label><input id=\"s3\" type=\"password\">"
    "<button class=\"s\" id=\"rs\">%s</button><div id=\"msg\" class=\"msg\"></div>"
    "<script>var T={erro:\"%s\",dif:\"%s\",ok:\"%s\"};</script>",
    s[0], s[1], s[2], s[3], s[7], s[4], s[5], s[1], s[6], s[8], s[9], s[10]);
  return celapps_pagina("Backup e restauração", corpo,
    "function v(i){return document.getElementById(i).value}"
    "function esperar(fim){var t=setInterval(function(){estado(function(e){if(e.ocupado)return;clearInterval(t);fim(e)})},800)}"
    "document.getElementById('cr').onclick=function(){if(v('s1')!==v('s2')){msg(T.dif);return}msg('\\u2026');"
    "post('criar',{senha:v('s1')}).then(function(){esperar(function(e){if(e.pronto){msg(T.ok);location.href='arquivo'}else msg(T.erro)})})};"
    "document.getElementById('rs').onclick=function(){var f=document.getElementById('f').files[0];if(!f)return;var r=new FileReader();"
    "r.onload=function(){var b=r.result.split(',')[1];msg('\\u2026');post('restaurar',{senha:v('s3'),dados:b}).then(function(){"
    "esperar(function(e){msg(e.restaurados>=0?T.ok:T.erro)})})};r.readAsDataURL(f)};");
}
void qrbackup_iniciar(void) {
  CelAppDef d = { pagina, pedido, quadro, 8u << 20 };
  celapps_definir(CA_BACKUP, &d);
}
