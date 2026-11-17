// XMLTV start/stop times with time zones (src/xmltv.c, xmltv_tempo).
// The guide keeps UTC epochs; the offset in the XMLTV string must move the
// local wall time back to UTC. No offset means UTC (XMLTV DTD).
#include "xmltv.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  // 2026-11-17 02:00:00 UTC
  const long long T = 1794880800LL;
  assert(xmltv_tempo("20261117020000") == T);              // no offset = UTC
  assert(xmltv_tempo("20261117020000 +0000") == T);
  assert(xmltv_tempo("20261117050000 +0300") == T);        // Istanbul / Riyadh
  assert(xmltv_tempo("20261116210000 -0500") == T);        // New York (EST)
  assert(xmltv_tempo("20261117073000 +0530") == T);        // India, half-hour zone
  assert(xmltv_tempo("202611170200 +0000") == T);          // seconds are optional
  assert(xmltv_tempo("20261117020030 +0000") == T + 30);
  assert(xmltv_tempo("20261117010000 -0100") == T);        // crosses no date, minus
  assert(xmltv_tempo("20261116230000 -0300") == T);        // crosses midnight backwards
  assert(xmltv_tempo("20261231233000 -0300") == 1798770600LL); // new year in UTC
  assert(xmltv_tempo("nonsense") == 0);
  assert(xmltv_tempo("20261317020000") == 0);              // month 13
  puts("qrtime_xmltv_tz: ok");
  return 0;
}
