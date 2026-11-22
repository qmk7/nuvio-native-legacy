// QR TIME MORNING GREETING: the first time the app is opened between 05:00 and
// 12:00, once a day per profile (Settings > Aparência > "Bom dia"):
//   - the weather for the prayer-times city (Open-Meteo: free, no key, only
//     the city's coordinates are sent); no city = no weather line;
//   - today's reminders (followed teams' matches, programmes);
//   - series with a new episode today (Agenda).
// It never waits for the network: after 6 s without an answer it goes out
// without the weather.
#include "bomdia.h"
#include "agenda.h"
#include "ajustes.h"
#include "dados.h"
#include "horafmt.h"
#include "idioma.h"
#include "ilha.h"
#include "js.h"
#include "lembrete.h"
#include "perfis.h"
#include "rede.h"
#include "salat.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t trava = PTHREAD_MUTEX_INITIALIZER;
static int fio, temClima;
static BdClima clima;
static Uint32 desde;

int bomdia_parse(const char *json, BdClima *c) {
  const char *cur, *d, *a;
  memset(c, 0, sizeof *c);
  if (!json || !(cur = strstr(json, "\"current\""))) return 0;
  if (!(cur = strchr(cur, '{'))) return 0;
  c->temp = js_num(cur, js_fim(cur), "temperature_2m", -999);
  c->codigo = (int)js_num(cur, js_fim(cur), "weather_code", -1);
  if (c->temp < -100 || c->codigo < 0) return 0;
  c->max = c->min = -999;
  // The daily arrays hold numbers ("temperature_2m_max":[29.8]): the first one.
  if ((d = strstr(json, "\"daily\""))) {
    if ((a = strstr(d, "\"temperature_2m_max\":[")) && a[22] != ']') c->max = strtod(a + 22, NULL);
    if ((a = strstr(d, "\"temperature_2m_min\":[")) && a[22] != ']') c->min = strtod(a + 22, NULL);
  }
  return 1;
}
// WMO weather codes, as Open-Meteo documents them.
const char *bomdia_tempo(int c) {
  if (c == 0) return "Céu limpo";
  if (c <= 2) return "Poucas nuvens";
  if (c == 3) return "Nublado";
  if (c == 45 || c == 48) return "Neblina";
  if (c >= 51 && c <= 57) return "Garoa";
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return "Chuva";
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "Neve";
  if (c >= 95) return "Tempestade";
  return "";
}

typedef struct { double lat, lon; } Pedido;
static void *fioClima(void *arg) {
  Pedido p = *(Pedido *)arg;
  char url[300];
  char *r;
  BdClima c;
  free(arg);
  snprintf(url, sizeof url, "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1", p.lat, p.lon);
  r = rede_baixar(url, 6);
  pthread_mutex_lock(&trava);
  if (r && bomdia_parse(r, &c)) { clima = c; temClima = 1; }
  else temClima = -1;
  fio = 0;
  pthread_mutex_unlock(&trava);
  free(r);
  return NULL;
}

static void hojeIso(char *d, size_t n) {
  time_t t = time(NULL);
  struct tm lt;
  localtime_r(&t, &lt);
  strftime(d, n, "%Y-%m-%d", &lt);
}
static void mostrar(void) {
  IlhaModal m;
  IlhaAvisoEx a;
  const ContaPerfil *p = perfis_item_ativo();
  char hoje[12];
  int k = 0;
  time_t agora = time(NULL);
  struct tm lt;
  memset(&m, 0, sizeof m); memset(&a, 0, sizeof a);
  hojeIso(hoje, sizeof hoje);
  localtime_r(&agora, &lt);
  snprintf(m.kicker, sizeof m.kicker, "%s", i18n("Bom dia"));
  if (p && p->nome[0]) snprintf(m.titulo, sizeof m.titulo, i18n("Bom dia, %s"), p->nome);
  else snprintf(m.titulo, sizeof m.titulo, "%s", i18n("Bom dia"));
  if (temClima == 1) {
    if (clima.max > -100) snprintf(m.linha, sizeof m.linha, "%.0f°  ·  %s  ·  %s %.0f°  %s %.0f°", clima.temp, i18n(bomdia_tempo(clima.codigo)),
                                   i18n("máx"), clima.max, i18n("mín"), clima.min);
    else snprintf(m.linha, sizeof m.linha, "%.0f°  ·  %s", clima.temp, i18n(bomdia_tempo(clima.codigo)));
  }
  // Today's reminders (matches of followed teams come in here too).
  for (int i = 0; i < lembrete_n() && k < 3; i++) {
    const Lembrete *l = lembrete_item(i);
    struct tm li;
    char h[16];
    if (!l || l->ini < agora) continue;
    localtime_r(&l->ini, &li);
    if (li.tm_yday != lt.tm_yday || li.tm_year != lt.tm_year) continue;
    hora_tela(h, sizeof h, &li);
    snprintf(m.lista[k++], sizeof m.lista[0], "%s  %s", h, l->titulo);
  }
  for (int i = 0; i < agenda_n() && k < 3; i++) {
    const AgItem *it = agenda_lista(i);
    if (it && !strcmp(it->dataProx, hoje)) snprintf(m.lista[k++], sizeof m.lista[0], "%s: %s", i18n("Episódio novo hoje"), it->titulo);
  }
  if (!m.linha[0] && !k) { printf("[bomdia] nada para dizer hoje\n"); fflush(stdout); return; }
  snprintf(m.icone, sizeof m.icone, "aj_sunrise");
  m.nBotoes = 1;
  snprintf(m.botao[0], sizeof m.botao[0], "%s", i18n("Fechar"));
  a.chave = "bomdia"; a.tipo = ILHA_ACENTO; a.icone = "aj_sunrise"; a.texto = m.titulo; a.ms = 15000u; a.modal = &m; a.tecla = 1;
  ilha_avisar_ex(&a);
  printf("[bomdia] mostrado (%s, %d item(ns))\n", temClima == 1 ? "com clima" : "sem clima", k);
  fflush(stdout);
}

void bomdia_passo(Uint32 agora) {
  static int feitoNestaSessao;
  char arq[40], hoje[12], *t;
  time_t now = time(NULL);
  struct tm lt;
  if (feitoNestaSessao || !ajustes_bom_dia()) return;
  localtime_r(&now, &lt);
  if (lt.tm_hour < 5 || lt.tm_hour >= 12) return;
  if (!desde) { desde = agora ? agora : 1; return; }
  if (agora - desde < 8000u) return;   // let the Home settle first
  snprintf(arq, sizeof arq, "bomdia-p%d.txt", perfis_ativo());
  hojeIso(hoje, sizeof hoje);
  if ((t = dados_ler(arq)) != NULL) {
    int ja = !strncmp(t, hoje, 10);
    free(t);
    if (ja) { feitoNestaSessao = 1; return; }
  }
  pthread_mutex_lock(&trava);
  if (!temClima && !fio) {
    int c = ajustes_salat_cidade() - 1;
    const SalatCidade *cs;
    if (c >= 0 && c < salat_cidades(&cs)) {
      Pedido *p = malloc(sizeof *p);
      pthread_t th;
      if (p) {
        p->lat = cs[c].lat; p->lon = cs[c].lon;
        fio = 1;
        if (pthread_create(&th, NULL, fioClima, p)) { fio = 0; free(p); temClima = -1; } else pthread_detach(th);
      }
    } else temClima = -1;
  }
  if (fio && agora - desde < 14000u) { pthread_mutex_unlock(&trava); return; }
  pthread_mutex_unlock(&trava);
  feitoNestaSessao = 1;
  dados_gravar(arq, hoje);
  mostrar();
}
