// QR TIME WEEKLY SUMMARY: each finished player session leaves one line in
// semana-p<N>.txt ("<day>\t<imdb>\t<type>\t<seconds>\t<title>\t<genre>",
// 60 days kept, this TV only, never sent anywhere). On Friday from 18:00
// (the start of the Saudi weekend), once a week, a card sums up the last
// 7 days: time watched, films and series, the most watched title and genre.
#include "semana.h"
#include "ajustes.h"
#include "dados.h"
#include "idioma.h"
#include "ilha.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void arq(char *d, size_t n) { snprintf(d, n, "semana-p%d.txt", perfis_ativo()); }
static void limpo(char *d, size_t n, const char *s) {
  size_t k = 0;
  for (; s && *s && k + 1 < n; s++) d[k++] = (*s == '\t' || *s == '\n' || *s == '\r') ? ' ' : *s;
  d[k] = 0;
}
void semana_registrar(const char *imdb, const char *tipo, const char *titulo, const char *genero, double seg, time_t quando) {
  char n[40], dia[12], l[400], t[160], g[80], *velho, *novo;
  struct tm lt;
  size_t nv;
  if (!imdb || !imdb[0] || seg < 60 || (strcmp(tipo, "movie") && strcmp(tipo, "series"))) return;
  localtime_r(&quando, &lt);
  strftime(dia, sizeof dia, "%Y-%m-%d", &lt);
  limpo(t, sizeof t, titulo); limpo(g, sizeof g, genero);
  snprintf(l, sizeof l, "%s\t%s\t%s\t%.0f\t%s\t%s\n", dia, imdb, tipo, seg, t, g);
  arq(n, sizeof n);
  velho = dados_ler(n);
  // Keep 60 days: drop lines older than that (the file is in date order).
  {
    time_t lim = quando - 60L * 86400;
    struct tm lm; char dl[12];
    localtime_r(&lim, &lm); strftime(dl, sizeof dl, "%Y-%m-%d", &lm);
    while (velho && velho[0] && strncmp(velho, dl, 10) < 0) {
      char *nl = strchr(velho, '\n');
      if (!nl) { velho[0] = 0; break; }
      memmove(velho, nl + 1, strlen(nl + 1) + 1);
    }
  }
  nv = (velho ? strlen(velho) : 0) + strlen(l) + 1;
  if ((novo = malloc(nv))) { snprintf(novo, nv, "%s%s", velho ? velho : "", l); dados_gravar(n, novo); free(novo); }
  free(velho);
}

int semana_resumo(time_t agora, SemResumo *r) {
  char n[40], de[12], *t, *p;
  time_t ini = agora - 7L * 86400;
  struct tm lt;
  struct { char imdb[24], titulo[160]; double seg; } tit[64];
  struct { char g[80]; double seg; } gen[32];
  int nt = 0, ng = 0;
  memset(r, 0, sizeof *r);
  localtime_r(&ini, &lt);
  strftime(de, sizeof de, "%Y-%m-%d", &lt);
  arq(n, sizeof n);
  if (!(t = dados_ler(n))) return 0;
  for (p = t; p && *p; ) {
    char *nl = strchr(p, '\n'), *c[6];
    int k = 0;
    if (nl) *nl = 0;
    c[k++] = p;
    for (char *q = p; *q && k < 6; q++) if (*q == '\t') { *q = 0; c[k++] = q + 1; }
    if (k == 6 && strncmp(c[0], de, 10) >= 0) {
      double s = atof(c[3]);
      int i;
      r->segundos += s;
      for (i = 0; i < nt && strcmp(tit[i].imdb, c[1]); i++) {}
      if (i == nt && nt < 64) {
        snprintf(tit[nt].imdb, sizeof tit[0].imdb, "%s", c[1]); snprintf(tit[nt].titulo, sizeof tit[0].titulo, "%s", c[4]);
        tit[nt].seg = 0; nt++;
        if (!strcmp(c[2], "movie")) r->filmes++; else r->series++;
      }
      if (i < 64) tit[i].seg += s;
      {
        char g[80], *sep;
        snprintf(g, sizeof g, "%s", c[5]);
        // "Programa de TV · Drama · Mistério": the first genre that is not the kind.
        sep = strstr(g, " · ");
        if (sep && (!strncmp(g, "Programa", 8) || !strncmp(g, "Filme", 5) || !strncmp(g, "TV", 2))) memmove(g, sep + 4, strlen(sep + 4) + 1);
        if ((sep = strstr(g, " · "))) *sep = 0;
        if (g[0]) {
          int j;
          for (j = 0; j < ng && strcmp(gen[j].g, g); j++) {}
          if (j == ng && ng < 32) { snprintf(gen[ng].g, sizeof gen[0].g, "%s", g); gen[ng].seg = 0; ng++; }
          if (j < 32) gen[j].seg += s;
        }
      }
    }
    p = nl ? nl + 1 : NULL;
  }
  free(t);
  for (int i = 0, b = -1; i < nt; i++) if (b < 0 || tit[i].seg > tit[b].seg) { b = i; snprintf(r->topo, sizeof r->topo, "%s", tit[i].titulo); }
  for (int j = 0, b = -1; j < ng; j++) if (b < 0 || gen[j].seg > gen[b].seg) { b = j; snprintf(r->genero, sizeof r->genero, "%s", gen[j].g); }
  return r->segundos >= 60;
}

void semana_passo(Uint32 agora) {
  static Uint32 ultimo;
  static int feito;
  time_t t = time(NULL);
  struct tm lt;
  char n[40], sem[16], *v;
  SemResumo r;
  if (feito || agora - ultimo < 30000u || !ajustes_bom_dia()) return;
  ultimo = agora;
  localtime_r(&t, &lt);
  if (lt.tm_wday != 5 || lt.tm_hour < 18) return;   // Friday evening
  strftime(sem, sizeof sem, "%G-W%V", &lt);
  snprintf(n, sizeof n, "semana-vista-p%d.txt", perfis_ativo());
  if ((v = dados_ler(n))) { int ja = !strncmp(v, sem, strlen(sem)); free(v); if (ja) { feito = 1; return; } }
  feito = 1;
  dados_gravar(n, sem);
  if (!semana_resumo(t, &r)) return;
  {
    IlhaModal m;
    IlhaAvisoEx a;
    int h = (int)(r.segundos / 3600), mi = (int)(r.segundos / 60) % 60;
    memset(&m, 0, sizeof m); memset(&a, 0, sizeof a);
    snprintf(m.kicker, sizeof m.kicker, "%s", i18n("Sua semana"));
    snprintf(m.titulo, sizeof m.titulo, i18n("%d h %d min assistidos"), h, mi);
    snprintf(m.linha, sizeof m.linha, i18n("%d filme(s) · %d série(s)"), r.filmes, r.series);
    if (r.topo[0]) snprintf(m.lista[0], sizeof m.lista[0], "%s: %s", i18n("Mais assistido"), r.topo);
    if (r.genero[0]) snprintf(m.lista[1], sizeof m.lista[1], "%s: %s", i18n("Gênero da semana"), i18n(r.genero));
    snprintf(m.icone, sizeof m.icone, "aj_chart-column");
    m.nBotoes = 1;
    snprintf(m.botao[0], sizeof m.botao[0], "%s", i18n("Fechar"));
    a.chave = "semana"; a.tipo = ILHA_ACENTO; a.icone = "aj_chart-column"; a.texto = m.titulo; a.ms = 15000u; a.modal = &m; a.tecla = 1;
    ilha_avisar_ex(&a);
    printf("[semana] resumo mostrado\n");
    fflush(stdout);
  }
}

time_t semana_ultimo(const char *imdb) {
  char n[40], *t, *p, ult[12] = "";
  size_t l = imdb ? strlen(imdb) : 0;
  if (!l) return 0;
  arq(n, sizeof n);
  if (!(t = dados_ler(n))) return 0;
  for (p = t; p && *p; ) {
    if (strlen(p) > 11 && p[10] == '\t' && !strncmp(p + 11, imdb, l) && p[11 + l] == '\t' && strncmp(p, ult, 10) > 0)
      snprintf(ult, sizeof ult, "%.10s", p);
    p = strchr(p, '\n'); if (p) p++;
  }
  free(t);
  if (!ult[0]) return 0;
  { struct tm lt = { 0 }; lt.tm_isdst = -1;
    if (sscanf(ult, "%d-%d-%d", &lt.tm_year, &lt.tm_mon, &lt.tm_mday) != 3) return 0;
    lt.tm_year -= 1900; lt.tm_mon -= 1; lt.tm_hour = 12;
    return mktime(&lt); }
}
