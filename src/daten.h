/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
/* daten.h – Laufwerkstabellen und Halbton-Tabellen.
 * Sie sind NICHT Teil dieses Programms: "ls64 einrichten" liest sie einmalig aus der
 * LightScribe System Software des Nutzers (liblightscribe.so.1 / LSPrtEn.dll / .deb / .rpm)
 * und legt sie im Benutzer-Datenordner als lsdaten.bin ab. */
#ifndef DATEN_H
#define DATEN_H
#include <stdint.h>

extern const uint8_t *HT_W;     /* 256 x 4 Fehlergewichte */
extern const uint8_t *HT_THR;   /* 256 x 2 Schwellen (lo, hi) */
extern const uint8_t *HT_M;     /* 128 x 128 Ditherflag */

int  daten_laden(char *err, int errlen);               /* 0 = ok; laedt einmalig */
int  daten_einrichten(const char *quelle, char *meldung, int len);   /* quelle NULL = automatisch suchen */
const char *daten_pfad(void);                          /* wo lsdaten.bin liegt bzw. hin soll */
int  daten_drf_anzahl(void);

#endif
