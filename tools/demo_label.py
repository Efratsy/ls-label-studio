#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LS Label Studio contributors
"""Neutrales Demo-/Testlabel fuer LS Label Studio (eigenes Motiv, frei verwendbar)."""
from PIL import Image, ImageDraw, ImageFont
import math

S = 2; N = 1254 * S; c = N / 2; PXMM = 271 / 25.4 * S
F = '/usr/share/fonts/truetype/dejavu/'
def font(mm, fett=True): return ImageFont.truetype(F + ('DejaVuSans-Bold.ttf' if fett else 'DejaVuSans.ttf'), max(8, int(mm * PXMM)))
im = Image.new('L', (N, N), 255); d = ImageDraw.Draw(im)

# --- obere Haelfte: Landschaft (weiche Verlaeufe = Tonwert-Test)
horizont = c + 6 * PXMM
for y in range(int(horizont)):
    t = y / horizont
    d.line([(0, y), (N, y)], fill=int(110 + 135 * t ** 0.8))
sx, sy = c + 20 * PXMM, c - 38 * PXMM
for r in range(int(8 * PXMM), 0, -1):
    d.ellipse([sx - r, sy - r, sx + r, sy + r], fill=int(255 - 25 * (r / (8 * PXMM)) ** 2))
def berge(basis, amp, freq, phase, oben, unten):
    pts = []
    for x in range(0, N + 1, 3):
        u = x / N
        h = basis - amp * (0.6 * math.sin(freq * u * 6.283 + phase) + 0.3 * math.sin(2.7 * freq * u * 6.283 + 1.7 * phase) + 0.1 * math.sin(7.3 * freq * u * 6.283))
        pts.append((x, h))
    maske = Image.new('L', (N, N), 0); ImageDraw.Draw(maske).polygon(pts + [(N, horizont), (0, horizont)], fill=255)
    top = int(min(p[1] for p in pts))
    verlauf = Image.new('L', (N, N), 0); vd = ImageDraw.Draw(verlauf)
    for y in range(top, int(horizont) + 1):
        t = (y - top) / max(1, horizont - top); vd.line([(0, y), (N, y)], fill=int(oben + (unten - oben) * t))
    im.paste(verlauf, (0, 0), maske)
berge(c - 16 * PXMM, 10 * PXMM, 1.1, 0.4, 175, 150)
berge(c - 7 * PXMM, 8 * PXMM, 1.6, 2.1, 125, 95)
berge(c + 1 * PXMM, 6 * PXMM, 2.2, 4.0, 70, 35)
d.rectangle([0, horizont, N, N], fill=255)

# --- Graukeil: 11 Felder 0..100 %, Ring 36..47 mm, unten
n, a0, a1 = 11, 22, 158
r1, r2 = 47 * PXMM, 36 * PXMM
for k in range(n):
    ton = int(round(255 * (1 - k / (n - 1))))
    s = a0 + (a1 - a0) * k / n; e = a0 + (a1 - a0) * (k + 1) / n
    d.pieslice([c - r1, c - r1, c + r1, c + r1], s, e, fill=ton)
d.arc([c - r1, c - r1, c + r1, c + r1], a0, a1, fill=0, width=int(0.25 * PXMM))
d.pieslice([c - r2, c - r2, c + r2, c + r2], a0, a1, fill=255)
d.arc([c - r2, c - r2, c + r2, c + r2], a0, a1, fill=0, width=int(0.25 * PXMM))
f = font(2.0, True)
for k in range(n):
    a = math.radians(a0 + (a1 - a0) * (k + 0.5) / n); r = 41.5 * PXMM
    t = f"{k * 10}"; w = d.textlength(t, font=f)
    d.text((c + r * math.cos(a) - w / 2, c + r * math.sin(a) - 1.2 * PXMM), t, font=f, fill=255 if k >= 5 else 0)

# --- Feinlinien-Test: Strahlenfaecher links und rechts auf Hoehe des Horizonts
for seite in (-1, 1):
    for k in range(24):
        a = math.radians((0 if seite > 0 else 180) + (k - 12) * 0.9)
        ra, rb = 49.5 * PXMM, 57 * PXMM
        d.line([(c + ra * math.cos(a), c + 9 * PXMM + ra * math.sin(a) * 0.15), (c + rb * math.cos(a), c + 9 * PXMM + rb * math.sin(a) * 0.15)], fill=0, width=max(1, int(0.12 * PXMM)))

# --- Text im Bogen
def bogen(text, r_mm, mm, oben=True, fett=True, ton=0):
    f = font(mm, fett); r = r_mm * PXMM
    breiten = [d.textlength(ch, font=f) for ch in text]; ges = sum(breiten)
    start = (-math.pi / 2 - ges / r / 2) if oben else (math.pi / 2 + ges / r / 2); x = 0
    for ch, w in zip(text, breiten):
        mitte = x + w / 2; a = start + mitte / r if oben else start - mitte / r
        gw = gh = int(mm * PXMM * 3)
        g = Image.new('L', (gw, gh), 0); gd = ImageDraw.Draw(g)
        asc, desc = f.getmetrics()
        y0 = gh / 2 - asc if oben else gh / 2         # oben: Grundlinie auf r (nach aussen), unten: nach innen
        gd.text((gw / 2 - w / 2, y0), ch, font=f, fill=255)
        g = g.rotate(-(math.degrees(a) + (90 if oben else -90)), resample=Image.BICUBIC)
        im.paste(ton, (int(c + r * math.cos(a) - gw / 2), int(c + r * math.sin(a) - gh / 2)), g)
        x += w

bogen("LS LABEL STUDIO", 51.5, 6.2, True, True, 255)
bogen("NATIVE LIGHTSCRIBE LABELING  ·  LINUX & WINDOWS  ·  64-BIT", 52.0, 2.5, False, True, 0)

# --- gerade Zeilen
def mittig(text, y_mm, mm, fett=True, ton=0):
    f = font(mm, fett); w = d.textlength(text, font=f)
    d.text((c - w / 2, c + y_mm * PXMM), text, font=f, fill=ton)
bogen("DEMO LABEL  ·  v1.1", 27.0, 2.6, True, True, 255)
for i, mm in enumerate((1.2, 1.6, 2.0)):
    mittig("fine text %.1f mm" % mm, 25.3 + i * 2.9, mm, False)

out = im.resize((1254, 1254), Image.LANCZOS)
out.save('demo_label.png', dpi=(271, 271))
v = out.convert('RGB'); m = Image.new('L', v.size, 0); md = ImageDraw.Draw(m)
cc = 627; ra, ri = 58.7 * 271 / 25.4, 23.8 * 271 / 25.4
md.ellipse([cc - ra, cc - ra, cc + ra, cc + ra], fill=255); md.ellipse([cc - ri, cc - ri, cc + ri, cc + ri], fill=0)
bg = Image.new('RGB', v.size, (200, 200, 205)); bg.paste(v, (0, 0), m); bg.save('demo_vorschau.png')
