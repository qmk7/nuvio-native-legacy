// QR TIME party mode: guests request songs from their phones. See mcfesta.c.
#ifndef QRT_MCFESTA_H
#define QRT_MCFESTA_H
#include <SDL2/SDL.h>
#include "lanweb.h"
#include "mcmodelo.h"

void mcfesta_iniciar(void);                     // registers the phone page handlers (celapps)
void mcfesta_pedido(const LwPedido *p);
void mcfesta_quadro(Uint32 agora);
void mcfesta_acao(const char *chave, int botao); // the TV notice: 1 approve, 2 decline
int  mcfesta_decidir(int id, int aprovar);
int  mcfesta_votos_precisa(void);
// Tests.
void mcfesta_teste_busca(int (*fn)(const char *q, McFaixa *out, int max));
int  mcfesta_teste_pedidos(void);
#endif
