// QR TIME party mode (src/mcfesta.c): guest search, request, host approval
// into the queue, decline, request cap, votes to skip.
//   bash tests/qrtime_festa.sh
#include "mcfesta.h"
#include "mcfila.h"
#include "dados.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static int stub(const char *q, McFaixa *out, int max) {
  int n = 3 < max ? 3 : max;
  for (int i = 0; i < n; i++) {
    memset(&out[i], 0, sizeof out[i]);
    snprintf(out[i].id, sizeof out[i].id, "ss0000000%d:%d", i, i);
    snprintf(out[i].titulo, sizeof out[i].titulo, "%s %d", q, i);
    snprintf(out[i].artista, sizeof out[i].artista, "Artist");
  }
  return n;
}
static void req(const char *rota, const char *corpo, const char *cli) {
  LwPedido p;
  memset(&p, 0, sizeof p);
  snprintf(p.app, sizeof p.app, "festa"); snprintf(p.rota, sizeof p.rota, "%s", rota);
  snprintf(p.cliente, sizeof p.cliente, "%s", cli);
  p.corpo = strdup(corpo); p.n = strlen(corpo);
  mcfesta_pedido(&p);
  free(p.corpo);
}
int main(void) {
  dados_iniciar(getenv("NUVIO_DADOS"));
  perfis_definir_ativo(1);
  mcq_limpar();
  mcfesta_teste_busca(stub);
  req("buscar", "g=abc123&nome=Ana&q=Fairuz", "192.168.1.20");
  usleep(300000);
  req("pedir", "g=abc123&nome=Ana&i=1", "192.168.1.20");
  CONFERE(mcfesta_teste_pedidos() == 1, "a guest's request arrives");
  CONFERE(mcfesta_decidir(1, 1) && mcq_n() == 1 && !strcmp(mcq_item(0)->titulo, "Fairuz 1"), "approved: goes to the queue");
  CONFERE(!mcfesta_decidir(1, 0), "decided once only");
  req("pedir", "g=abc123&i=2", "192.168.1.20");
  CONFERE(mcfesta_decidir(2, 0) && mcq_n() == 1, "declined: queue unchanged");
  req("pedir", "g=abc123&i=9", "192.168.1.20");
  req("pedir", "g=zzz&i=0", "192.168.1.21");
  CONFERE(mcfesta_teste_pedidos() == 2, "out-of-range index and a guest without results: ignored");
  for (int i = 0; i < 12; i++) req("pedir", "g=abc123&i=0", "192.168.1.20");
  CONFERE(mcfesta_teste_pedidos() == 2 + 8, "at most 8 waiting requests (no flooding the TV)");
  CONFERE(mcfesta_votos_precisa() == 2, "two phones took part: 2 votes skip");
  req("buscar", "g=b1&q=x", "192.168.1.22"); req("buscar", "g=b2&q=x", "192.168.1.23"); req("buscar", "g=b3&q=x", "192.168.1.24");
  usleep(300000);
  CONFERE(mcfesta_votos_precisa() == 3, "five phones: 3 votes skip");
  req("buscar", "g=b4&q=x", "192.168.1.24");
  usleep(300000);
  CONFERE(mcfesta_votos_precisa() == 3, "a second guest id from the same phone does not count twice");
  return falhas ? 1 : 0;
}
