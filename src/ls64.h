/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
#ifndef LS64_H
#define LS64_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "plattform.h"
#ifdef _WIN32
#define fopen pf_fopen
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Bild (8-bit Graustufen, 0 = schwarz, 255 = weiss) ---- */
typedef struct { int w, h; double dpi; uint8_t *px; } bild;
int bild_laden(const char *pfad, bild *b, char *err);
int png_speichern(const char *pfad, const uint8_t *px, int w, int h);
/* Tonwertkurve laden (Textdatei: 256 Zahlen oder "ein aus"-Paare). 0 = ok */
int lut_laden(const char *pfad, uint8_t lut[256], char *err);

/* ---- Spurerzeugung ---- */
typedef struct {
    int tpi;               /* Spuren pro Zoll: 760 draft, 1015 normal, 1398 best */
    double r_innen_mm;     /* innerster Druckradius (Laufwerk Page 0x31: 23,8)  */
    double r_aussen_mm;    /* aeusserster Druckradius (58,7)                    */
    double band_von_mm;    /* nur Spuren in diesem Bereich (0 = alles)          */
    double band_bis_mm;
    int fit;               /* 1 = Bild auf Discdurchmesser skalieren, 0 = nach DPI */
    double dpi_ersatz;     /* DPI falls Bild keine Angabe hat (no_fit)          */
    int param400;          /* Medienparameter (Page 0x32 Bytes 12-13), Std. 400 */
    int medium_ersatz;     /* Medien-ID erzwingen (0 = von der Disc)            */
    const uint8_t *lut;    /* Tonwertkurve (256 Eintraege) vor der Halbtonung, NULL = keine */
    int halbton;           /* 0 = Floyd-Steinberg (Standard), 1 = Jarvis, 2 = Stucki */
} spur_opt;

typedef struct { uint8_t *data; uint32_t len; } paket;
typedef struct {
    paket *p; int n, cap;  /* Spurpakete (ohne Endmarke)          */
    int spuren_gesamt;     /* Spuren im Bereich (inkl. leerer)     */
    double dichte;         /* Anteil gebrannter Punkte             */
    uint64_t punkte;       /* gesamte Punktzahl gesendeter Spuren  */
} spurliste;

#define PIX_MM (25.4 / 600.0)   /* Abstand der Marken entlang der Spur */

void spur_opt_standard(spur_opt *o);
int  spuren_erzeugen(const bild *b, const spur_opt *o, spurliste *s, char *err);
void spurliste_frei(spurliste *s);
int  spuren_speichern(const char *pfad, const spurliste *s, const spur_opt *o);
int  spuren_laden(const char *pfad, spurliste *s, spur_opt *o, char *err);
int  spuren_vorschau(const spurliste *s, const spur_opt *o, const char *png, int groesse);
void endmarke(uint8_t out[16]);
/* LightScribe-Druckdatei (.lsd, Windows) laden: Spuren + Laserparameter */
typedef struct { int gueltig, laufwerk, medium, fokus, schreib, lese; unsigned tempo_um, spur_nm, marke_nm; } lsd_param;
int spuren_laden_lsd(const char *pfad, spurliste *s, spur_opt *o, char *err);
const lsd_param *lsd_parameter(void);   /* NULL, wenn keine .lsd geladen */

/* ---- Laufwerk (ls64.c) ---- */
typedef struct {
    int ok;              /* 1 = Befehl erfolgreich (Status GOOD)      */
    int status;          /* SCSI-Status                                */
    uint8_t sense[32];
    int sense_len;
    int key, asc, ascq;  /* aus Sense, falls vorhanden                 */
    int len;             /* uebertragene Bytes (bei Daten vom Geraet)  */
} sg_res;
typedef struct {
    char vendor[9], model[17], rev[5];
    int is_cdrom;
    int ls_feature;           /* Feature 0xFF33 vorhanden            */
    int ls_current;           /* ... und "current" (Labelseite unten) */
    int profile;              /* aktuelles Profil laut GET CONFIG     */
    uint8_t p31[64]; int p31_len;
    uint8_t p32[80]; int p32_len;
    uint8_t evt[8];  int evt_len;
    int tur_ok, tur_key, tur_asc, tur_ascq;
} drive_info;
typedef struct {
    int valid;
    int media_present, ls_media, oriented;
    unsigned drive_inner_um, drive_outer_um, print_inner_um;
    unsigned media_param1, media_param2;
} ls_status;
sg_res sg_exec(int fd, const uint8_t *cdb, int cdb_len, int dir, uint8_t *buf, int buf_len, int timeout_ms);
void sg_log_oeffnen(const char *pfad);
void sg_log_schliessen(void);
int  open_drive(const char *dev);
int  read_all(int fd, drive_info *d);
ls_status decode(const drive_info *d);

/* ---- Laufwerks-/Medienparameter (drf.c) ---- */
/* gleiche Struktur wie drf_eintrag in drf_tabelle.h */
typedef struct { short laufwerk, medium, fokus, tempo, schreib, lese, tpi[4], mpi; } drf_param;
const drf_param *drf_suchen(int laufwerk, int medium);
/* Laufwerk-ID: Page 0x31 Byte 0x0C; Medien-ID: Mode-Header Byte 2 von Page 0x32 */
#define LAUFWERK_ID(p31) ((p31)[0x0c])
#define MEDIEN_ID(p32)   ((p32)[2])

/* ---- Brennen (brennen.c) ---- */
int brennen(const char *dev, const spurliste *s, const spur_opt *o, int ohne_rueckfrage);
/* Optional: Laserparameter aus Tabelle (qualitaet kontrast geschwindigkeit leistung fokus ...) statt DRF-Werte.
 * kontrast < 0 = aus (Standard). */
void brennen_kontrast_setzen(int kontrast, const char *tabelle);
/* Trockenlauf: Parameter berechnen/anzeigen, kein Laufwerk oeffnen (Laufwerk-/Medien-ID vorgeben) */
void brennen_trocken_setzen(int laufwerk_id, int medium_id);
/* Brennen sauber abbrechen, sobald diese Datei existiert (fuer die Oberflaeche, v. a. Windows) */
void brennen_stopdatei_setzen(const char *pfad);
void close_drive(int fd);

#endif
