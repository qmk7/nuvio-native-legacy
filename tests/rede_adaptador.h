// Offline adapter for tests written before provedorhttp.h used RedePedido:
// rede_pedir reports "unavailable" so provedorGet falls back to the test's
// own rede_baixar_st. Weak, so a test that defines these keeps its own.
// Use with: cc ... -include tests/rede_adaptador.h
#ifndef NV_TESTE_REDE_ADAPTADOR_H
#define NV_TESTE_REDE_ADAPTADOR_H
#include "rede.h"
#include <stdlib.h>
#include <string.h>
__attribute__((weak)) int rede_pedir(const RedePedido *p, RedeResposta *r) {
  (void)p; memset(r, 0, sizeof *r); r->erro = REDE_INDISPONIVEL; return 0;
}
__attribute__((weak)) void rede_resposta_limpar(RedeResposta *r) {
  free(r->corpo); free(r->cabecalhos); memset(r, 0, sizeof *r);
}
__attribute__((weak)) char *rede_baixar_st_retry(const char *u, int s, const char *const *h, int *st, int *retry) {
  if (retry) *retry = 0;
  return rede_baixar_st(u, s, h, st);
}
#endif
