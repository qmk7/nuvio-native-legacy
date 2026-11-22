// QR TIME LG LAUNCHER SHORTCUTS: Settings > Esta TV > "Atalhos no menu da LG"
// asks webOS (com.webos.applicationManager/addLaunchPoint) for three extra
// tiles in the launcher: Sport Center, Music and Live TV. Each one launches
// QR TIME with {"qrtime":"esportes"|"musica"|"aovivo"}, read from argv[1]
// at start, and the app goes there once the Home is up.
// UNVERIFIED ON THE B5: webOS may refuse addLaunchPoint to a developer-mode
// app; the log says "[atalhos] ... aceito/recusado". A launch while the app
// is already open (relaunch) is not handled yet: it opens the app as it was.
#include "atalhoslg.h"
#include "menu.h"
#include "idioma.h"
#include "video.h"
#include <stdio.h>
#include <string.h>

#if !defined(__APPLE__) && !defined(NV_LINUX_DESKTOP) && !defined(NV_TPK) && !defined(NV_ANDROID) && !defined(__EMSCRIPTEN__)
#define AT_WEBOS 1
#endif

static int pedido = -1;
static char dirArt[512] = ".";
void atalhoslg_dir(const char *d) { if (d) snprintf(dirArt, sizeof dirArt, "%s", d); }
static const struct { const char *chave, *titulo; int menu; } A[] = {
  { "esportes", "Centro de Esportes", MENU_ESPORTES },
  { "musica", "Música", MENU_MUSICA },
  { "aovivo", "TV ao vivo", MENU_GUIA },
};

void atalhoslg_lancamento(const char *json) {
  const char *p;
  if (!json || !(p = strstr(json, "\"qrtime\""))) return;
  for (size_t i = 0; i < sizeof A / sizeof *A; i++) {
    char k[48];
    snprintf(k, sizeof k, "\"%s\"", A[i].chave);
    if (strstr(p, k)) { pedido = A[i].menu; printf("[atalhos] lancado pelo atalho %s\n", A[i].chave); fflush(stdout); return; }
  }
}
int atalhoslg_destino(void) { int d = pedido; pedido = -1; return d; }

#ifdef AT_WEBOS
static void resp(const char *payload, void *u) {
  printf("[atalhos] %s: %s\n", (const char *)u, payload && strstr(payload, "\"returnValue\":true") ? "aceito" : "recusado");
  fflush(stdout);
}
#endif
int atalhoslg_criar(void) {
  const char *dirArte = dirArt;
#ifdef AT_WEBOS
  char icone[600], carga[900];
  // The app's own icon (deploy/app/icon-20.png), next to the art folder.
  snprintf(icone, sizeof icone, "%s/../icon-20.png", dirArte ? dirArte : ".");
  for (size_t i = 0; i < sizeof A / sizeof *A; i++) {
    snprintf(carga, sizeof carga, "{\"id\":\"com.qrtime.tv\",\"title\":\"QR TIME \u00b7 %s\",\"icon\":\"%s\",\"params\":{\"qrtime\":\"%s\"}}",
             i18n(A[i].titulo), icone, A[i].chave);
    video_luna("luna://com.webos.service.applicationManager/addLaunchPoint", carga, resp, (void *)A[i].chave);
  }
  return 1;
#else
  (void)dirArte;
  puts("[atalhos] so no webOS");
  fflush(stdout);
  return 0;
#endif
}
