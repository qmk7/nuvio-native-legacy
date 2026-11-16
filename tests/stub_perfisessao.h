#ifndef NV_TEST_STUB_PERFISESSAO_H
#define NV_TEST_STUB_PERFISESSAO_H
#include "perfis.h"
#define NV_PERFIL_SESSAO_FORTE

// Isolated fixtures keep one profile session throughout each process.
// Weak, so a fixture made of several files that each include this header links
// once (and a real perfis.c, when linked, still wins).
__attribute__((weak)) uint64_t perfis_sessao_geracao(void) { return 0; }
__attribute__((weak)) int perfis_sessao_atual(uint64_t geracao) { return geracao == 0; }
__attribute__((weak)) int perfis_sessao_travar(uint64_t geracao) { return perfis_sessao_atual(geracao); }
__attribute__((weak)) void perfis_sessao_destravar(void) {}
#endif
