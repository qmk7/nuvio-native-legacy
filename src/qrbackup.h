// QR TIME encrypted backup of the data the account does not sync. See qrbackup.c.
#ifndef QRT_QRBACKUP_H
#define QRT_QRBACKUP_H
#include <stddef.h>
void  qrbackup_iniciar(void);                     // registers the phone page (celapps)
int   qrbackup_nome_ok(const char *nome);         // on the backup list (no paths)
char *qrbackup_montar(size_t *n, int *arquivos);  // plaintext archive (malloc'd)
int   qrbackup_aplicar(const char *a, size_t n);  // files written, -1 not an archive
#endif
