// MUSIC CENTER: offline copies. A track is saved to this TV's app storage as
// the ORIGINAL file, only where the source allows it: Subsonic's "download"
// method (the server's download permission for this user decides; error 50
// otherwise) or a WebDAV file the account can read. Files: "mo-<hash>.<ext>"
// in the app data folder, listed in "music-offline.txt"; 2 GiB in total.
// The player then plays file:// instead of the network.
#ifndef NV_MCOFFLINE_H
#define NV_MCOFFLINE_H
#include "mcmodelo.h"

#define MCO_TETO (2LL << 30)
enum { MCOF_OK = 0, MCOF_SEM_PERMISSAO, MCOF_SEM_ESPACO, MCOF_REDE, MCOF_FONTE, MCOF_DISCO };

// BLOCKS (worker). Downloads `f` if not there yet.
int  mco_baixar(const McFaixa *f);
// "file:///..." of the local copy, or 0.
int  mco_caminho(const char *id, char *url, size_t n);
int  mco_apagar(const char *id);
int  mco_n(void);
int  mco_item(int i, McFaixa *out);
long long mco_usado(void);
#endif
