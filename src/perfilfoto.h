// QR TIME profile photo chosen on the phone. See perfilfoto.c.
#ifndef QRT_PERFILFOTO_H
#define QRT_PERFILFOTO_H
#include <stddef.h>
void perfilfoto_iniciar(void);                     // registers the phone page (celapps)
int  perfilfoto_salvar(int perfil, const unsigned char *jpg, size_t n);   // 1 ok (JPEG only)
int  perfilfoto_remover(int perfil);
#endif
