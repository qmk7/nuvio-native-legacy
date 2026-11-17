// "Test connection" (src/iptvteste.c) against the local IPTV test providers of
// tests/qrtime_iptv_mock.py, through the real protocol code and real HTTP.
// Accounts come from tests/qrtime_iptv_seed.c (good and wrong password).
//
//   qrtime_iptv_teste <data dir> expect-ok | expect-refused
#include "iptvteste.h"
#include "iptvfontes.h"
#include "dados.h"
#include "idioma.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint64_t perfis_sessao_geracao(void) { return 1; }
int perfis_sessao_atual(uint64_t s) { return s == 1; }
int perfis_sessao_travar(uint64_t s) { return s == 1; }
void perfis_sessao_destravar(void) {}
int perfis_ativo(void) { return 1; }
int ajustes_livetv_formato(void) { return 0; }
const char *i18n(const char *s) { return s; }   // source strings (pt) are the keys

static int falhas;
static void confere(int cond, const char *o, const char *m) {
  printf("%s: %s (%s)\n", cond ? "PASS" : "FAIL", o, m);
  if (!cond) falhas++;
}

static int testar(unsigned id, int tipo, char *msg, unsigned n) {
  int e = IPTVT_NADA;
  if (!iptvteste_iniciar(id, tipo)) return -1;
  for (int i = 0; i < 300; i++) {             // up to 30 s
    e = iptvteste_estado(id, msg, n);
    if (e == IPTVT_OK || e == IPTVT_FALHOU) return e;
    usleep(100000);
  }
  return e;
}

int main(int argc, char **argv) {
  IptvConta contas[8];
  char msg[200], quem[64];
  int n;
  if (argc != 3) { fprintf(stderr, "usage: %s <data dir> expect-ok|expect-refused\n", argv[0]); return 2; }
  setenv("NUVIO_DADOS", argv[1], 1);
  dados_iniciar(NULL);
  n = iptv_contas(contas, 8);
  if (n <= 0) { fprintf(stderr, "no accounts\n"); return 2; }

  // Pure text checks (no network).
  {
    XtreamConta c = {0};
    iptvteste_texto_xtream(0, 0, NULL, 0, msg, sizeof msg);
    confere(!strcmp(msg, "sem resposta do servidor"), "no answer", msg);
    iptvteste_texto_xtream(0, 503, NULL, 0, msg, sizeof msg);
    confere(!strcmp(msg, "erro HTTP 503 do servidor"), "HTTP error", msg);
    c.valido = 1; c.auth = 1; snprintf(c.status, sizeof c.status, "Expired");
    confere(!iptvteste_texto_xtream(1, 200, &c, 1000, msg, sizeof msg) && !strcmp(msg, "vencida"), "expired", msg);
    snprintf(c.status, sizeof c.status, "Active"); c.maxConexoes = 2; c.conexoes = 1;
    confere(iptvteste_texto_xtream(1, 200, &c, 1000, msg, sizeof msg) && !strcmp(msg, "ativa · 1 de 2 telas"), "active, screens", msg);
    confere(!iptvteste_texto_lista(0, msg, sizeof msg) && !strcmp(msg, "nenhum canal encontrado"), "empty list", msg);
  }

  for (int i = 0; i < n; i++) {
    int e = testar(contas[i].id, contas[i].tipo, msg, sizeof msg);
    snprintf(quem, sizeof quem, "account %u type %d", contas[i].id, contas[i].tipo);
    if (!strcmp(argv[2], "expect-refused")) {
      confere(e == IPTVT_FALHOU && !strcmp(msg, "recusada pelo servidor"), quem, msg);
    } else if (contas[i].tipo == IPTV_XTREAM) {
      confere(e == IPTVT_OK && !strncmp(msg, "ativa", 5), quem, msg);
    } else {
      confere(e == IPTVT_OK && strstr(msg, "conectado · 2 canais"), quem, msg);
    }
    confere(!strstr(msg, "qrtpass") && !strstr(msg, "wrongpass"), "no password in the result", msg);
  }
  // Another account's result is not reported for this one.
  confere(iptvteste_estado(contas[0].id + 999, NULL, 0) == IPTVT_NADA, "other account", "NADA");
  iptvteste_esquecer();
  confere(iptvteste_estado(contas[0].id, NULL, 0) == IPTVT_NADA, "forget", "NADA");
  return falhas ? 1 : 0;
}
