// QR TIME: shortcuts in the LG launcher (webOS addLaunchPoint). See atalhoslg.c.
#ifndef QRT_ATALHOSLG_H
#define QRT_ATALHOSLG_H
void atalhoslg_dir(const char *dirArte);         // main.c: where the art folder is (the icon sits next to it)
int  atalhoslg_criar(void);                      // asks webOS for the 3 shortcuts; 1 = asked
// argv[1] at launch: the JSON webOS passes; remembers {"qrtime":"..."}.
void atalhoslg_lancamento(const char *json);
// App thread, once inside the app: the MENU_* the shortcut asked for, or -1 (once).
int  atalhoslg_destino(void);
#endif
