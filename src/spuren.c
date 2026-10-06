/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
/*
 * spuren.c – aus einem Bild die LightScribe-Spurpakete berechnen.
 * Teil von ls64. Die Regeln wurden aus aufgezeichnetem Laufwerksverkehr der Original-Software ermittelt:
 *
 *  - Spur n liegt bei r = r_innen + (n + 0,5) * 25,4 mm / TPI   (Spurmitte)
 *  - Anzahl Spuren = floor((r_aussen - r_innen) * TPI / 25,4)
 *  - Marken entlang der Spur mit 600 dpi: U = round(2*pi*r / 42,333 um)  (98,4 % exakt wie Original, sonst +-1)
 *  - Bit i liegt beim Winkel 2*pi*i/U, Bit 0 = 3 Uhr, zunehmend Richtung 6 Uhr
 *    (Bildkoordinaten: x nach rechts, y nach unten, Ursprung Discmitte)
 *  - Paket: 16 Byte Kopf [Spur(2) | 0 x10 | Param 400 (2) | U (2)] + U Bits MSB zuerst,
 *    aufgefuellt auf 32-Bit; 1 = brennen
 *  - Markendichte = 1 - Grauwert/255 (Fehlerdiffusion), leere Spuren werden weggelassen
 *  - fit: Bildbreite/-hoehe (groessere Seite) = 2 * r_aussen; sonst Bild-DPI
 *  - Endmarke: 16 Byte, Byte 3 = 0x80
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ls64.h"

void spur_opt_standard(spur_opt *o) {
    memset(o, 0, sizeof *o);
    o->tpi = 1015; o->r_innen_mm = 23.8; o->r_aussen_mm = 58.7;
    o->fit = 1; o->dpi_ersatz = 300; o->param400 = 400; o->halbton = 3;   /* Standard: Halbtonung wie Original-Engine */
}

void endmarke(uint8_t out[16]) { memset(out, 0, 16); out[3] = 0x80; }

static void add(spurliste *s, uint8_t *d, uint32_t len) {
    if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 256; s->p = realloc(s->p, sizeof(paket) * s->cap); }
    s->p[s->n].data = d; s->p[s->n].len = len; s->n++;
}

void spurliste_frei(spurliste *s) {
    for (int i = 0; i < s->n; i++) free(s->p[i].data);
    free(s->p); memset(s, 0, sizeof *s);
}

/* Bilinear abtasten; ausserhalb des Bildes = weiss. Rueckgabe: Schwaerzung 0..1 */
static double schwaerzung(const bild *b, double x, double y) {
    if (x < 0 || y < 0 || x > b->w - 1 || y > b->h - 1) return 0.0;
    int x0 = (int)x, y0 = (int)y; int x1 = x0 + 1 < b->w ? x0 + 1 : x0, y1 = y0 + 1 < b->h ? y0 + 1 : y0;
    double fx = x - x0, fy = y - y0;
    const uint8_t *p = b->px;
    double g = (1 - fy) * ((1 - fx) * p[y0 * b->w + x0] + fx * p[y0 * b->w + x1])
             + fy * ((1 - fx) * p[y1 * b->w + x0] + fx * p[y1 * b->w + x1]);
    return 1.0 - g / 255.0;
}

static int spur_u(double r_mm) { return (int)lround(2 * M_PI * r_mm / 0.042333); }

/* Fehlerdiffusions-Kerne. dy = Anzahl Spuren weiter (0 = gleiche Spur), dx = Marken in Laufrichtung.
 * Reihenfolge der Eintraege = Reihenfolge der Addition (Floyd-Steinberg exakt wie zuvor). */
typedef struct { int dy, dx, w; } kern_e;
static const kern_e K_FS[] = {{0, 1, 7}, {1, -1, 3}, {1, 0, 5}, {1, 1, 1}};
static const kern_e K_JARVIS[] = {{0, 1, 7}, {0, 2, 5},
    {1, -2, 3}, {1, -1, 5}, {1, 0, 7}, {1, 1, 5}, {1, 2, 3},
    {2, -2, 1}, {2, -1, 3}, {2, 0, 5}, {2, 1, 3}, {2, 2, 1}};
static const kern_e K_STUCKI[] = {{0, 1, 8}, {0, 2, 4},
    {1, -2, 2}, {1, -1, 4}, {1, 0, 8}, {1, 1, 4}, {1, 2, 2},
    {2, -2, 1}, {2, -1, 2}, {2, 0, 4}, {2, 1, 2}, {2, 2, 1}};

int spuren_erzeugen_hp(const bild *b0, const spur_opt *o, spurliste *s, char *err);
int spuren_erzeugen(const bild *b0, const spur_opt *o, spurliste *s, char *err) {
    if (o->halbton == 3) return spuren_erzeugen_hp(b0, o, s, err);
    memset(s, 0, sizeof *s);
    /* Tonwertkurve: auf eine Kopie des Graustufenbildes anwenden (vor Abtastung + Halbtonung) */
    bild bk; const bild *b = b0; uint8_t *kopie = NULL;
    if (o->lut) {
        size_t n = (size_t)b0->w * b0->h;
        kopie = malloc(n);
        for (size_t i = 0; i < n; i++) kopie[i] = o->lut[b0->px[i]];
        bk = *b0; bk.px = kopie; b = &bk;
    }
    const kern_e *kern = K_FS; int nk = 4, div = 16;
    if (o->halbton == 1) { kern = K_JARVIS; nk = 12; div = 48; }
    else if (o->halbton == 2) { kern = K_STUCKI; nk = 12; div = 42; }

    double pitch = 25.4 / o->tpi;
    int N = (int)floor((o->r_aussen_mm - o->r_innen_mm) / pitch + 1e-9);
    if (N <= 0) { strcpy(err, "ungueltiger Druckbereich"); free(kopie); return -1; }
    /* Bildmassstab: mm pro Pixel */
    double mm_px;
    if (o->fit) mm_px = 2 * o->r_aussen_mm / (b->w > b->h ? b->w : b->h);
    else mm_px = 25.4 / (b->dpi > 1 ? b->dpi : o->dpi_ersatz);
    double cx = (b->w - 1) / 2.0, cy = (b->h - 1) / 2.0;

    int umax = spur_u(o->r_aussen_mm) + 16;
    /* Fehlerpuffer: [0] = aktuelle Spur, [1] = naechste, [2] = uebernaechste */
    double *ep[3];
    for (int k = 0; k < 3; k++) ep[k] = calloc(umax, sizeof(double));
    uint8_t *bits = malloc(umax);
    uint64_t gesamt = 0, schwarz = 0;
    s->spuren_gesamt = 0;

    for (int n = 0; n < N; n++) {
        double r = o->r_innen_mm + (n + 0.5) * pitch;
        int U = spur_u(r), Uz[3] = {U, spur_u(r + pitch), spur_u(r + 2 * pitch)};
        int im_band = !(o->band_bis_mm > 0) || (r >= o->band_von_mm && r <= o->band_bis_mm);
        int rueck = n & 1;                     /* Schlangenlinie: jede 2. Spur rueckwaerts */
        int fw = rueck ? -1 : 1;               /* Laufrichtung in Bit-Indizes */
        int any = 0;
        for (int k = 0; k < U; k++) {
            int i = rueck ? U - 1 - k : k;
            double th = 2 * M_PI * i / U;
            double v = 0;
            if (im_band) v = schwaerzung(b, cx + r * cos(th) / mm_px, cy + r * sin(th) / mm_px);
            double w = v + ep[0][i];
            int out = w >= 0.5;
            bits[i] = (uint8_t)out; any |= out;
            double e = w - out;
            for (int q = 0; q < nk; q++) {
                const kern_e *ke = &kern[q];
                if (ke->dy == 0) {
                    if (k + ke->dx < U) ep[0][((i + fw * ke->dx) % U + U) % U] += e * ke->w / div;
                } else {
                    int Un = Uz[ke->dy];
                    int j = (int)lround((double)i * Un / U);
                    ep[ke->dy][((j + fw * ke->dx) % Un + Un) % Un] += e * ke->w / div;
                }
            }
        }
        double *t = ep[0]; ep[0] = ep[1]; ep[1] = ep[2]; ep[2] = t;   /* weiterschieben */
        memset(ep[2], 0, sizeof(double) * umax);
        if (!im_band) continue;
        s->spuren_gesamt++;
        if (!any) continue;                    /* leere Spur: nicht senden (wie Original) */
        uint32_t nb = (uint32_t)((U + 31) / 32) * 4;
        uint8_t *pk = calloc(16 + nb, 1);
        pk[0] = n >> 8; pk[1] = n & 0xff;
        pk[12] = o->param400 >> 8; pk[13] = o->param400 & 0xff;
        pk[14] = U >> 8; pk[15] = U & 0xff;
        for (int i = 0; i < U; i++) if (bits[i]) { pk[16 + (i >> 3)] |= 1 << (i & 7); schwarz++; }   /* LSB-first wie Original */
        gesamt += U;
        add(s, pk, 16 + nb);
    }
    for (int k = 0; k < 3; k++) free(ep[k]);
    free(bits); free(kopie);
    s->punkte = gesamt;
    s->dichte = gesamt ? (double)schwarz / gesamt : 0;
    return 0;
}

/* ---- Datei: "LS64SPR1" + Kopf + Pakete (u32 Laenge + Daten), Endmarke am Schluss ---- */
static void w32(FILE *f, uint32_t v) { uint8_t b[4] = {v, v >> 8, v >> 16, v >> 24}; fwrite(b, 1, 4, f); }
static int r32(FILE *f, uint32_t *v) { uint8_t b[4]; if (fread(b, 1, 4, f) != 4) return -1; *v = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; return 0; }

int spuren_speichern(const char *pfad, const spurliste *s, const spur_opt *o) {
    FILE *f = fopen(pfad, "wb"); if (!f) return -1;
    fwrite("LS64SPR1", 1, 8, f);
    w32(f, 1); w32(f, o->tpi); w32(f, s->n); w32(f, o->param400);
    w32(f, (uint32_t)lround(o->r_innen_mm * 1000)); w32(f, (uint32_t)lround(o->r_aussen_mm * 1000));
    w32(f, s->spuren_gesamt); w32(f, 0);
    for (int i = 0; i < s->n; i++) { w32(f, s->p[i].len); fwrite(s->p[i].data, 1, s->p[i].len, f); }
    uint8_t e[16]; endmarke(e); w32(f, 16); fwrite(e, 1, 16, f);
    return fclose(f);
}

int spuren_laden(const char *pfad, spurliste *s, spur_opt *o, char *err) {
    memset(s, 0, sizeof *s); spur_opt_standard(o);
    FILE *f = fopen(pfad, "rb"); if (!f) { snprintf(err, 200, "kann '%s' nicht oeffnen", pfad); return -1; }
    char m[8]; uint32_t v, tpi, n, p4, ri, ra, sg, res;
    if (fread(m, 1, 8, f) != 8 || memcmp(m, "LS64SPR1", 8) || r32(f, &v) || r32(f, &tpi) || r32(f, &n) || r32(f, &p4)
        || r32(f, &ri) || r32(f, &ra) || r32(f, &sg) || r32(f, &res)) { fclose(f); strcpy(err, "keine ls64-Spurdatei"); return -1; }
    o->tpi = tpi; o->param400 = p4; o->r_innen_mm = ri / 1000.0; o->r_aussen_mm = ra / 1000.0; s->spuren_gesamt = sg;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t len; if (r32(f, &len) || len < 16 || len > 4096) { fclose(f); strcpy(err, "Spurdatei kaputt"); return -1; }
        uint8_t *d = malloc(len); if (fread(d, 1, len, f) != len) { free(d); fclose(f); strcpy(err, "Spurdatei kaputt"); return -1; }
        add(s, d, len);
    }
    fclose(f);
    uint64_t g = 0, z = 0;
    for (int i = 0; i < s->n; i++) { int U = s->p[i].data[14] << 8 | s->p[i].data[15]; g += U;
        for (int k = 0; k < U; k++) z += (s->p[i].data[16 + (k >> 3)] >> (7 - (k & 7))) & 1; }
    s->punkte = g; s->dichte = g ? (double)z / g : 0;
    return 0;
}

/* ---- Windows-Druckdatei (.lsd, aus LightScribe-Windows "SetSavePrintFile", Version 3) ----
 * 0x00 Magic 84 75 57 48, u32 Version; 0x10 Laufwerk/Medium (16 B); 0x20 Laserparameter (32 B):
 * [4] Leseleistung [5] Schreibleistung [7] Fokus (int8) [0x10] u32 Tempo um/s [0x14] u32 Spurabstand nm [0x18] u32 Markenabstand nm
 * ab 0x40 je Spur: 02 00 | u16 Spur | 00 00 | u16 Marken U | u16 Param(400) | 6x0, danach U Bits (MSB zuerst, auf 32 Bit aufgefuellt)
 * Ende: 02 01 + 14x0.  Alle Zahlen little-endian. */
static lsd_param g_lsd;
const lsd_param *lsd_parameter(void) { return g_lsd.gueltig ? &g_lsd : NULL; }
static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
int spuren_laden_lsd(const char *pfad, spurliste *s, spur_opt *o, char *err) {
    memset(s, 0, sizeof *s); spur_opt_standard(o); memset(&g_lsd, 0, sizeof g_lsd);
    FILE *f = fopen(pfad, "rb"); if (!f) { snprintf(err, 200, "kann '%s' nicht oeffnen", pfad); return -1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    uint8_t *d = malloc(n > 0 ? n : 1);
    if (n < 0x50 || fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); strcpy(err, "Druckdatei zu kurz"); return -1; }
    fclose(f);
    if (memcmp(d, "\x84\x75\x57\x48", 4) || le32(d + 4) != 3) { free(d); strcpy(err, "keine Windows-Druckdatei (Version 3)"); return -1; }
    const uint8_t *p = d + 0x20;
    g_lsd.gueltig = 1; g_lsd.lese = p[4]; g_lsd.schreib = p[5]; g_lsd.fokus = (int8_t)p[7];
    g_lsd.tempo_um = le32(p + 0x10); g_lsd.spur_nm = le32(p + 0x14); g_lsd.marke_nm = le32(p + 0x18);
    g_lsd.laufwerk = d[0x12]; g_lsd.medium = d[0x1c];
    if (!g_lsd.spur_nm) { free(d); strcpy(err, "Druckdatei: Spurabstand 0"); return -1; }
    o->tpi = (int)lround(25.4e6 / g_lsd.spur_nm);
    o->param400 = d[0x1a] | d[0x1b] << 8;
    long pos = 0x40; int ende = 0;
    while (pos + 16 <= n) {
        const uint8_t *h = d + pos;
        if (h[0] == 2 && h[1] == 1) { ende = 1; break; }
        if (h[0] != 2 || h[1] != 0) { free(d); snprintf(err, 200, "Druckdatei kaputt bei 0x%lx", pos); spurliste_frei(s); return -1; }
        unsigned nr = h[2] | h[3] << 8, U = h[6] | h[7] << 8, par = h[8] | h[9] << 8;
        unsigned nb = (U + 31) / 32 * 4;
        if (pos + 16 + (long)nb > n || nb > 4096 - 16) { free(d); strcpy(err, "Druckdatei abgeschnitten"); spurliste_frei(s); return -1; }
        uint8_t *pk = calloc(1, 16 + nb);
        pk[0] = nr >> 8; pk[1] = nr; pk[12] = par >> 8; pk[13] = par; pk[14] = U >> 8; pk[15] = U;
        memcpy(pk + 16, h + 16, nb);
        add(s, pk, 16 + nb);
        pos += 16 + nb;
    }
    free(d);
    if (!ende) { strcpy(err, "Druckdatei ohne Endmarke"); spurliste_frei(s); return -1; }
    s->spuren_gesamt = s->n;
    uint64_t g = 0, z = 0;
    for (int i = 0; i < s->n; i++) { int U = s->p[i].data[14] << 8 | s->p[i].data[15]; g += U;
        for (int k = 0; k < U; k++) z += (s->p[i].data[16 + (k >> 3)] >> (7 - (k & 7))) & 1; }
    s->punkte = g; s->dichte = g ? (double)z / g : 0;
    return 0;
}

/* Vorschau: Disc von der Labelseite gesehen (wie das Bild), Punkte gemittelt */
int spuren_vorschau(const spurliste *s, const spur_opt *o, const char *png, int G) {
    double pitch = 25.4 / o->tpi;
    int N = (int)floor((o->r_aussen_mm - o->r_innen_mm) / pitch + 1e-9) + 1;
    const uint8_t **spur = calloc(N, sizeof *spur);
    for (int i = 0; i < s->n; i++) { int n = s->p[i].data[0] << 8 | s->p[i].data[1]; if (n < N) spur[n] = s->p[i].data; }
    uint8_t *img = malloc((size_t)G * G);
    double mm = 2 * (o->r_aussen_mm + 1.0) / G;   /* mm pro Vorschaupixel */
    for (int y = 0; y < G; y++) for (int x = 0; x < G; x++) {
        double X = (x - G / 2.0 + 0.5) * mm, Y = (y - G / 2.0 + 0.5) * mm, r = hypot(X, Y);
        uint8_t val = 255;
        if (r >= o->r_innen_mm - pitch / 2 && r <= o->r_aussen_mm + pitch / 2) {
            val = 220;                                   /* unbeschriebene Disc */
            double th = atan2(Y, X); if (th < 0) th += 2 * M_PI;
            int n0 = (int)floor((r - mm / 2 - o->r_innen_mm) / pitch - 0.5), n1 = (int)ceil((r + mm / 2 - o->r_innen_mm) / pitch - 0.5);
            int sum = 0, cnt = 0;
            for (int n = n0 < 0 ? 0 : n0; n <= n1 && n < N; n++) {
                const uint8_t *d = spur[n];
                double rr = o->r_innen_mm + (n + 0.5) * pitch;
                int U = spur_u(rr); if (d) U = d[14] << 8 | d[15];
                int w = (int)(mm / rr / (2 * M_PI) * U) + 1;
                int i0 = (int)(th / (2 * M_PI) * U - w / 2.0);
                for (int k = 0; k < w; k++) { int i = ((i0 + k) % U + U) % U;
                    if (d) sum += (d[16 + (i >> 3)] >> (i & 7)) & 1;   /* LSB-first */
                    cnt++; }
            }
            if (cnt) val = (uint8_t)lround(220.0 * (1.0 - (double)sum / cnt));
        }
        img[(size_t)y * G + x] = val;
    }
    int r = png_speichern(png, img, G, G);
    free(img); free(spur);
    return r;
}

/* ======================= Halbtonung kompatibel zur Original-Engine =======================
 * Geometrie: Bild nearest neighbor, fit-dpi = round(max(W,H)*25,4/117,4), Mitte W/2+0,25,
 *   r = 23,8 mm + (n+0,5)*Spurabstand, U = round(2*pi*r/42,333 um) (frac < 0,0037 -> floor-1).
 * Halbtonung: Fehlerdiffusion mit tongesteuerten Gewichten/Schwellen und 128x128-Entscheidungsmuster
 *   (Tabellen: daten.c, aus der Software des Nutzers), Spuren abwechselnd rueckwaerts/vorwaerts,
 *   Fehlerpuffer zwischen den Spuren gestreckt. Kontone = 256 - Grau (Grau 255 / ausserhalb = 0).
 * Bits werden LSB-first gepackt (Marke j = Byte j>>3, Bit j&7). */
#include "daten.h"
#define HT_RAND 16
static int ht_u(int n, int spur_nm) {
    double r = 23800.0 + (n + 0.5) * spur_nm / 1000.0, x = 2 * M_PI * r / 42.333, fr = x - floor(x);
    return fr < 0.0037 ? (int)floor(x) - 1 : (int)floor(x + 0.5);
}
static inline int ht_tr(int x) { return x >= 0 ? x >> 8 : -((-x) >> 8); }
static long long ht_cdiv(long long a, long long b) { long long q = llabs(a) / llabs(b); return ((a >= 0) == (b > 0)) ? q : -q; }
static void ht_dehnen(int *ep, int W, long long A, long long D) {
    long long X = W - 1, idx_to = ht_cdiv(X * A, D) + X + 1;
    long long rem = D - (X * A - ht_cdiv(X * A, D) * D), half = ht_cdiv(D, 2);
    long long src = X, dst = idx_to;
    while (dst > src && X >= 0) {
        if (rem > half) { ep[HT_RAND + dst] = 0; dst--; rem -= D; }
        ep[HT_RAND + dst] = ep[HT_RAND + src]; ep[HT_RAND + src] = 0; src--; dst--;
        rem += A; X--;
    }
}
static void ht_spur(const uint8_t *f, uint8_t *dots, int W, int rueck, int row, int col, int *ep) {
    const uint8_t *M = HT_M + 128 * row; int P, e, s;
    if (rueck) { P = HT_RAND + W - 1; e = ep[P]; ep[P] = 0; ep[P + 1] = 0; s = -1; }
    else       { P = HT_RAND;         e = ep[P]; ep[P] = 0; ep[P - 1] = 0; s = 1; }
    col &= 127;
    for (int k = 0; k < W; k++) {
        int j = rueck ? W - 1 - k : k, v = f[j], dot = 0;
        if (v == 255) dot = 1;
        else if (v) { int lo = HT_THR[2 * v], hi = HT_THR[2 * v + 1]; e += v;
            if (e > lo || (e > hi && M[col])) { dot = 1; e -= 255; } }
        const uint8_t *w = HT_W + 4 * v;
        int a0 = ht_tr(e * w[0]), a1 = ht_tr(e * w[1]), a2 = ht_tr(e * w[2]), a3 = ht_tr(e * w[3]);
        e = a0 + ep[P + s]; ep[P + s] = a1; ep[P] += a2; ep[P - s] += a3;
        P += s; col = (col + s) & 127; dots[j] = (uint8_t)dot;
    }
}
int spuren_erzeugen_hp(const bild *b0, const spur_opt *o, spurliste *s, char *err) {
    memset(s, 0, sizeof *s);
    const uint8_t *px = b0->px; uint8_t *kopie = NULL;
    if (o->lut) { size_t n = (size_t)b0->w * b0->h; kopie = malloc(n); for (size_t i = 0; i < n; i++) kopie[i] = o->lut[b0->px[i]]; px = kopie; }
    int W = b0->w, H = b0->h;
    int spur_nm = (int)lround(25.4e6 / o->tpi);
    int N = (int)floor((o->r_aussen_mm - o->r_innen_mm) * o->tpi / 25.4 + 1e-9);
    double dpi = o->fit ? (double)lround((W > H ? W : H) * 25.4 / 117.4) : (b0->dpi > 1 ? b0->dpi : o->dpi_ersatz);
    double mm_px = 25.4 / dpi, cx = W / 2.0 + 0.25, cy = H / 2.0 + 0.25;
    int umax = ht_u(N + 2, spur_nm) + 64;
    int *ep = calloc(2 * HT_RAND + umax + 64, sizeof(int));
    uint8_t *f = malloc(umax), *dots = malloc(umax);
    uint64_t gesamt = 0, schwarz = 0;
    for (int n = 0; n < N; n++) {
        int U = ht_u(n, spur_nm), Wd = 8 * ((U + 7) / 8);
        double r = (23800.0 + (n + 0.5) * spur_nm / 1000.0) / 1000.0;
        int im_band = !(o->band_bis_mm > 0) || (r >= o->band_von_mm && r <= o->band_bis_mm);
        memset(f, 0, Wd);
        if (im_band) for (int i = 0; i < U; i++) {
            double th = 2 * M_PI * i / U;
            long xi = (long)floor(cx + r * cos(th) / mm_px), yi = (long)floor(cy + r * sin(th) / mm_px);
            if (xi < 0 || yi < 0 || xi >= W || yi >= H) continue;
            int g = px[(size_t)yi * W + xi];
            f[i] = g >= 255 ? 0 : (uint8_t)(256 - g > 255 ? 255 : 256 - g);
        }
        int rueck = (n % 2 == 0);
        ht_spur(f, dots, Wd, rueck, (42 + n) & 127, 35 + (rueck ? Wd : 0), ep);
        ht_dehnen(ep, Wd, spur_nm, (long long)n * spur_nm + 23800000LL);
        if (!im_band) continue;
        s->spuren_gesamt++;
        int any = 0; for (int i = 0; i < U; i++) any |= dots[i];
        if (!any) continue;
        uint32_t nb = (uint32_t)((U + 31) / 32) * 4;
        uint8_t *pk = calloc(16 + nb, 1);
        pk[0] = n >> 8; pk[1] = n & 0xff; pk[12] = o->param400 >> 8; pk[13] = o->param400 & 0xff; pk[14] = U >> 8; pk[15] = U & 0xff;
        for (int i = 0; i < U; i++) if (dots[i]) { pk[16 + (i >> 3)] |= 1 << (i & 7); schwarz++; }
        gesamt += U;
        add(s, pk, 16 + nb);
    }
    free(ep); free(f); free(dots); free(kopie);
    s->punkte = gesamt; s->dichte = gesamt ? (double)schwarz / gesamt : 0;
    return 0;
}
