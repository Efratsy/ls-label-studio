/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
/*
 * ls64 – nativer 64-bit-LightScribe-Treiber von LS Label Studio (Linux und Windows)
 *
 *   ls64 einrichten [QUELLE]    Tabellen einmalig aus der LightScribe System Software lesen
 *   ls64 suchen                 alle optischen Laufwerke pruefen
 *   ls64 info   [LAUFWERK]      Laufwerk + Disc anzeigen (nur lesende Befehle)
 *   ls64 roh    [LAUFWERK]      zusaetzlich Rohdaten (Hex) aller Antworten
 *   ls64 spuren BILD DATEI.lsp [Optionen]   Spurdaten berechnen (offline)
 *   ls64 vorschau DATEI.lsp BILD.png         Vorschau aus Spurdaten
 *   ls64 brennen BILD|DATEI.lsp|DATEI.lsd [Optionen]   Label brennen
 *
 * Erkennung (nur lesend): 12 INQUIRY, 46 GET CONFIGURATION (Feature 0xFF33), 4A GET EVENT STATUS,
 *   5A MODE SENSE(10) Seite 0x31 (Laufwerk) / 0x32 (Disc), 00 TEST UNIT READY
 * Bau: siehe Makefile (Linux) bzw. windows/build_windows.bat
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ls64.h"
#include "plattform.h"
#include "daten.h"

#define VERSION "1.1"

static int verbose;

/* ------------------------------------------------------------------ */
/* SCSI-Grundfunktion                                                  */
/* ------------------------------------------------------------------ */

static void hexdump(const char *title, const uint8_t *p, int n) {
    printf("    %s (%d Bytes)\n", title, n);
    for (int i = 0; i < n; i += 16) {
        printf("      %04x: ", i);
        for (int j = 0; j < 16; j++) { if (i + j < n) printf("%02x ", p[i + j]); else printf("   "); }
        printf(" ");
        for (int j = 0; j < 16 && i + j < n; j++) putchar(p[i + j] >= 32 && p[i + j] < 127 ? p[i + j] : '.');
        putchar('\n');
    }
}

/* Binaerprotokoll aller Laufwerksbefehle (Option --log), Format "SGR2" */
static FILE *logf; static uint32_t logseq;
void sg_log_oeffnen(const char *pfad) { logf = pfad ? pf_fopen(pfad, "wb") : NULL; }
void sg_log_schliessen(void) { if (logf) fclose(logf); logf = NULL; }
static void sg_log(const uint8_t *cdb, int cdb_len, int dir, const uint8_t *buf, int buf_len, int timeout_ms, double t0, double t1, const scsi_ergebnis *e) {
    if (!logf) return;
    uint32_t rec[17] = {0}; uint8_t c[16] = {0}, sense[32] = {0};
    memcpy(rec, "SGR2", 4);
    rec[1] = ++logseq; rec[2] = (uint32_t)t0; rec[3] = (uint32_t)((t0 - (uint32_t)t0) * 1e9); rec[4] = (uint32_t)t1; rec[5] = (uint32_t)((t1 - (uint32_t)t1) * 1e9);
    rec[6] = 0; rec[7] = 0x2285; rec[8] = e->fehler ? (uint32_t)-e->fehler : 0;
    rec[9] = (uint32_t)(!buf_len ? -1 : dir == 2 ? -2 : -3); rec[10] = buf_len; rec[11] = 0; rec[12] = timeout_ms;
    uint8_t *b4 = (uint8_t *)&rec[13]; b4[0] = cdb_len; b4[1] = e->status; b4[2] = e->sense_len; b4[3] = 0;
    rec[14] = e->transport_ok ? 0 : 1; rec[15] = e->resid; rec[16] = e->dauer_ms;
    memcpy(c, cdb, cdb_len > 16 ? 16 : cdb_len);
    memcpy(sense, e->sense, e->sense_len > 32 ? 32 : e->sense_len);
    uint32_t dl = 0;
    if (!e->fehler && buf_len) dl = dir == 2 ? (uint32_t)buf_len : (uint32_t)(buf_len - (e->resid > 0 ? e->resid : 0));
    fwrite(rec, 4, 17, logf); fwrite(c, 1, 16, logf); fwrite(sense, 1, 32, logf); fwrite(&dl, 4, 1, logf);
    if (dl) fwrite(buf, 1, dl, logf);
    fflush(logf);
}

/* dir: 0 = keine Daten, 1 = vom Geraet lesen, 2 = zum Geraet schreiben */
sg_res sg_exec(int fd, const uint8_t *cdb, int cdb_len, int dir, uint8_t *buf, int buf_len, int timeout_ms) {
    sg_res r; memset(&r, 0, sizeof r);
    scsi_ergebnis e;
    double t0 = pf_zeit();
    geraet_scsi(fd, cdb, cdb_len, dir, buf, buf_len, timeout_ms, &e);
    sg_log(cdb, cdb_len, dir, buf, buf_len, timeout_ms, t0, pf_zeit(), &e);
    if (e.fehler) { r.status = -e.fehler; return r; }
    r.status = e.status;
    r.sense_len = e.sense_len; memcpy(r.sense, e.sense, sizeof r.sense);
    if (r.sense_len >= 14) { r.key = r.sense[2] & 0x0f; r.asc = r.sense[12]; r.ascq = r.sense[13]; }
    r.len = buf_len - e.resid;
    r.ok = (e.status == 0 && e.transport_ok);
    if (verbose) {
        printf("  CDB");
        for (int i = 0; i < cdb_len; i++) printf(" %02x", cdb[i]);
        printf("  -> %s", r.ok ? "ok" : "FEHLER");
        if (r.sense_len) printf("  Sense %x/%02x/%02x", r.key, r.asc, r.ascq);
        printf("\n");
        if (r.ok && buf_len && dir != 2) hexdump("Antwort", buf, r.len);
    }
    return r;
}
static sg_res sg_cmd(int fd, const uint8_t *cdb, int cdb_len, uint8_t *buf, int buf_len, int timeout_ms) {
    return sg_exec(fd, cdb, cdb_len, buf_len ? 1 : 0, buf, buf_len, timeout_ms);
}

/* ------------------------------------------------------------------ */
/* Einzelne Befehle                                                    */
/* ------------------------------------------------------------------ */

static void cpy_trim(char *dst, const uint8_t *src, int n) {
    memcpy(dst, src, n); dst[n] = 0;
    for (int i = n - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;
}

int read_all(int fd, drive_info *d) {
    uint8_t b[512];
    memset(d, 0, sizeof *d);

    static const uint8_t inq[6] = {0x12, 0, 0, 0, 0x60, 0};
    sg_res r = sg_cmd(fd, inq, 6, b, 0x60, 10000);
    if (!r.ok) return -1;
    d->is_cdrom = (b[0] & 0x1f) == 5;
    cpy_trim(d->vendor, b + 8, 8); cpy_trim(d->model, b + 16, 16); cpy_trim(d->rev, b + 32, 4);

    static const uint8_t conf[10] = {0x46, 0x02, 0xff, 0x33, 0, 0, 0, 0, 0x10, 0};
    r = sg_cmd(fd, conf, 10, b, 16, 10000);
    if (r.ok && r.len >= 12) {
        d->profile = (b[6] << 8) | b[7];
        if (b[8] == 0xff && b[9] == 0x33) { d->ls_feature = 1; d->ls_current = b[10] & 1; }
    }
    if (!d->ls_feature) return 0;

    static const uint8_t evt[10] = {0x4a, 0x01, 0, 0, 0x10, 0, 0, 0, 0x08, 0};
    r = sg_cmd(fd, evt, 10, d->evt, 8, 10000); d->evt_len = r.ok ? r.len : 0;

    static const uint8_t tur[6] = {0, 0, 0, 0, 0, 0};
    r = sg_cmd(fd, tur, 6, NULL, 0, 10000);
    d->tur_ok = r.ok; d->tur_key = r.key; d->tur_asc = r.asc; d->tur_ascq = r.ascq;

    static const uint8_t ms31[10] = {0x5a, 0x08, 0x31, 0, 0, 0, 0, 0x02, 0x00, 0x01};
    r = sg_cmd(fd, ms31, 10, b, 512, 10000);
    if (r.ok) { d->p31_len = r.len > 64 ? 64 : r.len; memcpy(d->p31, b, d->p31_len); }

    static const uint8_t ms32[10] = {0x5a, 0x08, 0x32, 0, 0, 0, 0, 0x02, 0x00, 0x01};
    r = sg_cmd(fd, ms32, 10, b, 512, 10000);
    if (r.ok) { d->p32_len = r.len > 80 ? 80 : r.len; memcpy(d->p32, b, d->p32_len); }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Auswertung                                                         */
/* ------------------------------------------------------------------ */
static unsigned be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }


ls_status decode(const drive_info *d) {
    ls_status s; memset(&s, 0, sizeof s);
    if (d->p32_len >= 16 && d->p32[8] == 0x32) {
        uint8_t f = d->p32[11];
        s.ls_media = (f & 0x08) != 0;
        s.oriented = (f & 0x05) == 0x05;
        s.media_param1 = be16(d->p32 + 12);   /* 400 – taucht als Header-Feld in jedem Spurpaket auf */
        s.media_param2 = be16(d->p32 + 14);
        s.valid = 1;
    }
    if (d->evt_len >= 6) s.media_present = (d->evt[5] & 0x02) != 0;
    if (d->p31_len >= 48 && d->p31[8] == 0x31) {
        s.print_inner_um = be16(d->p31 + 0x1c);
        s.drive_inner_um = be16(d->p31 + 0x2c);
        s.drive_outer_um = be16(d->p31 + 0x2e);
    }
    return s;
}

static const char *ja(int v) { return v ? "ja" : "nein"; }

int open_drive(const char *dev) {
    char err[300];
    int fd = geraet_oeffnen(dev, err, sizeof err);
    if (fd < 0) fprintf(stderr, "%s\n", err);
    return fd;
}
void close_drive(int fd) { geraet_schliessen(fd); }

static int cmd_info(const char *dev, int raw) {
    int fd = open_drive(dev); if (fd < 0) return 1;
    drive_info d;
    verbose = raw;
    if (raw) printf("Rohdaten:\n");
    if (read_all(fd, &d) < 0) { fprintf(stderr, "%s antwortet nicht auf INQUIRY\n", dev); close_drive(fd); return 1; }
    close_drive(fd);
    if (raw) printf("\n");

    printf("Laufwerk      : %s\n", dev);
    printf("Hersteller    : %s\n", d.vendor);
    printf("Modell        : %s  (Firmware %s)\n", d.model, d.rev);
    printf("LightScribe   : %s\n", d.ls_feature ? "ja (Feature 0xFF33)" : "NEIN – kein LightScribe-Laufwerk");
    if (!d.ls_feature) return 2;

    ls_status s = decode(&d);
    if (s.drive_inner_um)
        printf("Laufwerk-Radius: innen %.1f mm, aussen %.1f mm\n", s.drive_inner_um / 1000.0, s.drive_outer_um / 1000.0);
    printf("\nDisc eingelegt : %s\n", ja(s.media_present));
    printf("LightScribe-Disc: %s\n", ja(s.ls_media));
    printf("Labelseite unten: %s\n", ja(s.oriented && d.ls_current));
    if (s.oriented && d.ls_current) {
        if (s.print_inner_um)
            printf("Druckbereich   : %.1f – %.1f mm Radius\n", s.print_inner_um / 1000.0, s.drive_outer_um / 1000.0);
        printf("Medienparameter: %u / %u\n", s.media_param1, s.media_param2);
        int lw = d.p31_len > 0x0c ? LAUFWERK_ID(d.p31) : -1, md = d.p32_len > 2 ? MEDIEN_ID(d.p32) : -1;
        const drf_param *mp = drf_suchen(lw, md);
        printf("Laufwerk-ID    : %d, Medien-ID %d\n", lw, md);
        if (mp && mp->tempo > 0)
            printf("Laserparameter : Fokus %d, Schreibleistung %d, Leseleistung %d, %d mm/s\n"
                   "Qualitaeten    : draft %d, normal %d, best %d Spuren/Zoll\n",
                   mp->fokus, mp->schreib, mp->lese, mp->tempo, mp->tpi[1], mp->tpi[2], mp->tpi[3]);
        else
            printf("Laserparameter : UNBEKANNT fuer diese Kombination\n");
        printf("\n=> BEREIT zum Beschriften.\n");
    } else if (!d.tur_ok && d.tur_key == 2 && d.tur_asc == 4 && d.tur_ascq == 1) {
        printf("\n=> Laufwerk erkennt die Disc gerade noch (bitte ca. 1 Minute warten und erneut pruefen).\n");
    } else if (!d.tur_ok && d.tur_key == 2 && d.tur_asc == 4 && d.tur_ascq == 0) {
        /* Laufwerk haengt nach der Erkennung in "nicht bereit" */
        printf("\n=> Laufwerk meldet 'nicht bereit'. Bitte Disc auswerfen, mit der Labelseite nach unten\n"
               "   neu einlegen, ca. 1 Minute warten und erneut pruefen.\n");
    } else if (s.ls_media && !s.oriented && d.profile != 0 && d.profile != 0xffff) {
        printf("\n=> LightScribe-Disc liegt falsch herum: bitte mit der LABELSEITE NACH UNTEN einlegen.\n");
    } else if (s.media_present && !s.ls_media) {
        printf("\n=> Keine LightScribe-Disc erkannt.\n");
    } else if (!s.media_present) {
        printf("\n=> Keine Disc eingelegt.\n");
    } else {
        printf("\n=> Zustand unklar (Sense %x/%02x/%02x) – bitte 'ls64 roh' ausfuehren.\n", d.tur_key, d.tur_asc, d.tur_ascq);
    }
    return 0;
}

static int cmd_suchen(void) {
    char namen[16][64]; int n = geraete_liste(namen, 16), found = 0;
    if (!n) { printf("Keine optischen Laufwerke gefunden.\n"); return 1; }
    for (int i = 0; i < n; i++) {
        int fd = open_drive(namen[i]); if (fd < 0) continue;
        drive_info d;
        if (read_all(fd, &d) == 0) {
            printf("%-10s %-9s %-17s %s\n", namen[i], d.vendor, d.model, d.ls_feature ? "LightScribe: ja" : "LightScribe: nein");
            if (d.ls_feature) found++;
        }
        close_drive(fd);
    }
    return found ? 0 : 2;
}

static int cmd_einrichten(int argc, char **argv) {
    char m[1500], e[400];
    if (argc > 2 && !strcmp(argv[2], "--pruefen")) {
        if (daten_laden(e, sizeof e) == 0) { printf("OK: Tabellen vorhanden (%d Laufwerk/Medium-Eintraege)\n", daten_drf_anzahl()); return 0; }
        printf("FEHLT: %s\n", e); return 4;
    }
    int r = daten_einrichten(argc > 2 ? argv[2] : NULL, m, sizeof m);
    printf("%s\n", m);
    return r ? 4 : 0;
}

/* ------------------------------------------------------------------ */
/* Spurdaten offline                                                  */
/* ------------------------------------------------------------------ */
static int qualitaet_tpi(const char *q) {
    if (!strcmp(q, "draft") || !strcmp(q, "entwurf")) return 760;
    if (!strcmp(q, "normal") || !strcmp(q, "medium")) return 1015;
    if (!strcmp(q, "best") || !strcmp(q, "beste")) return 1398;
    return 0;
}

static void zeige_statistik(const spurliste *s, const spur_opt *o) {
    double pitch = 25.4 / o->tpi;
    /* Brenndauer: Laufwerk brennt ~5400 Marken/s bei 275 mm/s (gemessen: 26:49 min bei 1015 Spuren/Zoll) + ~30 s Anlauf */
    printf("Qualitaet      : %d Spuren/Zoll (Abstand %.1f um)\n", o->tpi, pitch * 1000);
    printf("Spuren         : %d im Bereich, davon %d mit Inhalt (werden gesendet)\n", s->spuren_gesamt, s->n);
    if (s->n) {
        int a = s->p[0].data[0] << 8 | s->p[0].data[1], b = s->p[s->n - 1].data[0] << 8 | s->p[s->n - 1].data[1];
        printf("Radius         : %.2f – %.2f mm (Spur %d – %d)\n", o->r_innen_mm + (a + 0.5) * pitch, o->r_innen_mm + (b + 0.5) * pitch, a, b);
    }
    uint64_t bytes = 16; for (int i = 0; i < s->n; i++) bytes += s->p[i].len;
    printf("Daten          : %.1f kB, Punktdichte %.1f %%\n", bytes / 1024.0, s->dichte * 100);
    if (s->n) { int sek = (int)(s->punkte / 5400.0 + 30); printf("Brenndauer     : ca. %d:%02d min\n", sek / 60, sek % 60); }
}

static int cmd_spuren(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "Aufruf: ls64 spuren BILD AUSGABE.lsp [-q draft|normal|best] [-s fit|nofit] [-d DPI] [-b VON:BIS mm] [-v VORSCHAU.png] [--kurve DATEI] [--halbton hp|fs|jarvis|stucki]\n"); return 1; }
    { char de[400]; if (daten_laden(de, sizeof de)) { fprintf(stderr, "%s\n", de); return 4; } }
    spur_opt o; spur_opt_standard(&o);
    const char *vorschau = NULL, *kurve = NULL; uint8_t lut[256]; char err[256];
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "-q") && i + 1 < argc) { o.tpi = qualitaet_tpi(argv[++i]); if (!o.tpi) { fprintf(stderr, "Qualitaet: draft, normal oder best\n"); return 1; } }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) { i++; o.fit = strcmp(argv[i], "nofit") && strcmp(argv[i], "no_fit"); }
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) o.dpi_ersatz = atof(argv[++i]);
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) { if (sscanf(argv[++i], "%lf:%lf", &o.band_von_mm, &o.band_bis_mm) != 2) { fprintf(stderr, "Band: z.B. -b 31.4:36.5\n"); return 1; } }
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) vorschau = argv[++i];
        else if (!strcmp(argv[i], "--kurve") && i + 1 < argc) { kurve = argv[++i]; if (lut_laden(kurve, lut, err)) { fprintf(stderr, "%s\n", err); return 1; } o.lut = lut; }
        else if (!strcmp(argv[i], "--halbton") && i + 1 < argc) { i++; if (!strcmp(argv[i], "hp")) o.halbton = 3; else if (!strcmp(argv[i], "fs")) o.halbton = 0; else if (!strcmp(argv[i], "jarvis")) o.halbton = 1; else if (!strcmp(argv[i], "stucki")) o.halbton = 2; else { fprintf(stderr, "Halbton: hp, fs, jarvis oder stucki\n"); return 1; } }
        else { fprintf(stderr, "Unbekannte Option: %s\n", argv[i]); return 1; }
    }
    bild b;
    if (bild_laden(argv[2], &b, err)) { fprintf(stderr, "Bild: %s\n", err); return 1; }
    if (kurve) printf("Tonwertkurve   : %s\n", kurve);
    printf("Halbtonung     : %s\n", o.halbton == 3 ? "Standard" : o.halbton == 1 ? "Jarvis" : o.halbton == 2 ? "Stucki" : "Floyd-Steinberg");
    printf("Bild           : %s, %d x %d Pixel, %s\n", argv[2], b.w, b.h, o.fit ? "auf Disc skaliert (fit)" : "Originalgroesse (nofit)");
    spurliste s;
    if (spuren_erzeugen(&b, &o, &s, err)) { fprintf(stderr, "%s\n", err); return 1; }
    zeige_statistik(&s, &o);
    if (spuren_speichern(argv[3], &s, &o)) { fprintf(stderr, "kann %s nicht schreiben\n", argv[3]); return 1; }
    printf("Gespeichert    : %s\n", argv[3]);
    if (vorschau) { spuren_vorschau(&s, &o, vorschau, 1000); printf("Vorschau       : %s\n", vorschau); }
    spurliste_frei(&s); free(b.px);
    return 0;
}

static int endet_mit(const char *s, const char *e) { size_t a = strlen(s), b = strlen(e); return a >= b && !strcmp(s + a - b, e); }

static int cmd_vorschau(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "Aufruf: ls64 vorschau DATEI.lsp BILD.png [Groesse]\n"); return 1; }
    spurliste s; spur_opt o; char err[256];
    if ((endet_mit(argv[2], ".lsd") ? spuren_laden_lsd(argv[2], &s, &o, err) : spuren_laden(argv[2], &s, &o, err))) { fprintf(stderr, "%s\n", err); return 1; }
    zeige_statistik(&s, &o);
    if (spuren_vorschau(&s, &o, argv[3], argc > 4 ? atoi(argv[4]) : 1000)) { fprintf(stderr, "kann %s nicht schreiben\n", argv[3]); return 1; }
    printf("Vorschau       : %s\n", argv[3]);
    spurliste_frei(&s);
    return 0;
}


static int cmd_brennen(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "Aufruf: ls64 brennen BILD|DATEI.lsp|DATEI.lsd [-q draft|normal|best] [-s fit|nofit] [-b VON:BIS] [-g /dev/sr0] [--log DATEI.bin] [--ja]\n               [--kurve DATEI] [--halbton hp|fs|jarvis|stucki] [--kontrast N [--parameter DATEI]] [--trocken [--laufwerk-id N] [--medium N]]\n"); return 1; }
    { char de[400]; if (daten_laden(de, sizeof de)) { fprintf(stderr, "%s\n", de); return 4; } }
    spur_opt o; spur_opt_standard(&o);
    const char *dev = geraet_standard(), *logp = NULL, *kurve = NULL, *ptab = NULL; int ja = 0, q_gesetzt = 0, kontrast = -1, t_lw = 0, t_md = 112;
    uint8_t lut[256]; char err[256];
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-q") && i + 1 < argc) { o.tpi = qualitaet_tpi(argv[++i]); q_gesetzt = 1; if (!o.tpi) { fprintf(stderr, "Qualitaet: draft, normal oder best\n"); return 1; } }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) { i++; o.fit = strcmp(argv[i], "nofit") && strcmp(argv[i], "no_fit"); }
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) o.dpi_ersatz = atof(argv[++i]);
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) { if (sscanf(argv[++i], "%lf:%lf", &o.band_von_mm, &o.band_bis_mm) != 2) { fprintf(stderr, "Band: z.B. -b 31.4:36.5\n"); return 1; } }
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) dev = argv[++i];
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) logp = argv[++i];
        else if (!strcmp(argv[i], "--ja")) ja = 1;
        else if (!strcmp(argv[i], "--stopdatei") && i + 1 < argc) brennen_stopdatei_setzen(argv[++i]);
        else if (!strcmp(argv[i], "--medium") && i + 1 < argc) { o.medium_ersatz = atoi(argv[++i]); t_md = o.medium_ersatz; }
        else if (!strcmp(argv[i], "--kontrast") && i + 1 < argc) kontrast = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--parameter") && i + 1 < argc) ptab = argv[++i];
        else if (!strcmp(argv[i], "--trocken")) t_lw = t_lw ? t_lw : 199;
        else if (!strcmp(argv[i], "--laufwerk-id") && i + 1 < argc) t_lw = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--kurve") && i + 1 < argc) { kurve = argv[++i]; if (lut_laden(kurve, lut, err)) { fprintf(stderr, "%s\n", err); return 1; } o.lut = lut; }
        else if (!strcmp(argv[i], "--halbton") && i + 1 < argc) { i++; if (!strcmp(argv[i], "hp")) o.halbton = 3; else if (!strcmp(argv[i], "fs")) o.halbton = 0; else if (!strcmp(argv[i], "jarvis")) o.halbton = 1; else if (!strcmp(argv[i], "stucki")) o.halbton = 2; else { fprintf(stderr, "Halbton: hp, fs, jarvis oder stucki\n"); return 1; } }
        else { fprintf(stderr, "Unbekannte Option: %s\n", argv[i]); return 1; }
    }
    spurliste s;
    if (kurve || o.halbton != 3) {
        if (endet_mit(argv[2], ".lsp")) printf("Hinweis: --kurve/--halbton wirken nur bei Bilddateien, nicht bei .lsp.\n");
    }
    if (kontrast >= 0) brennen_kontrast_setzen(kontrast, ptab);
    if (t_lw) brennen_trocken_setzen(t_lw, t_md);
    if (endet_mit(argv[2], ".lsp")) {
        if (q_gesetzt) printf("Hinweis: -q wird bei einer .lsp-Datei ignoriert (Qualitaet steht in der Datei).\n");
        if (spuren_laden(argv[2], &s, &o, err)) { fprintf(stderr, "%s\n", err); return 1; }
    } else if (endet_mit(argv[2], ".lsd")) {
        if (q_gesetzt || kontrast >= 0) printf("Hinweis: -q/--kontrast werden bei einer LightScribe-Druckdatei (.lsd) ignoriert (alles steht in der Datei).\n");
        if (kontrast >= 0) brennen_kontrast_setzen(-1, NULL);
        if (spuren_laden_lsd(argv[2], &s, &o, err)) { fprintf(stderr, "%s\n", err); return 1; }
        printf("LightScribe-Druckdatei (.lsd): %d Spuren, %d Spuren/Zoll, Dichte %.3f\n", s.n, o.tpi, s.dichte);
    } else {
        bild b;
        if (bild_laden(argv[2], &b, err)) { fprintf(stderr, "Bild: %s\n", err); return 1; }
        if (spuren_erzeugen(&b, &o, &s, err)) { fprintf(stderr, "%s\n", err); return 1; }
        free(b.px);
    }
    if (logp) sg_log_oeffnen(logp);
    int r = brennen(dev, &s, &o, ja);
    sg_log_schliessen();
    spurliste_frei(&s);
    return r;
}

int main(int argc, char **argv) {
    pf_start(&argc, &argv);
    const char *cmd = argc > 1 ? argv[1] : "hilfe";
    const char *dev = argc > 2 ? argv[2] : geraet_standard();
    if (!strcmp(cmd, "einrichten")) return cmd_einrichten(argc, argv);
    if (!strcmp(cmd, "suchen")) return cmd_suchen();
    if (!strcmp(cmd, "info")) return cmd_info(dev, 0);
    if (!strcmp(cmd, "roh")) return cmd_info(dev, 1);
    if (!strcmp(cmd, "spuren")) return cmd_spuren(argc, argv);
    if (!strcmp(cmd, "vorschau")) return cmd_vorschau(argc, argv);
    if (!strcmp(cmd, "brennen")) return cmd_brennen(argc, argv);
    if (!strcmp(cmd, "version")) { printf("ls64 %s – nativ %d-bit\n", VERSION, (int)(sizeof(void *) * 8)); return 0; }
    printf("ls64 %s – nativer LightScribe-Treiber\n\n"
           "  ls64 einrichten [QUELLE]  einmalig: Tabellen aus der LightScribe System Software lesen\n"
           "                           (QUELLE: liblightscribe.so.1, .deb, .rpm oder LSPrtEn.dll; ohne = automatisch)\n"
           "  ls64 suchen              optische Laufwerke pruefen\n"
           "  ls64 info [LAUFWERK]     Laufwerk und Disc anzeigen (Linux /dev/sr0, Windows E:)\n"
           "  ls64 roh  [LAUFWERK]     wie info, plus Rohdaten\n"
           "  ls64 spuren BILD DATEI.lsp [Optionen]   Spurdaten berechnen (offline)\n"
           "        -q draft|normal|best   Qualitaet (Standard: normal)\n"
           "        -s fit|nofit           Bild auf Disc skalieren oder Originalgroesse\n"
           "        -b VON:BIS             nur Ring zwischen VON und BIS mm Radius\n"
           "        -v VORSCHAU.png        Vorschaubild schreiben\n"
           "        --kurve DATEI          Tonwertkurve (256 Werte oder 'ein aus'-Paare) vor der Halbtonung\n"
           "        --halbton hp|fs|jarvis|stucki   Halbtonung (Standard: hp)\n"
           "  ls64 vorschau DATEI.lsp BILD.png         Vorschau aus Spurdaten\n"
           "  ls64 brennen BILD|DATEI.lsp|DATEI.lsd [Optionen] Label brennen (Optionen wie 'spuren', dazu:)\n"
           "        -g LAUFWERK            Laufwerk (Linux /dev/sr0, Windows E:)\n"
           "        --stopdatei DATEI      Brennen sauber abbrechen, sobald DATEI existiert\n"
           "        --log DATEI.bin        alle Laufwerksbefehle protokollieren\n"
           "        --ja                   ohne Rueckfrage starten\n"
                      "        --trocken              nichts senden, nur Parameter zeigen (--laufwerk-id N, --medium N)\n"
                      "  ls64 version\n\n"
           "Nur 'brennen' schreibt auf die Disc; alle anderen Befehle lesen nur.\n", VERSION);
    return 0;
}
