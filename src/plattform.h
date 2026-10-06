/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
/* plattform.h – Betriebssystem-Schicht von ls64 (Linux: SG_IO, Windows: SCSI Pass-Through) */
#ifndef PLATTFORM_H
#define PLATTFORM_H
#include <stdio.h>
#include <stdint.h>

/* Ergebnis eines SCSI-Befehls */
typedef struct {
    int fehler;            /* 0 = Befehl wurde ausgefuehrt; sonst Systemfehler (errno / GetLastError) */
    int status;            /* SCSI-Status (0 = GOOD, 2 = CHECK CONDITION) */
    int transport_ok;      /* 1 = kein Host-/Treiberfehler */
    int resid;             /* nicht uebertragene Bytes */
    uint8_t sense[32]; int sense_len;
    unsigned dauer_ms;
} scsi_ergebnis;

/* Laufwerk oeffnen: Linux "/dev/sr0", Windows "E:" (oder "\\.\E:", "CdRom0"). Rueckgabe Handle >= 0, sonst -1 */
int  geraet_oeffnen(const char *name, char *err, int errlen);
void geraet_schliessen(int h);
/* dir: 0 = keine Daten, 1 = vom Geraet lesen, 2 = zum Geraet schreiben */
void geraet_scsi(int h, const uint8_t *cdb, int cdb_len, int dir, uint8_t *buf, int buf_len, int timeout_ms, scsi_ergebnis *e);
/* alle optischen Laufwerke: Namen in namen[i] (max. n), Rueckgabe Anzahl */
int  geraete_liste(char namen[][64], int n);
const char *geraet_standard(void);

double pf_zeit(void);                        /* monotone Sekunden */
void   pf_warte_ms(int ms);
void   pf_abbruch_einrichten(volatile int *flag);   /* Strg+C / Beenden setzt *flag = 1 */
void   pf_wach_halten(int an);               /* Standby waehrend des Brennens verhindern (Windows) */
FILE  *pf_fopen(const char *pfad_utf8, const char *modus);
int    pf_existiert(const char *pfad_utf8);
void   pf_loeschen(const char *pfad_utf8);
int    pf_ordner_anlegen(const char *pfad_utf8);  /* inkl. Elternordner */
void   pf_start(int *argc, char ***argv);    /* argv als UTF-8 (Windows), Konsole auf UTF-8 */
const char *pf_datenordner(void);            /* Benutzer-Datenordner von LS Label Studio */
const char *pf_programmordner(void);         /* Ordner, in dem ls64 liegt */
#define PF_TRENNER (pf_trenner())
char pf_trenner(void);

#endif
