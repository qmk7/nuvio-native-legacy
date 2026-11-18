#include "qrcontas.h"
#include "iptvfontes.h"
#include "iptvteste.h"
#include "xtream.h"
#include "m3u.h"
#include "stalker.h"
#include "guia.h"
#include "addons.h"
#include "mcfontes.h"
#include "mccat.h"
#include "trakt.h"
#include "simkl.h"
#include "ytapi.h"
#include "mclfm.h"
#include "mcspotify.h"
#include "descoberta.h"
#include "tsdb.h"
#include "jellyfin.h"
#include "plex.h"
#include "dados.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ---------------------------------------------------------------- test results
// One test at a time; results are kept per row (type + ref) for this session.
typedef struct { int tipo; char ref[24]; int estado; char msg[120]; } Resultado;
#define RES_MAX 32
static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static Resultado res[RES_MAX];
static int nRes, testando;

static Resultado *achar(int tipo, const char *ref, int criar) {
  for (int i = 0; i < nRes; i++) if (res[i].tipo == tipo && !strcmp(res[i].ref, ref)) return &res[i];
  if (!criar) return NULL;
  if (nRes == RES_MAX) { memmove(res, res + 1, sizeof *res * (RES_MAX - 1)); nRes--; }
  memset(&res[nRes], 0, sizeof res[nRes]);
  res[nRes].tipo = tipo; snprintf(res[nRes].ref, sizeof res[nRes].ref, "%s", ref);
  return &res[nRes++];
}
static void anotar(int tipo, const char *ref, int estado, const char *msg) {
  pthread_mutex_lock(&trava);
  Resultado *r = achar(tipo, ref, 1);
  r->estado = estado; snprintf(r->msg, sizeof r->msg, "%s", msg ? msg : "");
  pthread_mutex_unlock(&trava);
}
int qrc_resultado(const QrcLinha *l, char *msg, unsigned n) {
  int e = -1;
  if (msg && n) msg[0] = 0;
  if (!l) return -1;
  if (l->tipo == QRC_IPTV) {
    char m[160];
    int s = iptvteste_estado((unsigned)strtoul(l->ref, NULL, 10), m, sizeof m);
    if (s == IPTVT_NADA) return -1;
    if (msg && n) snprintf(msg, n, "%s", m);
    return s == IPTVT_TESTANDO ? QRC_E_TESTANDO : s == IPTVT_OK ? QRC_E_LIGADO : QRC_E_ERRO;
  }
  pthread_mutex_lock(&trava);
  Resultado *r = achar(l->tipo, l->ref, 0);
  if (r) { e = r->estado; if (msg && n) snprintf(msg, n, "%s", r->msg); }
  pthread_mutex_unlock(&trava);
  return e;
}

// ---------------------------------------------------------------- the list
static QrcLinha *add(QrcLinha *out, int *n, int max, int tipo, const char *ref, const char *nome, int traduzir,
                     int estado, int pode, const char *detalhe) {
  QrcLinha *l;
  if (*n >= max) return NULL;
  l = &out[(*n)++];
  memset(l, 0, sizeof *l);
  l->tipo = tipo; l->traduzir = traduzir; l->estado = estado; l->pode = pode;
  snprintf(l->ref, sizeof l->ref, "%s", ref ? ref : "");
  snprintf(l->nome, sizeof l->nome, "%s", nome ? nome : "");
  snprintf(l->detalhe, sizeof l->detalhe, "%s", detalhe ? detalhe : "");
  return l;
}
static const char *tipoIptv(int t) { return t == IPTV_XTREAM ? "Xtream Codes" : t == IPTV_M3U ? "M3U" : t == IPTV_STALKER ? "Stalker" : "IPTV"; }

int qrc_montar(QrcLinha *out, int max) {
  int n = 0;
  char b[160];
  // IPTV accounts
  {
    IptvConta c[16];
    int k = iptv_contas(c, 16);
    if (!k) add(out, &n, max, QRC_IPTV, "", "TV ao vivo (IPTV)", 1, QRC_E_NAO, QRC_P_ABRIR, "Nenhuma conta. Adicione em Ajustes > TV ao vivo.");
    for (int i = 0; i < k; i++) {
      char ref[24];
      snprintf(ref, sizeof ref, "%u", c[i].id);
      add(out, &n, max, QRC_IPTV, ref, c[i].nome[0] ? c[i].nome : tipoIptv(c[i].tipo), 0,
          c[i].ativa ? QRC_E_CONFIG : QRC_E_DESLIGADO, QRC_P_TESTAR | QRC_P_ALTERNAR | QRC_P_ATUALIZAR | QRC_P_ABRIR, tipoIptv(c[i].tipo));
    }
  }
  // Add-ons, one row each
  {
    int k = addons_n();
    if (!k) add(out, &n, max, QRC_ADDONS, "", "Addons", 1, QRC_E_NAO, QRC_P_ABRIR, "Nenhum addon instalado.");
    for (int i = 0; i < k && n < max; i++) {
      char ref[24];
      snprintf(ref, sizeof ref, "%d", i);
      add(out, &n, max, QRC_ADDONS, ref, addons_nome(i)[0] ? addons_nome(i) : "Addon", 0,
          addons_ativo(i) ? QRC_E_CONFIG : QRC_E_DESLIGADO, QRC_P_ALTERNAR | QRC_P_ATUALIZAR | QRC_P_ABRIR, "Addon");
    }
  }
  // Music sources
  {
    McFonte f;
    int any = 0;
    for (int i = 0; mcfontes_copiar(i, &f); i++) {
      any = 1;
      add(out, &n, max, QRC_MUSICA, f.chave, f.nome, 0, f.desligada ? QRC_E_DESLIGADO : QRC_E_CONFIG,
          QRC_P_TESTAR | QRC_P_ALTERNAR | QRC_P_ABRIR, f.tipo == MCS_SUBSONIC ? "Navidrome / Subsonic" : "WebDAV");
    }
    if (!any) add(out, &n, max, QRC_MUSICA, "", "Servidor de música", 1, QRC_E_NAO, QRC_P_ABRIR, "Navidrome, Subsonic ou WebDAV. Adicione em Música > Serviços.");
  }
  add(out, &n, max, QRC_TRAKT, "", "Trakt", 0, trakt_ativo() ? QRC_E_LIGADO : QRC_E_NAO, QRC_P_ABRIR,
      trakt_ativo() ? "Conectado neste perfil" : "Não conectado");
  add(out, &n, max, QRC_SIMKL, "", "Simkl", 0, simkl_ativo() ? QRC_E_LIGADO : QRC_E_NAO, QRC_P_ABRIR,
      simkl_ativo() ? "Conectado neste perfil" : "Não conectado");
  add(out, &n, max, QRC_YOUTUBE, "", "YouTube", 0, !yt_tem_chave() ? QRC_E_NAO : yt_logado() ? QRC_E_LIGADO : QRC_E_CONFIG,
      QRC_P_ABRIR | (yt_tem_chave() ? QRC_P_TESTAR : 0),
      !yt_tem_chave() ? "Sem chave da API" : yt_logado() ? "Chave e conta conectadas" : "Chave da API (conta não conectada)");
  add(out, &n, max, QRC_LASTFM, "", "Last.fm", 0, !mclfm_tem_chave() ? QRC_E_NAO : mclfm_conectado() ? QRC_E_LIGADO : QRC_E_CONFIG,
      QRC_P_ABRIR, !mclfm_tem_chave() ? "Sem chave da API" : mclfm_conectado() ? "Conectado neste perfil" : "Chave da API (conta não conectada)");
  add(out, &n, max, QRC_SPOTIFY, "", "Spotify", 0, mcsp_tem_chave() ? QRC_E_CONFIG : QRC_E_NAO, QRC_P_ABRIR,
      mcsp_tem_chave() ? "Só metadados públicos (playlists)" : "Sem chave da API");
  {
    const char *k = desc_chave_tmdb();
    add(out, &n, max, QRC_TMDB, "", "TMDB", 0, k && k[0] ? QRC_E_CONFIG : QRC_E_NAO, QRC_P_ABRIR,
        k && k[0] ? "Metadados, elenco e datas" : "Sem chave: elenco só com nomes");
  }
  add(out, &n, max, QRC_TSDB, "", "TheSportsDB", 0, QRC_E_CONFIG, QRC_P_CHAVE,
      tsdb_chave_pessoal() ? "Chave pessoal" : "Chave pública gratuita (limitada)");
  if (jellyfin_servidor_curto()[0]) {
    snprintf(b, sizeof b, "%s", jellyfin_servidor_curto());
    add(out, &n, max, QRC_JELLYFIN, "", "Jellyfin / Emby", 0, jellyfin_conectado() ? QRC_E_LIGADO : QRC_E_CONFIG, QRC_P_ABRIR, b);
  }
  if (plex_servidor_nome()[0]) {
    snprintf(b, sizeof b, "%s", plex_servidor_nome());
    add(out, &n, max, QRC_PLEX, "", "Plex", 0, plex_conectado() ? QRC_E_LIGADO : QRC_E_CONFIG, QRC_P_ABRIR, b);
  }
  return n;
}

// ---------------------------------------------------------------- operations
typedef struct { int tipo; char ref[24]; } Pedido;
static const char *msgMusica(int r) {
  return r == MC_OK ? "Conexão OK" : r == MC_ERRO_LOGIN ? "Usuário ou senha recusados" : r == MC_ERRO_REDE ? "Sem resposta do servidor"
       : r == MC_ERRO_NAO_SUPORTA ? "O servidor não oferece esta função" : "O servidor respondeu com erro";
}
static const char *msgYt(int r) {
  return r == YT_OK ? "Conexão OK" : r == YT_ERRO_CHAVE ? "Chave recusada" : r == YT_ERRO_COTA ? "Cota diária esgotada"
       : r == YT_ERRO_REDE ? "Sem resposta do servidor" : "O servidor respondeu com erro";
}
static void *fioTestar(void *u) {
  Pedido p = *(Pedido *)u;
  free(u);
  if (p.tipo == QRC_MUSICA) {
    McFonte f;
    char desc[120];
    int r = MC_ERRO_OUTRO;
    if (mcfontes_achar(p.ref, &f)) {
      if (f.tipo == MCS_SUBSONIC) r = mccat_testar(f.tipo, f.ss.url, f.ss.usuario, f.ss.senha, desc, sizeof desc);
      else r = mccat_testar(f.tipo, f.dv.url, f.dv.usuario, f.dv.senha, desc, sizeof desc);
    }
    anotar(p.tipo, p.ref, r == MC_OK ? QRC_E_LIGADO : QRC_E_ERRO, msgMusica(r));
    printf("[contas] test music source %s -> %s\n", p.ref, r == MC_OK ? "ok" : "failed");
  } else if (p.tipo == QRC_YOUTUBE) {
    YtItem *v = calloc(2, sizeof *v);
    char prox[64];
    int n = 0, r = v ? yt_em_alta("US", "", v, 1, &n, prox, sizeof prox) : YT_ERRO_OUTRO;
    free(v);
    anotar(p.tipo, p.ref, r == YT_OK ? QRC_E_LIGADO : QRC_E_ERRO, msgYt(r));
    printf("[contas] test YouTube key -> %s\n", r == YT_OK ? "ok" : "failed");
  }
  fflush(stdout);
  pthread_mutex_lock(&trava); testando = 0; pthread_mutex_unlock(&trava);
  return NULL;
}
int qrc_testar(const QrcLinha *l) {
  pthread_t t;
  Pedido *p;
  if (!l || !(l->pode & QRC_P_TESTAR)) return 0;
  if (l->tipo == QRC_IPTV) {
    IptvConta c[16];
    int k = iptv_contas(c, 16);
    unsigned id = (unsigned)strtoul(l->ref, NULL, 10);
    for (int i = 0; i < k; i++) if (c[i].id == id) return iptvteste_iniciar(id, c[i].tipo);
    return 0;
  }
  if (l->tipo != QRC_MUSICA && l->tipo != QRC_YOUTUBE) return 0;
  pthread_mutex_lock(&trava);
  if (testando) { pthread_mutex_unlock(&trava); return 0; }
  testando = 1;
  { Resultado *r = achar(l->tipo, l->ref, 1); r->estado = QRC_E_TESTANDO; snprintf(r->msg, sizeof r->msg, "Testando…"); }
  pthread_mutex_unlock(&trava);
  if (!(p = calloc(1, sizeof *p))) { pthread_mutex_lock(&trava); testando = 0; pthread_mutex_unlock(&trava); return 0; }
  p->tipo = l->tipo; snprintf(p->ref, sizeof p->ref, "%s", l->ref);
  if (pthread_create(&t, NULL, fioTestar, p) != 0) { free(p); pthread_mutex_lock(&trava); testando = 0; pthread_mutex_unlock(&trava); return 0; }
  pthread_detach(t);
  return 1;
}
int qrc_teste_esperar(int ms) {
  for (int i = 0; i < ms / 10; i++) {
    int v;
    pthread_mutex_lock(&trava); v = testando; pthread_mutex_unlock(&trava);
    if (!v) return 1;
    usleep(10000);
  }
  return 0;
}
int qrc_alternar(const QrcLinha *l) {
  if (!l || !(l->pode & QRC_P_ALTERNAR)) return 0;
  if (l->tipo == QRC_IPTV) {
    int ok = iptv_conta_ativar_id((unsigned)strtoul(l->ref, NULL, 10), l->estado == QRC_E_DESLIGADO);
    if (ok) guia_recarregar(0);   // the guide drops / brings back that account's channels
    return ok;
  }
  if (l->tipo == QRC_ADDONS) { int i = atoi(l->ref); if (i < 0 || i >= addons_n()) return 0; addons_alternar(i); return 1; }
  if (l->tipo == QRC_MUSICA) return mcfontes_ligar(l->ref, l->estado == QRC_E_DESLIGADO);
  return 0;
}
int qrc_atualizar(const QrcLinha *l) {
  if (!l || !(l->pode & QRC_P_ATUALIZAR)) return 0;
  if (l->tipo == QRC_IPTV) {
    xtream_fontes_invalidar(); m3u_fontes_invalidar(); stalker_fontes_invalidar();
    guia_recarregar(0);   // keeps the current channels on screen while it reloads
    return 1;
  }
  if (l->tipo == QRC_ADDONS) { addons_sondar_manifestos(); return 1; }
  return 0;
}

// ---------------------------------------------------------------- TheSportsDB key
static int chaveLimpa(const char *k) {
  if (strlen(k) > 40) return 0;
  for (; *k; k++) if (!((*k >= '0' && *k <= '9') || (*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') || *k == '-' || *k == '_')) return 0;
  return 1;
}
int qrc_tsdb_chave(const char *k) {
  if (!k || !k[0]) { dados_apagar("tsdb.txt"); tsdb_definir_chave(""); return 1; }
  if (!chaveLimpa(k)) return 0;   // the key goes into a URL path: letters, digits, - and _ only
  if (!dados_gravar("tsdb.txt", k)) return 0;
  tsdb_definir_chave(k);
  return 1;
}
void qrc_tsdb_carregar(void) {
  char *t = dados_ler("tsdb.txt");
  if (t) { t[strcspn(t, "\r\n")] = 0; if (chaveLimpa(t)) tsdb_definir_chave(t); free(t); }
}
