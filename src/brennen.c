/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
/*
 * brennen.c – Spurpakete an den LightScribe-Brenner senden.
 * Ablauf wie bei der Original-Software (ermittelt aus aufgezeichnetem Laufwerksverkehr):
 *
 *  1  Pruefen: LightScribe-Laufwerk, Disc mit Labelseite unten
 *  2  PREVENT MEDIUM REMOVAL            1e 00 00 00 01 00
 *  3  MODE SENSE Seite 0x31, Spurabstand (nm) in Bytes 23-24 eintragen,
 *     MODE SELECT(10)                   55 10 00 00 00 00 00 00 40 00
 *  4  START UNIT                        1b 00 00 00 01 00
 *  5  je Paket: READ BUFFER CAPACITY    5c 00 00 00 00 00 00 00 0c 00
 *        Sense 2/04/08 (Puffer voll) -> 100 ms warten
 *        frei >= 2048                   -> Paket senden: fd 02 00 00 00 00 00 LL LL 00
 *     ca. 1x pro Sekunde REQUEST SENSE  03 00 00 00 fc 00  (Fortschritt: Bytes 4-5 = Spur)
 *  6  Endmarke (16 Byte, Byte 3 = 0x80) wie ein Paket senden
 *  7  warten bis REQUEST SENSE Byte 16 = 0x10 und Puffer leer
 *  8  STOP UNIT 1b 00 00 00 00 00, ALLOW 1e 00 00 00 00 00
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "ls64.h"
#include "plattform.h"

static volatile int abbruch;
static char g_stop[1100];
void brennen_stopdatei_setzen(const char *pfad) { snprintf(g_stop, sizeof g_stop, "%s", pfad ? pfad : ""); }
static void stop_pruefen(void) { if (g_stop[0] && pf_existiert(g_stop)) { abbruch = 1; pf_loeschen(g_stop); } }
static int gleich_ci(const char *a, const char *b) { while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; } return *a == *b; }

static double jetzt(void) { return pf_zeit(); }
static void warte_ms(int ms) { pf_warte_ms(ms); }
static unsigned be32(const uint8_t *p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void zeit(char *b, double s) { if (s < 0) s = 0; snprintf(b, 16, "%d:%02d", (int)s / 60, (int)s % 60); }

#define MARKEN_PRO_S 5400.0   /* gemessen: Laufwerk brennt ~5400 Marken/s bei 275 mm/s */

typedef struct {
    int fd;
    int erste, letzte;        /* Spurnummern */
    int n; const int *nr; const double *kum;   /* je gesendeter Spur: Nummer, Marken kumuliert bis inkl. */
    double gesamt;            /* Marken insgesamt */
    double start, t_erste;    /* Startzeit, Zeitpunkt der ersten gemeldeten Spur */
    int spur_aktuell;
    int gueltig;              /* letzte REQUEST-SENSE-Antwort enthielt Spurangabe */
    int fertig;               /* Byte 16 = 0x10 wurde gemeldet (bleibt gesetzt) */
    double letzte_abfrage;
    double marken_pro_s;
} lauf;

static double marken_bis(const lauf *L, int spur) {     /* Marken der Spuren <= spur */
    int lo = 0, hi = L->n - 1, k = -1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (L->nr[m] <= spur) { k = m; lo = m + 1; } else hi = m - 1; }
    return k < 0 ? 0 : L->kum[k];
}

static void fortschritt(lauf *L, int erzwingen) {
    stop_pruefen();
    if (!erzwingen && jetzt() - L->letzte_abfrage < 1.0) return;
    L->letzte_abfrage = jetzt();
    static const uint8_t rs[6] = {0x03, 0, 0, 0, 0xfc, 0};
    uint8_t b[252];
    sg_res r = sg_exec(L->fd, rs, 6, 1, b, 252, 10000);
    if (!r.ok || r.len < 17) return;
    L->gueltig = (b[0] & 0x7f) == 0x70 && (b[0] & 0x80);
    if (L->gueltig) {
        L->spur_aktuell = b[4] << 8 | b[5];
        if (!L->t_erste) L->t_erste = jetzt();
        if (b[16] == 0x10) L->fertig = 1;
    }
    double vorbei = jetzt() - L->start;
    double p = L->fertig ? 1.0 : (L->spur_aktuell >= L->erste ? marken_bis(L, L->spur_aktuell) / L->gesamt : 0);
    if (p > 1) p = 1;
    /* Restzeit: gemessene Geschwindigkeit, sobald genug Daten da sind, sonst Erfahrungswert */
    double rate = L->marken_pro_s;
    if (L->t_erste && p > 0.05 && jetzt() - L->t_erste > 20) rate = p * L->gesamt / (jetzt() - L->t_erste);
    double rest = (1 - p) * L->gesamt / rate;
    char a[16], c[16]; zeit(a, vorbei); zeit(c, rest);
    if (!L->t_erste)
        printf("\rBrenne:   0 %%  Laufwerk laeuft an / sucht Startposition ...  vergangen %s   ", a);
    else
        printf("\rBrenne: %3d %%  (Spur %d von %d–%d)  vergangen %s  Rest ca. %s   ",
               (int)(p * 100 + 0.5), L->spur_aktuell, L->erste, L->letzte, a, c);
    fflush(stdout);
}

/* Paket senden, vorher auf Platz im Laufwerkspuffer warten. 0 = ok */
static int sende(lauf *L, const uint8_t *d, uint32_t len) {
    static const uint8_t cap[10] = {0x5c, 0, 0, 0, 0, 0, 0, 0, 0x0c, 0};
    double seit = jetzt();
    for (;;) {
        uint8_t b[12];
        sg_res r = sg_exec(L->fd, cap, 10, 1, b, 12, 60000);
        if (r.ok && r.len >= 12 && be32(b + 8) >= 2048) break;
        if (!r.ok && !(r.key == 2 && r.asc == 4 && r.ascq == 8)) {
            fprintf(stderr, "\nFehler bei READ BUFFER CAPACITY (Sense %x/%02x/%02x)\n", r.key, r.asc, r.ascq);
            return -1;
        }
        fortschritt(L, 0);
        if (jetzt() - seit > 300) { fprintf(stderr, "\nLaufwerk nimmt seit 5 Minuten keine Daten an – Abbruch.\n"); return -1; }
        warte_ms(100);
    }
    uint8_t cdb[10] = {0xfd, 0x02, 0, 0, 0, 0, 0, (uint8_t)(len >> 8), (uint8_t)len, 0};
    sg_res r = sg_exec(L->fd, cdb, 10, 2, (uint8_t *)d, len, 60000);
    if (!r.ok) { fprintf(stderr, "\nFehler beim Senden der Spurdaten (Sense %x/%02x/%02x, Status %d)\n", r.key, r.asc, r.ascq, r.status); return -1; }
    fortschritt(L, 0);
    return 0;
}

static int einfach(int fd, uint8_t a, uint8_t b4, int timeout, const char *was) {
    uint8_t cdb[6] = {a, 0, 0, 0, b4, 0};
    sg_res r = sg_exec(fd, cdb, 6, 0, NULL, 0, timeout);
    if (!r.ok) fprintf(stderr, "Hinweis: %s fehlgeschlagen (Sense %x/%02x/%02x)\n", was, r.key, r.asc, r.ascq);
    return r.ok ? 0 : -1;
}


/* ---- Optional: Laserparameter aus Tabelle (--kontrast N [--parameter DATEI]) ----
 * Textdatei, '#' = Kommentar. Erste nichtleere Zeile mit Buchstaben (auch nach '#' erlaubt: "# qualitaet kontrast ...")
 * ist die Kopfzeile mit Spaltennamen; fehlt sie, gilt die Reihenfolge
 *   qualitaet kontrast geschwindigkeit leistung fokus [lese]
 * Erkannte Spaltennamen: qualitaet|quality, kontrast|contrast, geschwindigkeit|tempo|speed (mm/s),
 * leistung|schreib|schreibleistung|power, lese|leseleistung|read, fokus|focus. Weitere Spalten (z. B. tpi)
 * werden ignoriert. "-" oder leer = Wert der Laufwerkstabelle behalten. */
static int g_kontrast = -1; static const char *g_tabelle = "kontrast_parameter.txt";
static int g_trocken_lw = 0, g_trocken_md = 0;
void brennen_kontrast_setzen(int k, const char *tabelle) { g_kontrast = k; if (tabelle) g_tabelle = tabelle; }
void brennen_trocken_setzen(int lw, int md) { g_trocken_lw = lw; g_trocken_md = md; }

enum { C_Q, C_K, C_V, C_P, C_F, C_L, C_X, C_N };
static int spalte_von_name(const char *w) {
    static const struct { const char *n; int c; } t[] = {
        {"qualitaet", C_Q}, {"quality", C_Q}, {"kontrast", C_K}, {"contrast", C_K},
        {"geschwindigkeit", C_V}, {"tempo", C_V}, {"speed", C_V}, {"leistung", C_P}, {"schreib", C_P},
        {"schreibleistung", C_P}, {"power", C_P}, {"lese", C_L}, {"leseleistung", C_L}, {"read", C_L},
        {"fokus", C_F}, {"focus", C_F}};
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) if (gleich_ci(w, t[i].n)) return t[i].c;
    return C_X;
}
static int quali_passt(const char *w, int tpi) {
    static const struct { const char *n[3]; int tpi; } q[] = {{{"draft", "entwurf", "760"}, 760}, {{"normal", "medium", "1015"}, 1015}, {{"best", "beste", "1398"}, 1398}};
    for (int i = 0; i < 3; i++) for (int k = 0; k < 3; k++) if (gleich_ci(w, q[i].n[k])) return q[i].tpi == tpi;
    return 0;
}
/* Sucht die Zeile (qualitaet, kontrast). v[]: Geschwindigkeit mm/s, Schreibleistung, Leseleistung, Fokus; NAN = nicht angegeben */
static int tabelle_suchen(const char *datei, int tpi, int kontrast, double v[4]) {
    FILE *f = fopen(datei, "r");
    if (!f) { fprintf(stderr, "Parametertabelle '%s' nicht lesbar.\n", datei); return -1; }
    int sp[32], ns = 6; for (int i = 0; i < 32; i++) sp[i] = C_X;
    sp[0] = C_Q; sp[1] = C_K; sp[2] = C_V; sp[3] = C_P; sp[4] = C_F; sp[5] = C_L;
    char z[1024]; int gef = 0;
    while (fgets(z, sizeof z, f)) {
        char *p = z; while (*p == ' ' || *p == '\t' || *p == '#') p++;
        int kopf = z[strspn(z, " \t")] == '#';
        char *tok[32]; int nt = 0;
        for (char *c = p, *sv; nt < 32 && (c = strtok_r(c, " \t\r\n;,", &sv)); c = NULL) tok[nt++] = c;
        if (!nt) continue;
        int alpha = 0; for (int i = 0; i < nt; i++) if (spalte_von_name(tok[i]) != C_X) alpha++;
        if (alpha >= 2) {                                    /* Kopfzeile */
            for (int i = 0; i < 32; i++) sp[i] = C_X;
            for (int i = 0; i < nt; i++) sp[i] = spalte_von_name(tok[i]);
            ns = nt; continue;
        }
        if (kopf) continue;                                  /* sonstiger Kommentar */
        const char *q = NULL, *kt = NULL; double w[8]; for (int i = 0; i < 8; i++) w[i] = NAN;
        for (int i = 0; i < nt && i < ns; i++) {
            if (sp[i] == C_Q) q = tok[i]; else if (sp[i] == C_K) kt = tok[i];
            else if (sp[i] != C_X && strcmp(tok[i], "-")) w[sp[i]] = atof(tok[i]);
        }
        if (!q || !kt || !quali_passt(q, tpi) || atoi(kt) != kontrast) continue;
        v[0] = w[C_V]; v[1] = w[C_P]; v[2] = w[C_L]; v[3] = w[C_F]; gef = 1; break;
    }
    fclose(f);
    if (!gef) fprintf(stderr, "Parametertabelle '%s': keine Zeile fuer %d Spuren/Zoll, Kontrast %d.\n", datei, tpi, kontrast);
    return gef ? 0 : -1;
}

/* Laufwerkswerte (mp) mit Tabellenwerten ueberschreiben. 0 = ok. tempo_mm: Geschwindigkeit in mm/s (auch gebrochen) */
static int tabelle_anwenden(drf_param *mp, double *tempo_mm, int tpi) {
    double v[4];
    if (tabelle_suchen(g_tabelle, tpi, g_kontrast, v)) return -1;
    double alt[4] = {mp->tempo, mp->schreib, mp->lese, mp->fokus};
    const char *nm[4] = {"Geschwindigkeit mm/s", "Schreibleistung", "Leseleistung", "Fokus"};
    for (int i = 0; i < 4; i++) if (!isnan(v[i])) {
        double lo = i == 3 ? -128 : i == 0 ? 20 : 1, hi = i == 3 ? 127 : i == 0 ? 1000 : 255;
        if (v[i] < lo || v[i] > hi) { fprintf(stderr, "Parametertabelle: %s = %g ausserhalb %g..%g – abgebrochen.\n", nm[i], v[i], lo, hi); return -1; }
        if (i != 3 && fabs(v[i] - alt[i]) > 0.5 * alt[i]) printf("WARNUNG: %s %g weicht um mehr als 50 %% vom Laufwerkswert %g ab.\n", nm[i], v[i], alt[i]);
    }
    if (!isnan(v[0])) { *tempo_mm = v[0]; mp->tempo = (short)lround(v[0]); }
    if (!isnan(v[1])) mp->schreib = (short)lround(v[1]);
    if (!isnan(v[2])) mp->lese = (short)lround(v[2]);
    if (!isnan(v[3])) mp->fokus = (short)lround(v[3]);
    printf("Parameter : aus Tabelle %s (Spuren/Zoll %d, Kontrast %d): Fokus %d, Schreib %d, Lese %d, %g mm/s\n"
           "            (Laufwerkstabelle: Fokus %d, Schreib %d, Lese %d, %g mm/s)\n", g_tabelle, tpi, g_kontrast,
           mp->fokus, mp->schreib, mp->lese, *tempo_mm, (int)alt[3], (int)alt[1], (int)alt[2], alt[0]);
    return 0;
}

int brennen(const char *dev, const spurliste *s, const spur_opt *o, int ohne_rueckfrage) {
    if (s->n == 0) { printf("Das Bild ist leer – es gibt nichts zu brennen.\n"); return 0; }
    if (g_trocken_lw > 0) {          /* Trockenlauf: kein Laufwerk, nur Parameter zeigen */
        const drf_param *t = drf_suchen(g_trocken_lw, g_trocken_md);
        if (!t) { printf("Trockenlauf: keine Laserparameter fuer Laufwerk %d / Medium %d.\n", g_trocken_lw, g_trocken_md); return 1; }
        drf_param l = *t; double tm = l.tempo;
        printf("Trockenlauf (kein Laufwerk, es wird nichts gesendet): %d Spuren, %d Spuren/Zoll\n", s->n, o->tpi);
        printf("Laufwerkstabelle: Fokus %d, Schreib %d, Lese %d, %d mm/s\n", l.fokus, l.schreib, l.lese, l.tempo);
        if (g_kontrast >= 0 && tabelle_anwenden(&l, &tm, o->tpi)) return 1;
        const lsd_param *lp = lsd_parameter();
        if (lp) { printf("Laserparameter aus LightScribe-Druckdatei (Laufwerk %d / Medium %d).\n", lp->laufwerk, lp->medium);
                  l.fokus = lp->fokus; l.schreib = lp->schreib; l.lese = lp->lese; tm = lp->tempo_um / 1000.0; }
        printf("MODE SELECT Seite 0x31 wuerde setzen: [0x10]=%d [0x11]=%d [0x12]=%d [0x19..1B]=%u um/s [0x17..18]=%u nm\n",
               l.fokus, l.lese, l.schreib, (unsigned)lround(tm * 1000), (unsigned)lround(25.4e6 / o->tpi));
        return 0;
    }
    int fd = open_drive(dev); if (fd < 0) return 1;
    drive_info d;
    if (read_all(fd, &d) < 0 || !d.ls_feature) { fprintf(stderr, "%s ist kein LightScribe-Laufwerk.\n", dev); close_drive(fd); return 1; }
    ls_status st = decode(&d);
    if (!(st.oriented && d.ls_current)) {
        fprintf(stderr, "Disc nicht bereit (eingelegt: %s, LightScribe: %s, Labelseite unten: nein).\n"
                        "Bitte 'ls64 info %s' pruefen.\n", st.media_present ? "ja" : "nein", st.ls_media ? "ja" : "nein", dev);
        close_drive(fd); return 1;
    }
    if (st.media_param1 && st.media_param1 != (unsigned)o->param400)
        printf("Hinweis: Medienparameter der Disc %u statt %d – verwende den Wert der Disc.\n", st.media_param1, o->param400);
    uint16_t param = st.media_param1 ? (uint16_t)st.media_param1 : (uint16_t)o->param400;

    /* Laser-/Geschwindigkeitsparameter fuer dieses Laufwerk und diese Disc */
    int lw_id = d.p31_len > 0x0c ? LAUFWERK_ID(d.p31) : -1, md_id = d.p32_len > 2 ? MEDIEN_ID(d.p32) : -1;
    if (o->medium_ersatz) md_id = o->medium_ersatz;
    const drf_param *mp = drf_suchen(lw_id, md_id);
    if (mp && (mp->tempo <= 0 || mp->schreib <= 0)) mp = NULL;
    double marken_pro_s = MARKEN_PRO_S;
    drf_param mpk; double tempo_mm = 0;      /* lokale Kopie, falls Tabellenwerte (--kontrast) gelten */
    if (mp) {
        mpk = *mp; mp = &mpk; tempo_mm = mp->tempo;
        if (g_kontrast >= 0 && tabelle_anwenden(&mpk, &tempo_mm, o->tpi)) { close_drive(fd); return 1; }
    } else if (g_kontrast >= 0) {
        fprintf(stderr, "--kontrast: keine Laufwerkstabelle fuer Laufwerk %d / Medium %d – Tabelle wird nicht angewendet, Abbruch.\n", lw_id, md_id);
        close_drive(fd); return 1;
    }
    const lsd_param *lp = lsd_parameter();
    if (lp) {                               /* LightScribe-Druckdatei: deren Laserparameter verwenden */
        if (!mp) { memset(&mpk, 0, sizeof mpk); mpk.mpi = 600; for (int k = 0; k < 4; k++) mpk.tpi[k] = o->tpi; mp = &mpk; }
        printf("Laserparameter aus LightScribe-Druckdatei: Fokus %d, Schreib %d, Lese %d, %.1f mm/s, Spurabstand %u nm, Markenabstand %u nm\n"
               "            (Laufwerkstabelle: Fokus %d, Schreib %d, Lese %d, %g mm/s)\n",
               lp->fokus, lp->schreib, lp->lese, lp->tempo_um / 1000.0, lp->spur_nm, lp->marke_nm, mpk.fokus, mpk.schreib, mpk.lese, tempo_mm);
        if (lp->laufwerk != lw_id || lp->medium != md_id)
            printf("Hinweis: Druckdatei wurde fuer Laufwerk %d / Medium %d erzeugt, hier: Laufwerk %d / Medium %d.\n", lp->laufwerk, lp->medium, lw_id, md_id);
        mpk.fokus = lp->fokus; mpk.schreib = lp->schreib; mpk.lese = lp->lese; tempo_mm = lp->tempo_um / 1000.0;
    }
    if (mp) {
        marken_pro_s = 0.83 * tempo_mm * 1000.0 / (25400.0 / mp->mpi);   /* 0,83 = gemessener Wirkungsgrad */
        int ok_tpi = 0; for (int k = 0; k < 4; k++) ok_tpi |= mp->tpi[k] == o->tpi;
        if (!ok_tpi) printf("Hinweis: %d Spuren/Zoll ist fuer diese Disc nicht vorgesehen (erlaubt: %d/%d/%d/%d).\n",
                            o->tpi, mp->tpi[0], mp->tpi[1], mp->tpi[2], mp->tpi[3]);
    } else {
        printf("WARNUNG: Keine Laserparameter fuer Laufwerk %d / Medium %d bekannt – es werden nur die\n"
               "         aktuellen Laufwerkswerte verwendet (Label kann blasser werden).\n", lw_id, md_id);
    }

    int erste = s->p[0].data[0] << 8 | s->p[0].data[1], letzte = s->p[s->n - 1].data[0] << 8 | s->p[s->n - 1].data[1];
    double pitch = 25.4 / o->tpi;
    double marken = 0;
    int *nr = malloc(sizeof(int) * s->n); double *kum = malloc(sizeof(double) * s->n);
    for (int i = 0; i < s->n; i++) {
        nr[i] = s->p[i].data[0] << 8 | s->p[i].data[1];
        marken += s->p[i].data[14] << 8 | s->p[i].data[15];
        kum[i] = marken;
    }
    char dauer[16]; zeit(dauer, marken / marken_pro_s + 30);
    printf("Laufwerk  : %s %s auf %s\n", d.vendor, d.model, dev);
    printf("Brennen   : %d Spuren, Radius %.1f–%.1f mm, %d Spuren/Zoll\n", s->n,
           o->r_innen_mm + (erste + 0.5) * pitch, o->r_innen_mm + (letzte + 0.5) * pitch, o->tpi);
    if (mp) printf("Parameter : Laufwerk-ID %d, Medium %d: Fokus %d, Schreibleistung %d, %g mm/s\n", lw_id, md_id, mp->fokus, mp->schreib, tempo_mm);
    printf("Dauer     : ca. %s min\n", dauer);
    if (!ohne_rueckfrage) {
        printf("\nWaehrend des Brennens die Disc nicht auswerfen und den Rechner nicht in Standby schicken.\n"
               "Zum Starten 'ja' eingeben: ");
        fflush(stdout);
        char a[16] = {0};
        if (!fgets(a, sizeof a, stdin) || strncmp(a, "ja", 2)) { printf("Abgebrochen – nichts gebrannt.\n"); free(nr); free(kum); close_drive(fd); return 0; }
    }

    pf_abbruch_einrichten(&abbruch);
    if (g_stop[0]) pf_loeschen(g_stop);
    pf_wach_halten(1);

    int fehler = 0, abgebrochen = 0;
    lauf L; memset(&L, 0, sizeof L);
    einfach(fd, 0x1e, 1, 10000, "Fach sperren");

    /* Laufwerks-/Medienparameter in Seite 0x31 setzen (wie die Original-Software):
     *   0x10 Fokus-Offset (signed), 0x11 Leseleistung, 0x12 Schreibleistung,
     *   0x14-15 Markenabstand nm, 0x17-18 Spurabstand nm, 0x19-1B Schreibgeschwindigkeit um/s */
    {
        static const uint8_t ms[10] = {0x5a, 0x08, 0x31, 0, 0, 0, 0, 0x02, 0x00, 0x01};
        uint8_t p[512];
        sg_res r = sg_exec(fd, ms, 10, 1, p, 512, 10000);
        if (!r.ok || r.len < 64 || p[8] != 0x31) { fprintf(stderr, "Seite 0x31 nicht lesbar.\n"); fehler = 1; goto ende; }
        unsigned nm = (unsigned)lround(25.4e6 / o->tpi);
        p[23] = nm >> 8; p[24] = nm & 0xff;
        if (mp) {
            unsigned mnm = (unsigned)lround(25.4e6 / mp->mpi), v = (unsigned)lround(tempo_mm * 1000);
            p[0x10] = (uint8_t)(int8_t)mp->fokus; p[0x11] = (uint8_t)mp->lese; p[0x12] = (uint8_t)mp->schreib;
            p[0x14] = mnm >> 8; p[0x15] = mnm & 0xff;
            p[0x19] = v >> 16; p[0x1a] = v >> 8; p[0x1b] = v & 0xff;
        }
        static const uint8_t sel[10] = {0x55, 0x10, 0, 0, 0, 0, 0, 0, 0x40, 0};
        r = sg_exec(fd, sel, 10, 2, p, 64, 10000);
        if (!r.ok) { fprintf(stderr, "Laufwerksparameter konnten nicht gesetzt werden (Sense %x/%02x/%02x).\n", r.key, r.asc, r.ascq); fehler = 1; goto ende; }
        uint8_t q[512];
        r = sg_exec(fd, ms, 10, 1, q, 512, 10000);
        if (!r.ok || memcmp(q + 0x10, p + 0x10, 12)) { fprintf(stderr, "Laufwerksparameter wurden nicht uebernommen.\n"); fehler = 1; goto ende; }
    }

    printf("\nStarte Laufwerk ...\n");
    if (einfach(fd, 0x1b, 1, 60000, "Laufwerk starten")) { fehler = 1; goto ende; }

    L.fd = fd; L.erste = erste; L.letzte = letzte; L.n = s->n; L.nr = nr; L.kum = kum; L.gesamt = marken; L.start = jetzt(); L.marken_pro_s = marken_pro_s;
    for (int i = 0; i < s->n && !abbruch; i++) {
        const paket *pk = &s->p[i];
        uint8_t *dd = pk->data;
        dd[12] = param >> 8; dd[13] = param & 0xff;
        if (sende(&L, dd, pk->len)) { fehler = 1; break; }
    }
    abgebrochen = abbruch;
    if (abbruch) printf("\nAbbruch angefordert – bereits gesendete Spuren werden noch fertig gebrannt, dann wird das Laufwerk sauber beendet ...\n");

    /* Endmarke senden (auch bei Abbruch, damit das Laufwerk sauber abschliesst) */
    uint8_t em[16]; endmarke(em);
    if (!fehler || abbruch) {
        abbruch = 0;
        if (sende(&L, em, 16)) fehler = 1;
    }
    /* Warten bis fertig */
    if (!fehler) {
        double t0 = jetzt(); int alte = -1; double seit = jetzt(); int leerlauf = 0;
        for (;;) {
            fortschritt(&L, 1);
            static const uint8_t cap[10] = {0x5c, 0, 0, 0, 0, 0, 0, 0, 0x0c, 0};
            uint8_t b[12]; sg_res r = sg_exec(fd, cap, 10, 1, b, 12, 60000);
            int leer = r.ok && r.len >= 12 && be32(b + 8) == be32(b + 4);
            /* Fertig: "0x10" wurde gemeldet (kann danach wieder verschwinden, Test 7c)
               oder Laufwerk ist im Leerlauf (keine Spurangabe mehr) und der Puffer leer */
            if (leer && L.fertig) break;
            leerlauf = (leer && !L.gueltig) ? leerlauf + 1 : 0;
            if (leerlauf >= 3) { L.fertig = 1; break; }
            if (L.spur_aktuell != alte) { alte = L.spur_aktuell; seit = jetzt(); }
            if (jetzt() - seit > 300) { fprintf(stderr, "\nKein Fortschritt seit 5 Minuten – Abbruch.\n"); fehler = 1; break; }
            if (abbruch && jetzt() - t0 > 2) { printf("\nWarte auf Laufwerk (Spuren im Puffer werden noch gebrannt) ...\n"); abbruch = 0; }
            warte_ms(1000);
        }
    }
    if (!fehler && L.fertig) { fortschritt(&L, 1); }
ende:
    free(nr); free(kum);
    einfach(fd, 0x1b, 0, 60000, "Laufwerk stoppen");
    einfach(fd, 0x1e, 0, 10000, "Fach freigeben");
    close_drive(fd);
    pf_wach_halten(0);
    printf("\n%s\n", fehler ? "Brennen mit Fehler beendet." : abgebrochen ? "Brennen abgebrochen (Label unvollstaendig)." : "Fertig! Das Label ist gebrannt.");
    return fehler ? 2 : abgebrochen ? 3 : 0;
}
