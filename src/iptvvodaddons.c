// Hooks the IPTV library (iptvvod.c) into addons.c as one more source of
// streams for catalogue titles (a separate file so iptvvod.c compiles and is
// tested without the addons module, like plexaddons.c).
#include "addons.h"
#include "iptvvod.h"

static int origem(const char *id, const char *tipo, int (*cancelado)(void *), void *ctx,
                  OrigemAviso aviso, void *avisoU, void *saida) {
  (void)aviso; (void)avisoU;   // one quiet lookup, no per-source progress rows
  return iptvvod_origem(id, tipo, cancelado, ctx, NULL, NULL, saida);
}
void iptvvod_ligar_aos_addons(void) {
  addons_definir_origem_extra(origem, iptvvod_origem_ativa);
}
