// GUEST ADD-ONS (QR TIME): a guest has no Nuvio account to bring an add-on
// list, so the list lives on this TV, per profile ("addons-local-p<N>.txt",
// "name\turl\ton" per line), and is handed to addons.c exactly like the
// account's list (addons_definir_lista).
//
// Adding validates the add-on first: <base>/manifest.json must answer with an
// id and a name. Accepted forms: https://host/path/manifest.json, the base
// without it, and stremio://host/... (read as https). Add-on URLs can carry a
// personal key, so a URL is never logged (only its host) and the file is the
// profile's own data, like the account's add-on list.
#ifndef NV_ADDONSLOCAL_H
#define NV_ADDONSLOCAL_H
#include <stddef.h>

enum { ADL_NADA = 0, ADL_TRABALHANDO, ADL_OK, ADL_FALHOU, ADL_REPETIDO };

// Base URL from what was typed: scheme fixed, "/manifest.json" and trailing
// "/" removed. 1 when it is a usable http(s) URL.
int  addonslocal_base(const char *entrada, char *base, size_t tam);
// Applies this profile's saved list (guest entry, profile change).
void addonslocal_carregar(void);
// Starts validating and adding (BLOCKS nothing). 1 = started.
int  addonslocal_adicionar(const char *entrada);
// Result of the last add (consumed once it is final). `nome` gets the
// add-on's name on success.
int  addonslocal_estado(char *nome, size_t tam);
// Saves the current on/off state of every add-on (after addons_alternar).
void addonslocal_salvar(void);
// Removes the add-ons that are switched off. Returns how many went.
int  addonslocal_remover_desligados(void);
#endif
