// QR TIME phase 8: data schema, upgrades, cache invalidation, explicit resets
// and profile isolation (src/qrdados.c), and switching a music source off.
//
//   bash tests/qrtime_dados.sh
#include "qrdados.h"
#include "dados.h"
#include "perfis.h"
#include "mcfontes.h"
#include "mcfila.h"
#include "mcoffline.h"
#include "agenda.h"
#include "esporte.h"
#include "pularserie.h"
#include "intro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int falhas;
#define CONFERE(c, o) do { int ok_ = (c); printf("%s: %s\n", ok_ ? "PASS" : "FAIL", o); if (!ok_) falhas++; } while (0)
static int existe(const char *n) { char *t = dados_ler(n); int r = t != NULL; free(t); return r; }
static int contem(const char *n, const char *s) { char *t = dados_ler(n); int r = t && strstr(t, s); free(t); return r; }

static int chamadas[4], falharPasso2 = 1;
static int p1(void) { chamadas[0]++; dados_gravar("upgrade-mark.txt", "v2"); return 1; }
static int p2(void) { chamadas[1]++; return !falharPasso2; }

int main(void) {
  dados_iniciar(getenv("NUVIO_DADOS"));
  perfis_definir_ativo(1);

  // ---- schema ----
  dados_gravar("progresso.txt", "user progress");
  CONFERE(qrdados_esquema_iniciar() == 0 && qrdados_esquema_disco() == QRD_ESQUEMA && contem(QRD_ARQ, "schema=2"),
          "first start: schema recorded (v2)");
  CONFERE(contem("qrtime-quality.txt", "pendente"), "v2: max-quality defaults marked for the first settings load");
  CONFERE(contem("progresso.txt", "user progress"), "first start: existing user data untouched");
  {
    // Disk is at the app's version (2); steps [2] (2->3) and [3] (3->4) are the test's.
    QrdPasso v[4] = { NULL, NULL, p1, p2 };
    qrdados_teste_passos(v, 4, 4);
    qrdados_esquema_iniciar();
    CONFERE(chamadas[0] == 1 && chamadas[1] == 1 && qrdados_esquema_disco() == 3 && contem(QRD_ARQ, "schema=3"),
            "upgrade 2->3 done, 3->4 failed: stays at 3 (no partial jump)");
    CONFERE(contem("progresso.txt", "user progress"), "a failed upgrade removes nothing");
    falharPasso2 = 0;
    qrdados_esquema_iniciar();
    CONFERE(chamadas[0] == 1 && chamadas[1] == 2 && qrdados_esquema_disco() == 4, "next start resumes at the failed step only");
    qrdados_teste_passos(NULL, 0, 0);
    qrdados_esquema_iniciar();
    CONFERE(qrdados_mais_novo() && contem(QRD_ARQ, "schema=4") && contem("progresso.txt", "user progress"),
            "older app on newer data: nothing downgraded, nothing removed");
  }

  // ---- cache: allowlist only ----
  {
    char c[700];
    dados_gravar("catalogo-abc.bin", "x");
    dados_gravar("epg-1.xml", "x");
    dados_gravar("ficha-tt1.json", "x");
    dados_gravar("music-favorites-p1.txt", "keep");
    dados_gravar("trakt-p1.txt", "keep");
    dados_gravar("unknown-file.dat", "keep");
    dados_caminho(c, sizeof c, "cache"); mkdir(c, 0700);
    dados_gravar("cache/img1.bin", "xxxx");
    CONFERE(qrdados_e_cache("catalogo-abc.bin") && qrdados_e_cache("cache") && !qrdados_e_cache("progresso.txt")
            && !qrdados_e_cache("music-sources-p1.txt") && !qrdados_e_cache("../x") && !qrdados_e_cache("unknown-file.dat"),
            "cache allowlist: regenerable names only, unknown names are user data");
    CONFERE(qrdados_cache_bytes() >= 7, "cache size measured");
    CONFERE(qrdados_resetar(QRR_CACHE) && !existe("catalogo-abc.bin") && !existe("epg-1.xml") && !existe("cache/img1.bin"),
            "clear cache removes the regenerable files");
    CONFERE(existe("progresso.txt") && existe("music-favorites-p1.txt") && existe("trakt-p1.txt") && existe("unknown-file.dat")
            && existe(QRD_ARQ), "...and keeps progress, favourites, tokens, unknown files, the schema");
    struct stat st;
    CONFERE(!stat(c, &st) && S_ISDIR(st.st_mode), "the cache folder itself stays (live writers reuse it)");
  }

  // ---- resets touch only this profile ----
  {
    const char *const arq[] = { "music-history-p%d.txt", "music-queue-p%d.txt", "music-favorites-p%d.txt",
                                "music-playlists-p%d.txt", "agenda-follows-p%d.txt", "esporte-p%d.txt", NULL };
    char n[64];
    for (int p = 1; p <= 2; p++) for (int i = 0; arq[i]; i++) { snprintf(n, sizeof n, arq[i], p); dados_gravar(n, "data\n"); }
    perfis_definir_ativo(1);
    qrdados_resetar(QRR_MUSICA_HIST);
    CONFERE(!existe("music-history-p1.txt") && !existe("music-queue-p1.txt") && existe("music-favorites-p1.txt")
            && existe("music-history-p2.txt") && existe("music-queue-p2.txt"),
            "music history reset: profile 1 only; its favourites and profile 2 kept");
    qrdados_resetar(QRR_MUSICA_BIBLIO);
    CONFERE(!existe("music-favorites-p1.txt") && !existe("music-playlists-p1.txt") && existe("music-favorites-p2.txt")
            && existe("music-playlists-p2.txt"), "favourites/playlists reset: profile 1 only");
    CONFERE(agenda_seguir("tt9000009", "Show", "", 1) && agenda_segue("tt9000009"), "a followed series (profile 1)");
    qrdados_resetar(QRR_AGENDA_SEGUIR);
    CONFERE(!agenda_segue("tt9000009") && !existe("agenda-follows-p1.txt") && existe("agenda-follows-p2.txt"),
            "follows reset: profile 1 forgets, in memory too; profile 2 kept");
    qrdados_resetar(QRR_ESPORTE);
    CONFERE(!existe("esporte-p1.txt") && existe("esporte-p2.txt"), "Sport Center reset: profile 1 only");
    dados_gravar("youtube.txt", "key"); dados_gravar("spotify.txt", "id\tsecret");
    dados_gravar("youtube-account-p1.txt", "tok1"); dados_gravar("youtube-account-p2.txt", "tok2");
    qrdados_resetar(QRR_CONTAS_PERFIL);
    CONFERE(!existe("youtube-account-p1.txt") && existe("youtube-account-p2.txt") && existe("youtube.txt"),
            "disconnect accounts: this profile's link only; the TV's API key stays");
    CONFERE(qrdados_reset_por_tv(QRR_CHAVES_TV) && !qrdados_reset_por_tv(QRR_MUSICA_HIST), "each reset says whether it is for the whole TV");
    qrdados_resetar(QRR_CHAVES_TV);
    CONFERE(!existe("youtube.txt") && !existe("spotify.txt") && existe("youtube-account-p2.txt") && existe("progresso.txt"),
            "delete API keys: keys gone, nothing else");
    CONFERE(!qrdados_resetar(QRR_N) && !qrdados_resetar(-1), "unknown reset: refused");
  }

  // ---- skip intro/recap for good, per series and profile ----
  {
    perfis_definir_ativo(1);
    CONFERE(!pularserie_manual("tt0903747", INTRO_ABERTURA) && !pularserie_sempre("tt0903747", INTRO_ABERTURA),
            "first manual intro skip: not automatic yet");
    CONFERE(pularserie_manual("tt0903747", INTRO_ABERTURA) && pularserie_sempre("tt0903747", INTRO_ABERTURA)
            && !pularserie_sempre("tt0903747", INTRO_RESUMO) && !pularserie_sempre("tt0944947", INTRO_ABERTURA),
            "second: automatic for that series and kind only");
    CONFERE(!pularserie_manual("tt0903747", INTRO_ABERTURA), "already automatic: no second announcement");
    CONFERE(!pularserie_manual("tt0903747", INTRO_CREDITOS) && !pularserie_sempre("tt0903747", INTRO_CREDITOS),
            "credits are not remembered (they have their own rules)");
    pularserie_recarregar();
    CONFERE(pularserie_sempre("tt0903747", INTRO_ABERTURA), "survives a restart");
    perfis_definir_ativo(2);
    CONFERE(!pularserie_sempre("tt0903747", INTRO_ABERTURA), "profile 2 does not inherit it");
    perfis_definir_ativo(1);
    CONFERE(qrdados_resetar(QRR_PULO_SERIES) && !pularserie_sempre("tt0903747", INTRO_ABERTURA)
            && !existe("skip-series-p1.txt"), "reset: forgotten for this profile");
    CONFERE(!pularserie_manual("x", 0) && !pularserie_manual(NULL, INTRO_ABERTURA), "bad input refused");
  }

  // ---- a music source switched off ----
  {
    McFonte f, g;
    memset(&f, 0, sizeof f);
    f.tipo = MCS_SUBSONIC;
    snprintf(f.nome, sizeof f.nome, "Home");
    snprintf(f.ss.url, sizeof f.ss.url, "http://192.168.1.2:4533");
    snprintf(f.ss.usuario, sizeof f.ss.usuario, "u"); snprintf(f.ss.senha, sizeof f.ss.senha, "p w");
    CONFERE(mcfontes_salvar(&f) && mcfontes_ativa(0), "a source starts switched on");
    CONFERE(mcfontes_ligar(f.chave, 0) && !mcfontes_ativa(0) && mcfontes_n() == 1, "switched off: kept, not forgotten");
    mcfontes_carregar();
    CONFERE(mcfontes_copiar(0, &g) && g.desligada && !strcmp(g.ss.senha, "p w"), "off state survives a restart; password intact");
    {
      McFaixa t;
      memset(&t, 0, sizeof t);
      snprintf(t.id, sizeof t.id, "%s:song1", f.chave);
      CONFERE(mco_baixar(&t) == MCOF_FONTE, "a switched-off source is not contacted (offline download refused)");
    }
    mcfontes_ligar(f.chave, 1);
    CONFERE(mcfontes_ativa(0), "switched back on");
    {
      char n[48], linha[300];
      snprintf(n, sizeof n, "music-sources-p1.txt");
      snprintf(linha, sizeof linha, "1\tssdeadbeef\tOld\thttp://h\tu\tpw\n");   // a file from before phase 8 (6 columns)
      dados_gravar(n, linha);
      mcfontes_carregar();
      CONFERE(mcfontes_n() == 1 && mcfontes_ativa(0) && mcfontes_copiar(0, &g) && !strcmp(g.ss.senha, "pw"),
              "upgrade: a 6-column file from before loads, switched on, password intact");
    }
  }
  return falhas ? 1 : 0;
}
