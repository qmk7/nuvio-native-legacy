// QR TIME sports additions: Arabic team names, matching Arabic guides,
// goal detection, automatic reminders once per match.
//   bash tests/qrtime_esportealerta.sh
#include "esportealerta.h"
#include "esportenomes.h"
#include "esporte.h"
#include "lembrete.h"
#include "dados.h"
#include "perfis.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static EspEvento ev(const char *id, int estado, time_t ini, const char *pc, const char *pf) {
  EspEvento e;
  memset(&e, 0, sizeof e);
  snprintf(e.id, sizeof e.id, "%s", id); snprintf(e.liga, sizeof e.liga, "soccer/ksa.1");
  e.estado = estado; e.ini = ini;
  snprintf(e.casa.nome, sizeof e.casa.nome, "Al Hilal"); snprintf(e.casa.curto, sizeof e.casa.curto, "Al Hilal"); snprintf(e.casa.id, sizeof e.casa.id, "1");
  snprintf(e.fora.nome, sizeof e.fora.nome, "Al Nassr"); snprintf(e.fora.curto, sizeof e.fora.curto, "Al Nassr"); snprintf(e.fora.id, sizeof e.fora.id, "2");
  snprintf(e.casa.placar, sizeof e.casa.placar, "%s", pc); snprintf(e.fora.placar, sizeof e.fora.placar, "%s", pf);
  return e;
}
int main(void) {
  time_t agora = time(NULL);
  char m[200];
  dados_iniciar(getenv("NUVIO_DADOS"));
  perfis_definir_ativo(1);
  lembrete_carregar(1);
  CONFERE(!strcmp(esporte_nome_ar("Al Hilal"), "الهلال") && !strcmp(esporte_nome_ar("Man City"), "مانشستر سيتي")
          && !strcmp(esporte_liga_ar("Saudi Pro League"), "دوري روشن السعودي") && !esporte_nome_ar("Unknown FC"), "dictionary");
  {
    EspEvento e = ev("700001", ESP_PRE, agora + 3600, "", "");
    CONFERE(esporte_time_em(&e.casa, "مباراة الهلال والنصر - دوري روشن") && esporte_time_em(&e.fora, "مباراة الهلال والنصر - دوري روشن"),
            "an Arabic guide title names both teams (\"and\" glued to the name)");
    CONFERE(esporte_time_em(&e.casa, "Al Hilal v Al Nassr") && !esporte_time_em(&e.casa, "Al Ittihad v Al Ahli"), "English titles as before");
    CONFERE(esportealerta_lembrar(&e, agora) == 1 && lembrete_n() == 1 && !strcmp(lembrete_item(0)->canal, "esporte:700001"),
            "followed match: one reminder (the match page when no channel is linked)");
    CONFERE(esportealerta_lembrar(&e, agora) == 0 && lembrete_n() == 1, "added once");
    lembrete_alternar("esporte:700001", "Sport Center", lembrete_item(0)->titulo, "", e.ini, e.ini + 7200);   // the person cancels it
    CONFERE(lembrete_n() == 0 && esportealerta_lembrar(&e, agora) == 0 && lembrete_n() == 0, "cancelled by the person: not added again");
    EspEvento longe = ev("700002", ESP_PRE, agora + 4 * 86400, "", "");
    CONFERE(esportealerta_lembrar(&longe, agora) == 0, "more than 48 h away: not yet");
  }
  {
    EspEvento a = ev("700003", ESP_AO_VIVO, agora - 600, "0", "0"), b = ev("700003", ESP_AO_VIVO, agora - 600, "1", "0"),
              c = ev("700003", ESP_AO_VIVO, agora - 600, "0", "0");
    CONFERE(esportealerta_gol(&a, &b, m, sizeof m) && strstr(m, "1 – 0"), "a goal is detected");
    CONFERE(!esportealerta_gol(&b, &b, m, sizeof m), "no change: nothing");
    CONFERE(!esportealerta_gol(&b, &c, m, sizeof m), "a goal taken back (VAR): no notice");
    EspEvento d = ev("700009", ESP_AO_VIVO, agora, "2", "0");
    CONFERE(!esportealerta_gol(&a, &d, m, sizeof m), "another match: not compared");
  }
  return falhas ? 1 : 0;
}
