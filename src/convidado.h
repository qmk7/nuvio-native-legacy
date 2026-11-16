// GUEST MODE (QR TIME): using the app without a Nuvio account.
//
// Why: the base app could not get past the login screen without a Nuvio
// (Supabase) account, and a package built without server settings stopped on
// "this package was built without a server". QR TIME must work for someone who
// only has an IPTV subscription.
//
// What a guest gets: the local profile 1 of this TV. IPTV accounts, favourites,
// guide reminders and settings are already stored per profile on the device
// (dados.c), so they work unchanged. What needs the account (sync, cloud
// library, social) simply stays idle, as it already does with no session.
//
// The choice is a per-device file (convidado.txt in dados_dir). Signing in with
// a real account clears it; signing out of an account does not set it, so the
// person sees the login screen and chooses again.
#ifndef NV_CONVIDADO_H
#define NV_CONVIDADO_H

// 1 when this TV is in guest mode. Reads the file once, then cached.
int  convidado_ativo(void);
// Enter guest mode and remember it. 1 when it is active afterwards (it stays
// active for this run even if the file could not be written).
int  convidado_entrar(void);
// Leave guest mode (to sign in with an account). Removes the file.
void convidado_sair(void);

// Start-up policy, without I/O: open as guest when there is no account
// session and either the person chose guest mode or the build has no account
// server at all (then login can never succeed).
int  convidado_deve_entrar(int sessaoLogada, int servidorPronto, int escolheuConvidado);

// Tests only: forget the cached value so the next call reads the file again.
void convidado_teste_reiniciar(void);

#endif
