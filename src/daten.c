/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 LS Label Studio contributors
 * LS Label Studio – https://github.com/Efratsy/ls-label-studio */
/* daten.c – Tabellen der LightScribe-Engine aus der Software des Nutzers einlesen (ls64 einrichten)
 * und zur Laufzeit laden. Dieses Programm enthaelt keine dieser Tabellen selbst.
 *
 * Gesucht wird:
 *   - die "Drive Resource Descriptors" (XML-Texte <DriveResource>…</DriveResource>): Laserparameter je
 *     Laufwerk/Medium
 *   - der 17920-Byte-Block der Halbtonung (Gewichte 256x4, Schwellen 256x2, Ditherflag 128x128), erkannt an
 *     seiner Struktur und geprueft ueber SHA-256 (nur die Version, mit der ls64 verifiziert wurde, wird akzeptiert)
 * Quellen: liblightscribe.so.1 (Linux), LSPrtEn.dll (Windows) oder die Pakete .deb/.rpm (gzip-komprimiert). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ls64.h"
#include "daten.h"
#include "plattform.h"
#ifndef OHNE_ZLIB
#include <zlib.h>
#endif

#define BLOCK 17920
static const uint8_t BLOCK_SHA256[32] = {
    0xb2,0x90,0x6f,0x5b,0xeb,0x71,0xd1,0x33,0xcc,0x0a,0xb0,0x78,0x16,0x18,0x37,0xa7,
    0xae,0x38,0x9c,0x8b,0x31,0x6d,0x85,0x3a,0xf8,0x03,0xe8,0xa7,0xc8,0x7e,0x8a,0x18};

const uint8_t *HT_W, *HT_THR, *HT_M;
static uint8_t g_block[BLOCK];
static drf_param *g_drf; static int g_ndrf, g_geladen;

/* ---------------------------------------------------------------- SHA-256 (kompakt) */
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void sha256(const uint8_t *d, size_t n, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t tot = ((n + 9 + 63) / 64) * 64;
    for (size_t off = 0; off < tot; off += 64) {
        uint8_t blk[64];
        for (int i = 0; i < 64; i++) {
            size_t p = off + i;
            blk[i] = p < n ? d[p] : p == n ? 0x80 : 0;
        }
        if (off + 64 == tot) { uint64_t bits = (uint64_t)n * 8; for (int i = 0; i < 8; i++) blk[63 - i] = (uint8_t)(bits >> (8 * i)); }
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = (uint32_t)blk[4*i] << 24 | blk[4*i+1] << 16 | blk[4*i+2] << 8 | blk[4*i+3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3), s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],e=h[4],f=h[5],g=h[6],hh=h[7],dd=h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ROR(e,6)^ROR(e,11)^ROR(e,25), ch = (e&f)^(~e&g), t1 = hh+S1+ch+K256[i]+w[i];
            uint32_t S0 = ROR(a,2)^ROR(a,13)^ROR(a,22), mj = (a&b)^(a&c)^(b&c), t2 = S0+mj;
            hh=g; g=f; f=e; e=dd+t1; dd=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=dd; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    for (int i = 0; i < 8; i++) { out[4*i] = h[i] >> 24; out[4*i+1] = h[i] >> 16; out[4*i+2] = h[i] >> 8; out[4*i+3] = (uint8_t)h[i]; }
}

/* ---------------------------------------------------------------- Pfade */
static char g_pfad[1100];
const char *daten_pfad(void) {
    if (!g_pfad[0]) snprintf(g_pfad, sizeof g_pfad, "%s%clsdaten.bin", pf_datenordner(), PF_TRENNER);
    return g_pfad;
}

/* ---------------------------------------------------------------- Laden */
static int datei_lesen(const char *p, uint8_t **buf, size_t *n) {
    FILE *f = pf_fopen(p, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    if (l < 0) { fclose(f); return -1; }
    *buf = malloc(l ? l : 1); *n = l;
    if (fread(*buf, 1, l, f) != (size_t)l) { fclose(f); free(*buf); return -1; }
    fclose(f); return 0;
}
static int16_t le16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

static int laden_aus(const char *p) {
    uint8_t *b; size_t n;
    if (datei_lesen(p, &b, &n)) return -1;
    if (n < 16 + BLOCK || memcmp(b, "LSLSDAT1", 8)) { free(b); return -2; }
    uint32_t nd = b[12] | b[13] << 8 | b[14] << 16 | (uint32_t)b[15] << 24;
    if (n != 16 + (size_t)nd * 22 + BLOCK) { free(b); return -2; }
    const uint8_t *blk = b + 16 + nd * 22; uint8_t h[32]; sha256(blk, BLOCK, h);
    if (memcmp(h, BLOCK_SHA256, 32)) { free(b); return -3; }
    memcpy(g_block, blk, BLOCK);
    g_drf = calloc(nd ? nd : 1, sizeof(drf_param)); g_ndrf = nd;
    for (uint32_t i = 0; i < nd; i++) {
        const uint8_t *e = b + 16 + i * 22; drf_param *d = &g_drf[i];
        d->laufwerk = le16(e); d->medium = le16(e + 2); d->fokus = le16(e + 4); d->tempo = le16(e + 6);
        d->schreib = le16(e + 8); d->lese = le16(e + 10);
        for (int k = 0; k < 4; k++) d->tpi[k] = le16(e + 12 + 2 * k);
        d->mpi = le16(e + 20);
    }
    free(b);
    HT_W = g_block; HT_THR = g_block + 1024; HT_M = g_block + 1536;
    g_geladen = 1;
    return 0;
}

int daten_laden(char *err, int errlen) {
    if (g_geladen) return 0;
    const char *env = getenv("LS64_DATEN");
    char p2[1100];
    if (env && *env && laden_aus(env) == 0) return 0;
    if (laden_aus(daten_pfad()) == 0) return 0;
    snprintf(p2, sizeof p2, "%s%clsdaten.bin", pf_programmordner(), PF_TRENNER);
    if (laden_aus(p2) == 0) return 0;
#ifndef _WIN32
    if (laden_aus("/usr/local/share/ls-label-studio/lsdaten.bin") == 0) return 0;
#endif
    snprintf(err, errlen, "Die LightScribe-Tabellen fehlen (%s).\n"
             "Bitte einmalig 'ls64 einrichten' ausfuehren (liest sie aus der installierten LightScribe System Software).", daten_pfad());
    return -1;
}

int daten_drf_anzahl(void) { return g_ndrf; }

const drf_param *drf_suchen(int laufwerk, int medium) {
    char e[300];
    if (daten_laden(e, sizeof e)) return NULL;
    for (int i = 0; i < g_ndrf; i++) if (g_drf[i].laufwerk == laufwerk && g_drf[i].medium == medium) return &g_drf[i];
    return NULL;
}

/* ---------------------------------------------------------------- Einrichten */
static const uint8_t *suche(const uint8_t *h, size_t hn, const char *n, size_t ab) {
    size_t nn = strlen(n);
    for (size_t i = ab; i + nn <= hn; i++) if (h[i] == (uint8_t)n[0] && !memcmp(h + i, n, nn)) return h + i;
    return NULL;
}
static int tag_int(const uint8_t *a, const uint8_t *e, const char *tag, int *v) {
    char t[64]; snprintf(t, sizeof t, "<%s>", tag);
    const uint8_t *p = suche(a, e - a, t, 0); if (!p) return -1;
    p += strlen(t); *v = (int)strtol((const char *)p, NULL, 10); return 0;
}
static int drf_vergleich(const void *x, const void *y) {
    const drf_param *a = x, *b = y;
    return a->laufwerk != b->laufwerk ? a->laufwerk - b->laufwerk : a->medium - b->medium;
}
/* XML-Beschreibungen sammeln; Rueckgabe Anzahl Eintraege */
static int drf_parsen(const uint8_t *b, size_t n, drf_param **aus) {
    int cap = 256, k = 0; drf_param *v = malloc(sizeof *v * cap);
    size_t pos = 0;
    for (;;) {
        const uint8_t *s = suche(b, n, "<DriveResource>", pos); if (!s) break;
        const uint8_t *e = suche(b, n, "</DriveResource>", s - b); if (!e) break;
        int lw; if (tag_int(s, e, "DriveID", &lw)) { pos = e - b; continue; }
        size_t mp = s - b;
        for (;;) {
            const uint8_t *m = suche(b, e - b, "<Media>", mp); if (!m) break;
            const uint8_t *me = suche(b, e - b, "</Media>", m - b); if (!me) break;
            int x[11]; static const char *tags[] = {"ImagingParameter", "FocusOffset", "WriteVelocity", "LaserWritePower", "LaserReadPower",
                "TrackResolution-Draft", "TrackResolution-Normal", "TrackResolution-Best", "TrackResolution-ELCBest", "MarkResolution"};
            int ok = 1; for (int t = 0; t < 10; t++) if (tag_int(m, me, tags[t], &x[t])) ok = 0;
            if (ok) {
                if (k == cap) { cap *= 2; v = realloc(v, sizeof *v * cap); }
                drf_param *d = &v[k++];
                d->laufwerk = lw; d->medium = x[0]; d->fokus = x[1]; d->tempo = x[2]; d->schreib = x[3]; d->lese = x[4];
                for (int t = 0; t < 4; t++) d->tpi[t] = x[5 + t];
                d->mpi = x[9];
            }
            mp = me - b;
        }
        pos = e - b;
    }
    /* sortieren, Duplikate (gleiches Laufwerk+Medium) entfernen */
    qsort(v, k, sizeof *v, drf_vergleich);
    int j = 0; for (int i = 0; i < k; i++) if (!j || drf_vergleich(&v[j - 1], &v[i])) v[j++] = v[i];
    *aus = v; return j;
}
/* Halbton-Block finden: Ditherteil (16384 Bytes nur 0/1) ab Versatz 1536, dann SHA-256 */
static int block_finden(const uint8_t *b, size_t n, uint8_t out[BLOCK]) {
    if (n < BLOCK) return -1;
    size_t lauf = 0;     /* Laenge der aktuellen 0/1-Folge, die bei i endet */
    for (size_t i = 0; i < n; i++) {
        lauf = b[i] <= 1 ? lauf + 1 : 0;
        if (lauf >= 16384) {
            size_t m0 = i + 1 - 16384;            /* moeglicher Anfang des Ditherteils */
            if (m0 >= 1536) {
                uint8_t h[32]; sha256(b + m0 - 1536, BLOCK, h);
                if (!memcmp(h, BLOCK_SHA256, 32)) { memcpy(out, b + m0 - 1536, BLOCK); return 0; }
            }
        }
    }
    return -1;
}
typedef struct { int hat_block, ndrf; uint8_t block[BLOCK]; drf_param *drf; } fund;
static void puffer_pruefen(const uint8_t *b, size_t n, fund *f) {
    if (!f->hat_block && block_finden(b, n, f->block) == 0) f->hat_block = 1;
    if (!f->ndrf) { drf_param *d; int k = drf_parsen(b, n, &d); if (k > 0) { f->drf = d; f->ndrf = k; } else free(d); }
}
#ifndef OHNE_ZLIB
static void gzip_pruefen(const uint8_t *b, size_t n, fund *f) {
    for (size_t i = 0; i + 10 < n && (!f->hat_block || !f->ndrf); i++) {
        if (b[i] != 0x1f || b[i + 1] != 0x8b || b[i + 2] != 8) continue;
        z_stream z; memset(&z, 0, sizeof z);
        if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) continue;
        size_t cap = 1 << 22, len = 0; uint8_t *o = malloc(cap);
        z.next_in = (Bytef *)(b + i); z.avail_in = (uInt)(n - i);
        int r;
        do {
            if (len == cap) { cap *= 2; o = realloc(o, cap); }
            z.next_out = o + len; z.avail_out = (uInt)(cap - len);
            r = inflate(&z, Z_NO_FLUSH);
            len = cap - z.avail_out;
        } while (r == Z_OK && len < ((size_t)1 << 30));
        inflateEnd(&z);
        if (len > 1000) puffer_pruefen(o, len, f);
        free(o);
    }
}
#endif
static int quelle_pruefen(const char *p, fund *f) {
    uint8_t *b; size_t n;
    if (datei_lesen(p, &b, &n)) return -1;
    puffer_pruefen(b, n, f);
#ifndef OHNE_ZLIB
    if (!f->hat_block || !f->ndrf) gzip_pruefen(b, n, f);
#endif
    free(b);
    return 0;
}
static int speichern(fund *f, char *meldung, int len) {
    pf_ordner_anlegen(pf_datenordner());
    FILE *o = pf_fopen(daten_pfad(), "wb");
    if (!o) { snprintf(meldung, len, "Kann %s nicht schreiben.", daten_pfad()); return -1; }
    uint8_t kopf[16] = {'L','S','L','S','D','A','T','1', 1,0,0,0, (uint8_t)f->ndrf, (uint8_t)(f->ndrf >> 8), (uint8_t)(f->ndrf >> 16), 0};
    fwrite(kopf, 1, 16, o);
    for (int i = 0; i < f->ndrf; i++) {
        const drf_param *d = &f->drf[i];
        int16_t v[11] = {d->laufwerk, d->medium, d->fokus, d->tempo, d->schreib, d->lese, d->tpi[0], d->tpi[1], d->tpi[2], d->tpi[3], d->mpi};
        for (int k = 0; k < 11; k++) { uint8_t x[2] = {(uint8_t)v[k], (uint8_t)((uint16_t)v[k] >> 8)}; fwrite(x, 1, 2, o); }
    }
    fwrite(f->block, 1, BLOCK, o);
    return fclose(o);
}

int daten_einrichten(const char *quelle, char *meldung, int len) {
    static const char *namen[] = {"liblightscribe.so.1", "liblightscribe.so", "LSPrtEn.dll", NULL};
#ifdef _WIN32
    static const char *auto_q[] = {"C:\\Program Files (x86)\\Common Files\\LightScribe\\LSPrtEn.dll",
                                   "C:\\Program Files\\Common Files\\LightScribe\\LSPrtEn.dll", NULL};
#else
    static const char *auto_q[] = {"/usr/lib/liblightscribe.so.1", "/usr/lib32/liblightscribe.so.1", "/usr/lib/i386-linux-gnu/liblightscribe.so.1",
                                   "/opt/lightscribe/lib/liblightscribe.so.1", "/usr/local/lib/liblightscribe.so.1", NULL};
#endif
    fund f; memset(&f, 0, sizeof f);
    const char *benutzt = NULL;
    if (quelle) {
        if (quelle_pruefen(quelle, &f) == 0) benutzt = quelle;
        else {  /* vielleicht ein Ordner */
            char p[1100];
            for (int i = 0; namen[i] && (!f.hat_block || !f.ndrf); i++) {
                snprintf(p, sizeof p, "%s%c%s", quelle, PF_TRENNER, namen[i]);
                if (quelle_pruefen(p, &f) == 0) benutzt = quelle;
            }
            if (!benutzt) { snprintf(meldung, len, "Kann '%s' nicht lesen.", quelle); return -1; }
        }
    } else {
        for (int i = 0; auto_q[i] && (!f.hat_block || !f.ndrf); i++)
            if (pf_existiert(auto_q[i]) && quelle_pruefen(auto_q[i], &f) == 0) benutzt = auto_q[i];
        if (!benutzt) {
            snprintf(meldung, len, "Keine LightScribe System Software gefunden.\n"
#ifdef _WIN32
                     "Bitte die LightScribe System Software installieren oder 'ls64 einrichten PFAD\\LSPrtEn.dll' angeben."
#else
                     "Bitte das Paket angeben: 'ls64 einrichten lightscribe-1.18.27.10-linux-2.6-intel.deb' (oder .rpm / liblightscribe.so.1)."
#endif
                     );
            return -1;
        }
    }
    if (!f.hat_block || !f.ndrf) {
        snprintf(meldung, len, "In '%s' wurden die Tabellen nicht (vollstaendig) gefunden%s.\n"
                 "Benoetigt wird die LightScribe System Software 1.18.27.10 (Linux .deb/.rpm oder Windows LSPrtEn.dll).",
                 benutzt, f.ndrf && !f.hat_block ? " (Halbton-Tabellen fehlen oder andere Version)" : "");
        free(f.drf); return -1;
    }
    if (speichern(&f, meldung, len)) { free(f.drf); return -1; }
    snprintf(meldung, len, "Eingerichtet: %d Laufwerk/Medium-Eintraege und Halbton-Tabellen aus '%s'\n-> %s",
             f.ndrf, benutzt, daten_pfad());
    free(f.drf);
    g_geladen = 0; free(g_drf); g_drf = NULL;
    return 0;
}
