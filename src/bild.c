/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
/*
 * bild.c – Bilder laden (BMP, PNG) und speichern (PNG oder BMP, nach Dateiendung), als 8-bit-Graustufen.
 * Teil von ls64. Ohne libpng (-DOHNE_PNG, z. B. Windows-Bau) werden nur BMP-Dateien gelesen und geschrieben.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#ifndef OHNE_PNG
#include <png.h>
#endif
#include "ls64.h"

/* Farbe -> Grau wie die Original-Engine (gemessen mit Farbfeldern: 0,43 R + 0,46 G + 0,11 B) */
static uint8_t lum(int r, int g, int b) { return (uint8_t)((43 * r + 46 * g + 11 * b + 50) / 100); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }

/* ---- BMP: unkomprimiert 8/24/32 Bit, oder 1/4/8 Bit mit Palette ---- */
static int load_bmp(FILE *f, bild *b, char *err) {
    uint8_t fh[14], ih[124];
    if (fread(fh, 1, 14, f) != 14 || fh[0] != 'B' || fh[1] != 'M') { strcpy(err, "keine BMP-Datei"); return -1; }
    uint32_t off = le32(fh + 10);
    if (fread(ih, 1, 4, f) != 4) { strcpy(err, "BMP-Kopf kaputt"); return -1; }
    uint32_t hs = le32(ih); if (hs < 40 || hs > 124) { strcpy(err, "BMP-Format nicht unterstuetzt"); return -1; }
    if (fread(ih + 4, 1, hs - 4, f) != hs - 4) { strcpy(err, "BMP-Kopf kaputt"); return -1; }
    int32_t w = (int32_t)le32(ih + 4), h = (int32_t)le32(ih + 8);
    int bpp = le16(ih + 14); uint32_t comp = le32(ih + 16);
    uint32_t ppm_x = le32(ih + 24), ncol = le32(ih + 32);
    if (comp != 0 && !(comp == 3 && bpp == 32)) { strcpy(err, "komprimierte BMP nicht unterstuetzt"); return -1; }
    int bottom_up = h > 0; if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || w > 20000 || h > 20000) { strcpy(err, "ungueltige Bildgroesse"); return -1; }
    uint8_t pal[256][4]; memset(pal, 0, sizeof pal);
    if (bpp <= 8) {
        if (!ncol) ncol = 1u << bpp;
        if (ncol > 256) ncol = 256;
        fseek(f, 14 + hs, SEEK_SET);
        if (fread(pal, 4, ncol, f) != ncol) { strcpy(err, "BMP-Palette kaputt"); return -1; }
    } else if (bpp != 24 && bpp != 32) { strcpy(err, "BMP-Farbtiefe nicht unterstuetzt"); return -1; }
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    uint8_t *row = malloc(stride);
    b->w = w; b->h = h; b->px = malloc((size_t)w * h);
    b->dpi = ppm_x ? ppm_x * 0.0254 : 0;
    for (int y = 0; y < h; y++) {
        fseek(f, off + (long)stride * y, SEEK_SET);
        if (fread(row, 1, stride, f) != stride) { strcpy(err, "BMP-Daten unvollstaendig"); free(row); return -1; }
        uint8_t *dst = b->px + (size_t)(bottom_up ? h - 1 - y : y) * w;
        for (int x = 0; x < w; x++) {
            int idx;
            switch (bpp) {
            case 1: idx = (row[x >> 3] >> (7 - (x & 7))) & 1; dst[x] = lum(pal[idx][2], pal[idx][1], pal[idx][0]); break;
            case 4: idx = (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15; dst[x] = lum(pal[idx][2], pal[idx][1], pal[idx][0]); break;
            case 8: idx = row[x]; dst[x] = lum(pal[idx][2], pal[idx][1], pal[idx][0]); break;
            case 24: dst[x] = lum(row[3 * x + 2], row[3 * x + 1], row[3 * x]); break;
            default: dst[x] = lum(row[4 * x + 2], row[4 * x + 1], row[4 * x]); break;
            }
        }
    }
    free(row);
    return 0;
}

#ifndef OHNE_PNG
/* ---- PNG ueber libpng; Transparenz wird auf Weiss gelegt ---- */
static int load_png(FILE *f, bild *b, char *err) {
    png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop i = png_create_info_struct(p);
    if (setjmp(png_jmpbuf(p))) { png_destroy_read_struct(&p, &i, NULL); strcpy(err, "PNG kaputt"); return -1; }
    png_init_io(p, f);
    png_read_info(p, i);
    png_set_expand(p); png_set_strip_16(p); png_set_gray_to_rgb(p);
    png_set_add_alpha(p, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(p, i);
    int w = png_get_image_width(p, i), h = png_get_image_height(p, i);
    png_uint_32 rx = 0, ry = 0; int unit = 0;
    b->dpi = (png_get_pHYs(p, i, &rx, &ry, &unit) && unit == PNG_RESOLUTION_METER) ? rx * 0.0254 : 0;
    uint8_t *rgba = malloc((size_t)w * h * 4);
    png_bytep *rows = malloc(sizeof(png_bytep) * h);
    for (int y = 0; y < h; y++) rows[y] = rgba + (size_t)y * w * 4;
    png_read_image(p, rows);
    png_destroy_read_struct(&p, &i, NULL);
    b->w = w; b->h = h; b->px = malloc((size_t)w * h);
    for (size_t k = 0; k < (size_t)w * h; k++) {
        uint8_t *q = rgba + 4 * k; int a = q[3];
        int g = lum(q[0], q[1], q[2]);
        b->px[k] = (uint8_t)((g * a + 255 * (255 - a) + 127) / 255);
    }
    free(rows); free(rgba);
    return 0;
}
#endif

int bild_laden(const char *pfad, bild *b, char *err) {
    memset(b, 0, sizeof *b);
    FILE *f = fopen(pfad, "rb");
    if (!f) { snprintf(err, 200, "kann '%s' nicht oeffnen", pfad); return -1; }
    uint8_t sig[8] = {0};
    size_t n = fread(sig, 1, 8, f); rewind(f);
    int r;
    if (n >= 2 && sig[0] == 'B' && sig[1] == 'M') r = load_bmp(f, b, err);
#ifndef OHNE_PNG
    else if (n == 8 && !png_sig_cmp(sig, 0, 8)) r = load_png(f, b, err);
    else { strcpy(err, "unbekanntes Bildformat (BMP oder PNG verwenden)"); r = -1; }
#else
    else { strcpy(err, "unbekanntes Bildformat (BMP verwenden)"); r = -1; }
#endif
    fclose(f);
    return r;
}

/* 8-bit-Graustufen als BMP (mit Palette) speichern */
static int bmp_speichern(const char *pfad, const uint8_t *px, int w, int h) {
    FILE *f = fopen(pfad, "wb"); if (!f) return -1;
    size_t stride = ((size_t)w + 3) & ~(size_t)3, groesse = 14 + 40 + 1024 + stride * h;
    uint8_t k[54] = {'B', 'M'};
    #define W32(o, v) do { uint32_t _v = (uint32_t)(v); k[o] = _v; k[o+1] = _v >> 8; k[o+2] = _v >> 16; k[o+3] = _v >> 24; } while (0)
    W32(2, groesse); W32(10, 14 + 40 + 1024); W32(14, 40); W32(18, w); W32(22, h);
    k[26] = 1; k[28] = 8; W32(34, stride * h); W32(38, 10670); W32(42, 10670); W32(46, 256);
    #undef W32
    fwrite(k, 1, 54, f);
    for (int i = 0; i < 256; i++) { uint8_t p[4] = {(uint8_t)i, (uint8_t)i, (uint8_t)i, 0}; fwrite(p, 1, 4, f); }
    uint8_t *z = calloc(1, stride);
    for (int y = h - 1; y >= 0; y--) { memcpy(z, px + (size_t)y * w, w); fwrite(z, 1, stride, f); }
    free(z);
    return fclose(f);
}

static int endet_bmp(const char *p) {
    size_t n = strlen(p);
    return n >= 4 && p[n - 4] == '.' && tolower((unsigned char)p[n - 3]) == 'b' && tolower((unsigned char)p[n - 2]) == 'm' && tolower((unsigned char)p[n - 1]) == 'p';
}

int png_speichern(const char *pfad, const uint8_t *px, int w, int h) {
    if (endet_bmp(pfad)) return bmp_speichern(pfad, px, w, h);
#ifdef OHNE_PNG
    return bmp_speichern(pfad, px, w, h);
#else
    FILE *f = fopen(pfad, "wb"); if (!f) return -1;
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop i = png_create_info_struct(p);
    if (setjmp(png_jmpbuf(p))) { png_destroy_write_struct(&p, &i); fclose(f); return -1; }
    png_init_io(p, f);
    png_set_IHDR(p, i, w, h, 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(p, i);
    for (int y = 0; y < h; y++) png_write_row(p, (png_const_bytep)(px + (size_t)y * w));
    png_write_end(p, i);
    png_destroy_write_struct(&p, &i);
    fclose(f);
    return 0;
#endif
}

/* ---- Tonwertkurve (LUT) laden --------------------------------------------
 * Textdatei, '#' = Kommentar. Zwei Formen:
 *   a) genau 256 Zeilen mit je EINER Zahl  (Eintrag i = neuer Grauwert fuer Grauwert i)
 *   b) Zeilen mit je "ein aus" (Stuetzstellen, beliebig viele >= 2, dazwischen linear,
 *      ausserhalb konstant)
 * Werte 0..255 (0 = schwarz, 255 = weiss), Dezimalzahlen erlaubt. Zeilen ohne Zahl
 * am Anfang (Kopfzeilen) werden ueberlesen. */
int lut_laden(const char *pfad, uint8_t lut[256], char *err) {
    FILE *f = fopen(pfad, "r");
    if (!f) { snprintf(err, 200, "kann Kurve '%s' nicht oeffnen", pfad); return -1; }
    double ein[4096], aus[4096]; int n = 0, einzel = 0, paare = 0;
    char z[512];
    while (fgets(z, sizeof z, f)) {
        char *c = strchr(z, '#'); if (c) *c = 0;
        for (char *q = z; *q; q++) if (*q == ',' || *q == ';' || *q == '\t') *q = ' ';
        char *e1, *e2; double a = strtod(z, &e1);
        if (e1 == z) continue;                         /* leer oder Kopfzeile */
        double v = strtod(e1, &e2);
        if (n >= 4096) { fclose(f); strcpy(err, "Kurve: zu viele Zeilen"); return -1; }
        if (e2 != e1) { ein[n] = a; aus[n] = v; paare++; }
        else { ein[n] = n; aus[n] = a; einzel++; }
        n++;
    }
    fclose(f);
    if (einzel && paare) { strcpy(err, "Kurve: Zeilen mit einer Zahl und mit Paaren gemischt"); return -1; }
    if (einzel && n != 256) { snprintf(err, 200, "Kurve: %d Zahlen gefunden, erwartet 256 (oder 'ein aus'-Paare)", n); return -1; }
    if (paare && n < 2) { strcpy(err, "Kurve: mindestens 2 Stuetzstellen noetig"); return -1; }
    for (int i = 0; i < n; i++) {
        if (aus[i] < 0 || aus[i] > 255 || ein[i] < 0 || ein[i] > 255) { strcpy(err, "Kurve: Werte muessen 0..255 sein"); return -1; }
        if (i && ein[i] <= ein[i - 1]) { strcpy(err, "Kurve: 'ein'-Werte muessen streng aufsteigend sein"); return -1; }
    }
    int k = 0;
    for (int g = 0; g < 256; g++) {
        double y;
        if (g <= ein[0]) y = aus[0];
        else if (g >= ein[n - 1]) y = aus[n - 1];
        else { while (ein[k + 1] < g) k++; double t = (g - ein[k]) / (ein[k + 1] - ein[k]); y = aus[k] + t * (aus[k + 1] - aus[k]); }
        lut[g] = (uint8_t)(y + 0.5);
    }
    return 0;
}
