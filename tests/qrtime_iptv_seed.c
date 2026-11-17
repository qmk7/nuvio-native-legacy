// Creates the three IPTV test accounts of tests/qrtime_livetv.sh in a data
// folder, through the app's own account code (iptvfontes/xtream/m3u/stalker)
// and the real storage (dados.c), so the files are exactly what the app writes
// when a person fills the Settings forms.
//
//   qrtime_iptv_seed <data dir> <base url>     e.g. http://127.0.0.1:8123
//   qrtime_iptv_seed <data dir> <base url> badpass   one Xtream account with
//                                                    a WRONG password only
//
// Optional, through the environment (tests/qrtime_catlock.sh):
//   QRT_SEED_PIN=1234             this TV's administrator PIN (admin.c)
//   QRT_SEED_LOCK="1:1:QRT Sports" lock that category of IPTV type 1, account 1
//                                 (key: the guide's categoriaChaveFonte)
//
// No network is used: seeding only stores settings. The network stubs below
// fail loudly if any code path tries to fetch.
#include "xtream.h"
#include "m3u.h"
#include "stalker.h"
#include "iptvfontes.h"
#include "dados.h"
#include "rede.h"
#include "admin.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t perfis_sessao_geracao(void) { return 1; }
int perfis_sessao_atual(uint64_t s) { return s == 1; }
int perfis_sessao_travar(uint64_t s) { return s == 1; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return 1; }
int ajustes_livetv_formato(void) { return 0; }

static void sem_rede(const char *f) {
  fprintf(stderr, "seed: unexpected network call %s\n", f);
  exit(3);
}
char *rede_baixar_st_retry(const char *u, int s, const char *const *h, int *st, int *r) {
  (void)u; (void)s; (void)h; (void)st; (void)r; sem_rede("rede_baixar_st_retry"); return NULL;
}
char *rede_baixar_bin_medido_controle(const char *u, int s, const char *const *h,
                                      const RedeControle *c, long *n, RedeMedida *m) {
  (void)u; (void)s; (void)h; (void)c; (void)n; (void)m; sem_rede("rede_baixar_bin_medido_controle"); return NULL;
}
char *rede_baixar_com(const char *u, int s, const char *const *h) {
  (void)u; (void)s; (void)h; sem_rede("rede_baixar_com"); return NULL;
}

static unsigned nova(int tipo, const char *nome) {
  IptvConta c;
  if (!iptv_conta_adicionar(tipo) || !iptv_conta_selecionada(&c) || !iptv_conta_renomear(nome)) {
    fprintf(stderr, "seed: could not add account %s\n", nome);
    exit(1);
  }
  return c.id;
}

// Same identity as guia_categorias.inc categoriaChaveFonte() for an IPTV
// account category: FNV-1a over "tipo:id:", "" and the name, each ended by 0.
static uint64_t chaveCategoria(const char *origem, const char *nome) {
  const char *partes[] = { origem, "", nome };
  uint64_t h = 1469598103934665603ULL;
  for (int i = 0; i < 3; i++) {
    for (const unsigned char *p = (const unsigned char *)partes[i]; *p; p++) h = (h ^ *p) * 1099511628211ULL;
    h = (h ^ 0) * 1099511628211ULL;
  }
  return h ? h : 1;
}
static void extras(void) {
  const char *pin = getenv("QRT_SEED_PIN"), *trava = getenv("QRT_SEED_LOCK");
  if (pin && *pin && !admin_pin_definir(pin)) { fprintf(stderr, "seed: admin pin\n"); exit(1); }
  if (trava && *trava) {
    char origem[32], linha[32];
    const char *nome = strchr(trava, ':');
    nome = nome ? strchr(nome + 1, ':') : NULL;
    if (!nome || nome - trava >= (long)sizeof origem - 1) { fprintf(stderr, "seed: lock\n"); exit(1); }
    memcpy(origem, trava, (size_t)(nome - trava + 1)); origem[nome - trava + 1] = 0;
    snprintf(linha, sizeof linha, "%016llx\n", (unsigned long long)chaveCategoria(origem, nome + 1));
    if (!dados_gravar("guia-categorias-bloq-p1.txt", linha)) { fprintf(stderr, "seed: lock file\n"); exit(1); }
  }
}

int main(int argc, char **argv) {
  char lista[300], epg[300];
  if (argc != 3 && argc != 4) { fprintf(stderr, "usage: %s <data dir> <base url> [badpass]\n", argv[0]); return 2; }
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(NULL);
  if (argc == 4 && !strcmp(argv[3], "badpass")) {
    unsigned b = nova(IPTV_XTREAM, "QRT Xtream wrong password");
    xtream_fonte_selecionar(b);
    if (!xtream_conta_definir(argv[2], "qrtuser", "wrongpass")) { fprintf(stderr, "seed: xtream\n"); return 1; }
    printf("seed: xtream(bad)=%u accounts=%d\n", b, iptv_contas(NULL, 0));
    return 0;
  }

  unsigned x = nova(IPTV_XTREAM, "QRT Xtream");
  xtream_fonte_selecionar(x);
  if (!xtream_conta_definir(argv[2], "qrtuser", "qrtpass")) { fprintf(stderr, "seed: xtream\n"); return 1; }

  unsigned m = nova(IPTV_M3U, "QRT M3U");
  m3u_fonte_selecionar(m);
  snprintf(lista, sizeof lista, "%s/list.m3u", argv[2]);
  snprintf(epg, sizeof epg, "%s/epg.xml", argv[2]);
  if (!m3u_conta_definir(lista, epg)) { fprintf(stderr, "seed: m3u\n"); return 1; }

  unsigned s = nova(IPTV_STALKER, "QRT Stalker");
  stalker_fonte_selecionar(s);
  if (!stalker_conta_definir(argv[2], "00:1A:79:00:00:42")) { fprintf(stderr, "seed: stalker\n"); return 1; }

  extras();
  printf("seed: xtream=%u m3u=%u stalker=%u accounts=%d\n", x, m, s, iptv_contas(NULL, 0));
  return 0;
}
