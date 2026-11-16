#include "convidado.h"
#include "dados.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARQ_CONVIDADO "convidado.txt"

// -1 = not read yet; 0/1 = cached. Read on the drawing thread at start-up and
// changed only by explicit user actions, but kept atomic because settings and
// the router may ask from other threads.
static atomic_int estado = ATOMIC_VAR_INIT(-1);

int convidado_ativo(void) {
  int v = atomic_load(&estado);
  if (v < 0) {
    char *b = dados_ler(ARQ_CONVIDADO);
    v = b && !strncmp(b, "1", 1);
    free(b);
    atomic_store(&estado, v);
  }
  return v;
}

int convidado_entrar(void) {
  if (!dados_gravar(ARQ_CONVIDADO, "1\n"))
    printf("[convidado] could not save guest mode; active for this run only\n");
  atomic_store(&estado, 1);
  return 1;
}

void convidado_sair(void) {
  dados_apagar(ARQ_CONVIDADO);
  atomic_store(&estado, 0);
}

int convidado_deve_entrar(int sessaoLogada, int servidorPronto, int escolheuConvidado) {
  if (sessaoLogada) return 0;
  return !servidorPronto || escolheuConvidado;
}

void convidado_teste_reiniciar(void) { atomic_store(&estado, -1); }
