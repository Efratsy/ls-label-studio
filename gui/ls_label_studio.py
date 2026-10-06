#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LS Label Studio contributors
"""LS Label Studio – design and burn LightScribe labels on Linux and Windows (GUI for ls64).
LS Label Studio – LightScribe-Labels unter Linux und Windows gestalten und brennen (Oberflaeche fuer ls64).

Qt binding: PyQt6 (preferred), PySide6 or PyQt5.
Usage:  ls_label_studio.py                normal
        ls_label_studio.py --selbsttest   headless self-test (render a label + ls64 spuren)
"""
import locale, math, os, re, shutil, subprocess, sys, time

# ---------------------------------------------------------------- Qt-Bindung
QT = None
for _name in ("PyQt6", "PySide6", "PyQt5"):
    try:
        if _name == "PyQt6":
            from PyQt6.QtCore import Qt, QTimer, QProcess, QSettings, QPointF, QRectF, QSize, QLocale
            from PyQt6.QtGui import (QImage, QPainter, QColor, QFont, QFontMetricsF, QPixmap, QPen, QBrush, QCursor,
                                     QPainterPath, QTransform, QIcon)
            from PyQt6.QtWidgets import *  # noqa
        elif _name == "PySide6":
            from PySide6.QtCore import Qt, QTimer, QProcess, QSettings, QPointF, QRectF, QSize, QLocale
            from PySide6.QtGui import (QImage, QPainter, QColor, QFont, QFontMetricsF, QPixmap, QPen, QBrush, QCursor,
                                       QPainterPath, QTransform, QIcon)
            from PySide6.QtWidgets import *  # noqa
        else:
            from PyQt5.QtCore import Qt, QTimer, QProcess, QSettings, QPointF, QRectF, QSize, QLocale
            from PyQt5.QtGui import (QImage, QPainter, QColor, QFont, QFontMetricsF, QPixmap, QPen, QBrush, QCursor,
                                     QPainterPath, QTransform, QIcon)
            from PyQt5.QtWidgets import *  # noqa
        QT = _name
        break
    except ImportError:
        continue
if QT is None:
    sys.stderr.write("No Qt binding found / Keine Qt-Bindung gefunden (PyQt6).\n")
    sys.exit(1)

try:
    import numpy as np
except ImportError:
    np = None

# ---------------------------------------------------------------- Konstanten
APP = "LS Label Studio"
VERSION = "1.1"
WINDOWS = sys.platform.startswith("win")
N = 1254                                   # Arbeitsflaeche in Pixel
DPI = round(N * 25.4 / 117.4)              # = 271, so rechnet ls64 bei "fit"
MM = 25.4 / DPI                            # mm pro Pixel
R_AUSSEN, R_INNEN = 58.7, 23.8             # Druckbereich der Disc (mm)
if WINDOWS:
    CACHE = os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "LSLabelStudio", "cache")
else:
    CACHE = os.path.join(os.path.expanduser("~"), ".cache", "ls-label-studio")

# ---------------------------------------------------------------- Sprache / language
SPRACHE = "en"
TEXTE = {
    "tab_bild": ("1  Bild", "1  Image"), "tab_text": ("2  Text", "2  Text"), "tab_brennen": ("3  Brennen", "3  Burn"),
    "bereit": ("Bereit.", "Ready."),
    "kein_ls64": ("Brennprogramm ls64 nicht gefunden – bitte neu installieren.", "Burn engine ls64 not found – please reinstall."),
    "bild_oeffnen": ("Bild öffnen …", "Open image …"),
    "kein_bild": ("(kein Bild – nur Text ist auch möglich)", "(no image – text only is fine too)"),
    "zoom": ("Zoom", "Zoom"), "drehen": ("Drehen", "Rotate"), "hell": ("Helligkeit", "Brightness"),
    "kontrast": ("Kontrast", "Contrast"), "mitte": ("Mitteltöne", "Midtones"), "zurueck": ("Alles zurücksetzen", "Reset all"),
    "tipp_bild": ("Tipp: In der Vorschau mit der Maus ziehen = verschieben, Mausrad = Zoom.\n"
                  "LightScribe brennt nur Graustufen; Farben werden wie bei der Original-Software umgerechnet.",
                  "Tip: drag in the preview to move the image, mouse wheel to zoom.\n"
                  "LightScribe burns grayscale only; colors are converted like the original software does."),
    "label_speichern": ("Label als Bild speichern …", "Save label as image …"),
    "bilder_filter": ("Bilder", "Images"), "bild_fehler": ("Das Bild konnte nicht geladen werden.", "The image could not be loaded."),
    "tooltip_vorschau": ("Ziehen: Bild verschieben  ·  Mausrad: Zoom", "Drag: move image  ·  Wheel: zoom"),
    "text_n": ("Text %d", "Text %d"), "text_platzhalter": ("z. B. STAFFEL 2 · FOLGE 5 & 6", "e.g. SEASON 2 · EPISODES 5 & 6"),
    "art": ("Art", "Style"), "art_liste": (["Bogen oben", "Bogen unten", "Gerade"], ["Arc top", "Arc bottom", "Straight"]),
    "radius": ("Radius / Höhe", "Radius / height"), "groesse": ("Größe", "Size"), "schrift": ("Schrift", "Font"),
    "fett": ("fett", "bold"), "weiss": ("weiß (auf dunklem Bild)", "white (on dark image)"),
    "tipp_text": ("„Radius“: Abstand der Schriftlinie von der Mitte (Bogen) bzw. Höhe unter/über der Mitte (Gerade, negativ = oben).\n"
                  "Druckbereich: 23,8 bis 58,7 mm vom Mittelpunkt.",
                  "\"Radius\": distance of the baseline from the center (arc) or offset below/above the center (straight, negative = up).\n"
                  "Printable area: 23.8 to 58.7 mm from the center."),
    "modus": ("Modus", "Mode"), "ganz": ("Ganze Disc", "Full disc"),
    "ring": ("Nur Ring (z. B. nur ein Titel) – deutlich schneller", "Ring only (e.g. just a title) – much faster"),
    "von": ("von", "from"), "bis": ("bis", "to"), "ring_lbl": ("Ring", "Ring"), "qualitaet": ("Qualität", "Quality"),
    "q_normal": ("Standard – 1015 Spuren/Zoll (empfohlen)", "Standard – 1015 tracks/inch (recommended)"),
    "q_draft": ("Schnell – 760 Spuren/Zoll (etwas heller)", "Fast – 760 tracks/inch (slightly lighter)"),
    "q_best": ("Fein – 1398 Spuren/Zoll (deutlich langsamer)", "Fine – 1398 tracks/inch (much slower)"),
    "laufwerk": ("Laufwerk", "Drive"), "geraet": ("Gerät", "Device"), "pruefen": ("Disc prüfen", "Check disc"),
    "nicht_geprueft": ("Noch nicht geprüft. Disc mit der Labelseite nach unten einlegen, ca. 1 Minute warten.",
                       "Not checked yet. Insert the disc label side down and wait about 1 minute."),
    "exakt": ("Exakte Punkt-Vorschau berechnen", "Compute exact dot preview"), "brennen": ("Brennen", "Burn"),
    "abbrechen": ("Abbrechen", "Cancel"), "ausgabe": ("Ausgabe von ls64 …", "ls64 output …"),
    "ueber": ("Über LS Label Studio", "About LS Label Studio"), "sprache": ("Sprache: Deutsch", "Language: English"),
    "sprache_neustart": ("Die Sprache wird beim nächsten Start übernommen.", "The language will change on next start."),
    "ueber_text": ("LightScribe-Labels unter Linux und Windows gestalten und brennen.", "Design and burn LightScribe labels on Linux and Windows."),
    "ueber_ki": ("Entwickelt mit Unterstützung eines KI-Modells (Claude, Anthropic); alle Ergebnisse wurden an echter Hardware geprüft.",
                 "Developed with the assistance of an AI model (Claude, Anthropic); all results were verified on real hardware."),
    "ueber_lizenz": ("Freie Software unter der GNU GPL v3 – Quellcode:", "Free software under the GNU GPL v3 – source code:"),
    "ueber_marke": ("LightScribe ist eine Marke von HP. Dieses Projekt ist unabhängig und nicht mit HP verbunden.",
                    "LightScribe is a trademark of HP. This project is independent and not affiliated with HP."),
    "brennprogramm": ("Brennprogramm", "Burn engine"), "nicht_gefunden": ("nicht gefunden", "not found"),
    "pruefe": ("prüfe …", "checking …"),
    "disc_bereit": ("✔ Bereit zum Beschriften.", "✔ Ready to label."),
    "disc_erkennt": ("⏳ Laufwerk erkennt die Disc noch – bitte ca. 1 Minute warten und erneut prüfen.",
                     "⏳ The drive is still detecting the disc – wait about 1 minute and check again."),
    "disc_falsch": ("✘ Keine LightScribe-Disc erkannt oder falsch herum. Labelseite muss nach UNTEN zeigen.",
                    "✘ No LightScribe disc detected or upside down. The label side must face DOWN."),
    "disc_keine": ("✘ Keine Disc eingelegt.", "✘ No disc inserted."),
    "disc_nicht": ("✘ Laufwerk nicht bereit – siehe Ausgabe.", "✘ Drive not ready – see output."),
    "rechte_linux": ("\n\nTipp: Rechte fehlen – bitte installieren.sh noch einmal ausführen.", "\n\nTip: missing permissions – please run install.sh again."),
    "rechte_win": ("\n\nTipp: Bitte LS Label Studio als Administrator starten.", "\n\nTip: please run LS Label Studio as administrator."),
    "leer": ("Das Label ist leer. Bitte ein Bild öffnen oder Text eingeben.", "The label is empty. Open an image or enter some text."),
    "berechne": ("Berechne Spuren …", "Computing tracks …"),
    "vorschau_fehler": ("Vorschau fehlgeschlagen:\n", "Preview failed:\n"), "berech_fehler": ("Berechnung fehlgeschlagen:\n", "Computation failed:\n"),
    "frage_brennen": ("Alles bereit.\n\nGeschätzte Brenndauer: ca. %s min\nWährend des Brennens die Disc nicht auswerfen und den Rechner nicht in den Standby schicken.\n\nJetzt brennen?",
                      "All set.\n\nEstimated burn time: about %s min\nDo not eject the disc or put the computer to sleep while burning.\n\nBurn now?"),
    "nicht_gebrannt": ("Nicht gebrannt.", "Not burned."), "laeuft_an": ("Laufwerk läuft an …", "Drive is spinning up …"),
    "brennt": ("Brennt … %s %%   vergangen %s", "Burning … %s %%   elapsed %s"), "rest": ("   Rest ca. %s", "   remaining approx. %s"),
    "fertig": ("✔ Fertig – das Label ist gebrannt.", "✔ Done – the label has been burned."),
    "fertig_box": ("Fertig! Das Label ist gebrannt.", "Done! The label has been burned."),
    "abgebrochen": ("Abgebrochen – Label unvollständig.", "Cancelled – label incomplete."),
    "fehler": ("Fehler – siehe Ausgabe unten.", "Error – see output below."), "brenn_fehler": ("Brennen fehlgeschlagen:\n", "Burning failed:\n"),
    "wirklich_abbrechen": ("Brennen wirklich abbrechen? Das Label bleibt unvollständig.", "Really cancel burning? The label will be incomplete."),
    "breche_ab": ("Breche ab … (bereits gesendete Spuren werden noch gebrannt, dann stoppt das Laufwerk)", "Cancelling … (tracks already sent are finished, then the drive stops)"),
    "schliessen_brennt": ("Es wird gerade gebrannt. Bitte erst warten oder „Abbrechen“ drücken.", "Burning in progress. Please wait or press \"Cancel\"."),
    "vorschau_titel": ("Exakte Vorschau – so werden die Lasermarken verteilt", "Exact preview – distribution of the laser marks"),
    "dauer": ("Geschätzte Brenndauer: ca. %s min", "Estimated burn time: about %s min"),
    "einr_titel": ("Einrichtung", "Setup"),
    "einr_text": ("LS Label Studio braucht einmalig die Laufwerks- und Halbton-Tabellen der originalen "
                  "LightScribe System Software (Version 1.18.27.10). Sie werden daraus gelesen und lokal gespeichert – "
                  "die Software selbst wird danach nicht mehr benötigt.\n\n"
                  "Unter Windows genügt die installierte LightScribe System Software.\n"
                  "Unter Linux: das Paket lightscribe-1.18.27.10-linux-2.6-intel.deb oder .rpm auswählen.",
                  "LS Label Studio needs the drive and halftone tables of the original LightScribe System Software "
                  "(version 1.18.27.10) once. They are read from it and stored locally – the software itself is not "
                  "needed afterwards.\n\nOn Windows the installed LightScribe System Software is enough.\n"
                  "On Linux: select the package lightscribe-1.18.27.10-linux-2.6-intel.deb or .rpm."),
    "einr_auto": ("Automatisch suchen", "Search automatically"), "einr_datei": ("Datei auswählen …", "Choose file …"),
    "einr_spaeter": ("Später", "Later"),
    "einr_filter": ("LightScribe System Software (*.deb *.rpm *.so *.so.1 *.dll);;Alle Dateien (*)",
                    "LightScribe System Software (*.deb *.rpm *.so *.so.1 *.dll);;All files (*)"),
    "einr_ok": ("Einrichtung abgeschlossen.", "Setup complete."), "einr_fehler": ("Einrichtung fehlgeschlagen:\n", "Setup failed:\n"),
    "einr_fehlt": ("Die Tabellen fehlen noch – bitte zuerst einrichten.", "The tables are still missing – please run setup first."),
}


def T(k):
    v = TEXTE[k]
    return v[0] if SPRACHE == "de" else v[1]


def sprache_bestimmen(einst):
    global SPRACHE
    s = einst.value("sprache", "")
    if s not in ("de", "en"):
        s = "de" if QLocale.system().name().lower().startswith("de") else "en"
    SPRACHE = s


def basisordner():
    """Ordner mit den Programmdateien (auch im PyInstaller-Paket)."""
    if getattr(sys, "frozen", False):
        return getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
    return os.path.dirname(os.path.abspath(__file__))


def finde_ls64():
    name = "ls64.exe" if WINDOWS else "ls64"
    kandidaten = [os.path.join(basisordner(), name)]
    if getattr(sys, "frozen", False):
        kandidaten.append(os.path.join(os.path.dirname(sys.executable), name))
    if not WINDOWS:
        kandidaten.append("/usr/local/bin/ls64")
    kandidaten.append(shutil.which(name) or "")
    for p in kandidaten:
        if p and os.path.isfile(p) and (WINDOWS or os.access(p, os.X_OK)):
            return p
    return None


def laufwerke():
    if WINDOWS:
        try:
            import ctypes
            maske = ctypes.windll.kernel32.GetLogicalDrives()
            liste = []
            for i in range(26):
                if maske & (1 << i):
                    b = chr(65 + i)
                    if ctypes.windll.kernel32.GetDriveTypeW(f"{b}:\\") == 5:      # DRIVE_CDROM
                        liste.append(f"{b}:")
            return liste or ["D:"]
        except Exception:
            return ["D:"]
    import glob
    return sorted(glob.glob("/dev/sr[0-9]*")) or ["/dev/sr0"]


def prozess_ohne_fenster():
    """subprocess-Optionen, damit unter Windows kein Konsolenfenster aufblitzt."""
    if WINDOWS:
        return {"creationflags": 0x08000000}      # CREATE_NO_WINDOW
    return {}


def _bytes(img):
    """Rohdaten eines QImage als bytes (PyQt: sip.voidptr, PySide: memoryview)."""
    b = img.constBits()
    if hasattr(b, "setsize"):
        b.setsize(img.sizeInBytes())
    return bytes(b)


def grau_laden(pfad):
    """Bild laden -> QImage Grayscale8 mit der Grauformel der Original-Engine (43 R + 46 G + 11 B)."""
    img = QImage(pfad)
    if img.isNull():
        return None
    if max(img.width(), img.height()) > 2400:      # sehr grosse Fotos verkleinern (schneller, reicht fuer 271 dpi)
        img = img.scaled(2400, 2400, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
    # Transparenz auf Weiss legen
    rgb = QImage(img.size(), QImage.Format.Format_RGB32)
    rgb.fill(QColor(255, 255, 255))
    p = QPainter(rgb); p.drawImage(0, 0, img); p.end()
    w, h = rgb.width(), rgb.height()
    raw = _bytes(rgb)                               # BGRA je Pixel, Zeilen ggf. aufgefuellt
    bpl = rgb.bytesPerLine()
    if np is not None:
        a = np.frombuffer(raw, np.uint8).reshape(h, bpl)[:, :w * 4].reshape(h, w, 4).astype(np.uint32)
        g = ((43 * a[..., 2] + 46 * a[..., 1] + 11 * a[..., 0] + 50) // 100).astype(np.uint8)
        data = g.tobytes()
    else:
        out = bytearray(w * h)
        for y in range(h):
            row = raw[y * bpl: y * bpl + 4 * w]
            o = y * w
            for x in range(w):
                i = 4 * x
                out[o + x] = (43 * row[i + 2] + 46 * row[i + 1] + 11 * row[i] + 50) // 100
        data = bytes(out)
    g = QImage(data, w, h, w, QImage.Format.Format_Grayscale8).copy()
    return g


def lut_anwenden(gray, hell, kontr, gamma):
    """Helligkeit (-100..100), Kontrast (-100..100), Gamma (0.3..3) per Tabelle auf ein Grayscale8-Bild."""
    if hell == 0 and kontr == 0 and abs(gamma - 1) < 1e-6:
        return gray
    k = math.tan((kontr + 100) / 200 * math.pi / 2) if kontr < 100 else 50.0   # 0 -> 1
    lut = bytearray(256)
    for v in range(256):
        x = v / 255.0
        x = x ** (1.0 / gamma)
        x = (x - 0.5) * k + 0.5 + hell / 200.0
        lut[v] = max(0, min(255, int(round(x * 255))))
    w, h, bpl = gray.width(), gray.height(), gray.bytesPerLine()
    data = _bytes(gray).translate(bytes(lut))
    return QImage(data, w, h, bpl, QImage.Format.Format_Grayscale8).copy()


# ---------------------------------------------------------------- Label-Modell
class Textzeile:
    def __init__(self, text="", art="gerade", pos=0.0, groesse=6.0, fett=True, schrift="Sans Serif", weiss=False):
        self.aktiv = bool(text); self.text = text; self.art = art; self.pos = pos
        self.groesse = groesse; self.fett = fett; self.schrift = schrift; self.weiss = weiss


class Label:
    def __init__(self):
        self.pfad = None; self.grau = None; self.angepasst = None
        self.zoom = 1.0; self.dreh = 0.0; self.dx = 0.0; self.dy = 0.0        # dx/dy in mm
        self.hell = 0; self.kontr = 0; self.gamma = 1.0
        self.texte = [Textzeile(art="bogen_oben", pos=52.0), Textzeile(art="bogen_unten", pos=50.0),
                      Textzeile(art="gerade", pos=30.0)]

    def bild_setzen(self, pfad):
        g = grau_laden(pfad)
        if g is None:
            return False
        self.pfad = pfad; self.grau = g; self.zoom = 1.0; self.dreh = 0.0; self.dx = self.dy = 0.0
        self.anpassen()
        return True

    def anpassen(self):
        self.angepasst = lut_anwenden(self.grau, self.hell, self.kontr, self.gamma) if self.grau is not None else None

    def rendern(self, groesse=N):
        """Label als QImage (RGB32, Graustufen) in groesse x groesse Pixel; Massstab wie ls64 'fit'."""
        img = QImage(groesse, groesse, QImage.Format.Format_RGB32)
        img.fill(QColor(255, 255, 255))
        s = groesse / N                                   # Vorschau-Skalierung
        px = 1.0 / MM * s                                 # Pixel je mm
        c = groesse / 2.0
        p = QPainter(img)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
        p.setRenderHint(QPainter.RenderHint.TextAntialiasing, True)
        if self.angepasst is not None:
            b = self.angepasst
            f = groesse / max(b.width(), b.height()) * self.zoom      # "fit": groessere Seite = Disc
            t = QTransform()
            t.translate(c + self.dx * px, c + self.dy * px)
            t.rotate(self.dreh)
            t.scale(f, f)
            t.translate(-b.width() / 2.0, -b.height() / 2.0)
            p.setTransform(t)
            p.drawImage(0, 0, b)
            p.resetTransform()
        for z in self.texte:
            if z.aktiv and z.text.strip():
                self._text(p, z, c, px)
        p.end()
        return img

    def _text(self, p, z, c, px):
        font = QFont(z.schrift)
        font.setPixelSize(max(1, int(round(z.groesse * px))))
        font.setBold(z.fett)
        p.setFont(font)
        p.setPen(QColor(255, 255, 255) if z.weiss else QColor(0, 0, 0))
        fm = QFontMetricsF(font)
        if z.art == "gerade":
            w = fm.horizontalAdvance(z.text)
            y = c + z.pos * px
            p.drawText(QPointF(c - w / 2.0, y + fm.ascent() / 2.0 - fm.descent() / 2.0), z.text)
            return
        r = z.pos * px                                    # Radius der Grundlinie
        if r <= 1:
            return
        breiten = [fm.horizontalAdvance(ch) for ch in z.text]
        gesamt = sum(breiten)
        oben = (z.art == "bogen_oben")
        start = (-math.pi / 2 - gesamt / r / 2) if oben else (math.pi / 2 + gesamt / r / 2)
        x = 0.0
        for ch, w in zip(z.text, breiten):
            mitte = x + w / 2.0
            a = start + mitte / r if oben else start - mitte / r
            p.save()
            p.translate(c + r * math.cos(a), c + r * math.sin(a))
            p.rotate(math.degrees(a) + (90 if oben else -90))
            if oben:
                p.drawText(QPointF(-w / 2.0, 0), ch)                      # Schrift nach aussen
            else:
                p.drawText(QPointF(-w / 2.0, fm.ascent()), ch)            # Schrift nach innen, aufrecht lesbar
            p.restore()
            x += w


# ---------------------------------------------------------------- Disc-Vorschau
class DiscAnsicht(QWidget):
    def __init__(self, label, geaendert):
        super().__init__()
        self.label = label; self.geaendert = geaendert
        self.band = None                    # (von_mm, bis_mm) im Ring-Modus
        self.pix = None; self._ziehen = None
        self.setMinimumSize(420, 420)
        self.setMouseTracking(False)
        self.setToolTip(T("tooltip_vorschau"))

    def neu(self):
        g = max(300, min(self.width(), self.height()))
        self.pix = self.label.rendern(g)
        self.update()

    def resizeEvent(self, e):
        self.neu()

    def paintEvent(self, e):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        p.fillRect(self.rect(), self.palette().window())
        if self.pix is None:
            return
        g = self.pix.width()
        ox, oy = (self.width() - g) / 2.0, (self.height() - g) / 2.0
        c = QPointF(ox + g / 2.0, oy + g / 2.0)
        pxmm = g / (N * MM)
        ra, ri = R_AUSSEN * pxmm, R_INNEN * pxmm
        # Disc-Rohling (silbrig)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(205, 205, 210)); p.drawEllipse(c, 60.0 * pxmm, 60.0 * pxmm)
        # Druckbereich: Label in LightScribe-Toenung (gold/grau), nur im Ring
        ring = QPainterPath(); ring.addEllipse(c, ra, ra); innen = QPainterPath(); innen.addEllipse(c, ri, ri)
        ring = ring.subtracted(innen)
        p.save(); p.setClipPath(ring)
        img = self.pix.convertToFormat(QImage.Format.Format_RGB32)
        p.drawImage(QPointF(ox, oy), img)
        p.setCompositionMode(QPainter.CompositionMode.CompositionMode_Multiply)
        p.fillRect(QRectF(ox, oy, g, g), QColor(232, 222, 196))
        p.restore()
        # Ring-Modus: nicht gebrannte Bereiche abdunkeln
        if self.band:
            von, bis = self.band
            aus = QPainterPath(); aus.addEllipse(c, ra, ra)
            b = QPainterPath(); b.addEllipse(c, bis * pxmm, bis * pxmm); bi = QPainterPath(); bi.addEllipse(c, von * pxmm, von * pxmm)
            aus = aus.subtracted(b.subtracted(bi)).subtracted(innen)
            p.setBrush(QColor(60, 60, 70, 150)); p.drawPath(aus)
            p.setBrush(Qt.BrushStyle.NoBrush); p.setPen(QPen(QColor(220, 60, 40), 1.5, Qt.PenStyle.DashLine))
            p.drawEllipse(c, von * pxmm, von * pxmm); p.drawEllipse(c, bis * pxmm, bis * pxmm)
            p.setPen(Qt.PenStyle.NoPen)
        # Nabe
        p.setBrush(QColor(225, 228, 232)); p.drawEllipse(c, 21.0 * pxmm, 21.0 * pxmm)
        p.setBrush(QColor(180, 184, 190)); p.drawEllipse(c, 16.5 * pxmm, 16.5 * pxmm)
        p.setBrush(self.palette().window()); p.drawEllipse(c, 7.5 * pxmm, 7.5 * pxmm)
        p.end()

    # Maus: verschieben / zoomen
    def _pos(self, e):
        return e.position() if hasattr(e, "position") else QPointF(e.pos())

    def mousePressEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self._ziehen = (self._pos(e), self.label.dx, self.label.dy)

    def mouseMoveEvent(self, e):
        if self._ziehen and self.pix is not None:
            start, dx0, dy0 = self._ziehen
            d = self._pos(e) - start
            mm_je_px = (N * MM) / self.pix.width()
            self.label.dx = dx0 + d.x() * mm_je_px
            self.label.dy = dy0 + d.y() * mm_je_px
            self.geaendert(schnell=True)

    def mouseReleaseEvent(self, e):
        if self._ziehen:
            self._ziehen = None
            self.geaendert()

    def wheelEvent(self, e):
        st = e.angleDelta().y() / 120.0
        self.label.zoom = max(0.2, min(6.0, self.label.zoom * (1.06 ** st)))
        self.geaendert(zoom_slider=True)



# ---------------------------------------------------------------- Hauptfenster
class Hauptfenster(QMainWindow):
    def __init__(self, einst):
        super().__init__()
        self.setWindowTitle(f"{APP} {VERSION}")
        ic = os.path.join(basisordner(), "ls-label-studio.svg")
        if os.path.exists(ic):
            self.setWindowIcon(QIcon(ic))
        self.einst = einst
        self.ls64 = finde_ls64()
        self.label = Label()
        self.proc = None; self.phase = None; self.t_start = 0.0; self.puffer = ""; self.ausgabe = []
        os.makedirs(CACHE, exist_ok=True)
        self.stopdatei = os.path.join(CACHE, "stop")

        self.ansicht = DiscAnsicht(self.label, self.geaendert)
        self.timer = QTimer(self); self.timer.setSingleShot(True); self.timer.timeout.connect(self.ansicht.neu)

        tabs = QTabWidget()
        tabs.addTab(self._tab_bild(), T("tab_bild"))
        tabs.addTab(self._tab_text(), T("tab_text"))
        tabs.addTab(self._tab_brennen(), T("tab_brennen"))
        tabs.setMinimumWidth(420)

        split = QSplitter()
        split.addWidget(self.ansicht); split.addWidget(tabs)
        split.setStretchFactor(0, 3); split.setStretchFactor(1, 2)
        self.setCentralWidget(split)
        self.statusBar().showMessage(T("bereit") if self.ls64 else T("kein_ls64"))
        self.resize(1200, 780)
        self.ansicht.neu()
        QTimer.singleShot(300, self.einrichtung_pruefen)

    # ---------- Einrichtung (Tabellen aus der LightScribe System Software)
    def _ls64_lauf(self, args, timeout=120):
        try:
            r = subprocess.run([self.ls64] + args, capture_output=True, timeout=timeout, **prozess_ohne_fenster())
            return r.returncode, (r.stdout + r.stderr).decode("utf-8", "replace")
        except Exception as e:
            return 99, str(e)

    def einrichtung_pruefen(self):
        if not self.ls64:
            return
        code, _ = self._ls64_lauf(["einrichten", "--pruefen"], 30)
        if code != 0:
            self.einrichten_dialog()

    def einrichten_dialog(self):
        box = QMessageBox(self)
        box.setWindowTitle(f"{APP} – {T('einr_titel')}")
        box.setText(T("einr_text"))
        b_auto = box.addButton(T("einr_auto"), QMessageBox.ButtonRole.AcceptRole)
        b_datei = box.addButton(T("einr_datei"), QMessageBox.ButtonRole.ActionRole)
        box.addButton(T("einr_spaeter"), QMessageBox.ButtonRole.RejectRole)
        box.exec()
        k = box.clickedButton()
        if k is b_auto:
            self._einrichten([])
        elif k is b_datei:
            pfad, _ = QFileDialog.getOpenFileName(self, T("einr_titel"), os.path.expanduser("~"), T("einr_filter"))
            if pfad:
                self._einrichten([pfad])

    def _einrichten(self, args):
        QApplication.setOverrideCursor(QCursor(Qt.CursorShape.WaitCursor))
        code, txt = self._ls64_lauf(["einrichten"] + args)
        QApplication.restoreOverrideCursor()
        if code == 0:
            QMessageBox.information(self, APP, T("einr_ok") + "\n\n" + txt.strip())
        else:
            QMessageBox.warning(self, APP, T("einr_fehler") + txt.strip())
            self.einrichten_dialog()

    # ---------- Tab 1: Bild
    def _slider(self, lo, hi, wert, aenderung):
        s = QSlider(Qt.Orientation.Horizontal); s.setRange(lo, hi); s.setValue(wert)
        s.valueChanged.connect(aenderung)
        return s

    def _tab_bild(self):
        w = QWidget(); f = QFormLayout(w)
        knopf = QPushButton(T("bild_oeffnen")); knopf.clicked.connect(self.bild_oeffnen)
        self.lbl_datei = QLabel(T("kein_bild")); self.lbl_datei.setWordWrap(True)
        f.addRow(knopf); f.addRow(self.lbl_datei)
        self.s_zoom = self._slider(20, 600, 100, self._bild_werte)
        self.s_dreh = self._slider(-180, 180, 0, self._bild_werte)
        self.s_hell = self._slider(-100, 100, 0, self._lut_werte)
        self.s_kontr = self._slider(-100, 100, 0, self._lut_werte)
        self.s_gamma = self._slider(30, 300, 100, self._lut_werte)
        f.addRow(T("zoom"), self.s_zoom); f.addRow(T("drehen"), self.s_dreh)
        f.addRow(T("hell"), self.s_hell); f.addRow(T("kontrast"), self.s_kontr); f.addRow(T("mitte"), self.s_gamma)
        zur = QPushButton(T("zurueck")); zur.clicked.connect(self._bild_zurueck)
        f.addRow(zur)
        hinweis = QLabel(T("tipp_bild")); hinweis.setWordWrap(True); hinweis.setStyleSheet("color: gray")
        f.addRow(hinweis)
        sp = QPushButton(T("label_speichern")); sp.clicked.connect(self.label_speichern)
        f.addRow(sp)
        return w

    def bild_oeffnen(self):
        start = self.einst.value("ordner", os.path.expanduser("~"))
        pfad, _ = QFileDialog.getOpenFileName(self, T("bild_oeffnen"), start,
                                              T("bilder_filter") + " (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)")
        if not pfad:
            return
        self.einst.setValue("ordner", os.path.dirname(pfad))
        QApplication.setOverrideCursor(QCursor(Qt.CursorShape.WaitCursor))
        ok = self.label.bild_setzen(pfad)
        QApplication.restoreOverrideCursor()
        if not ok:
            QMessageBox.warning(self, APP, T("bild_fehler")); return
        self.lbl_datei.setText(os.path.basename(pfad))
        self._bild_zurueck(nur_regler=True)
        self.geaendert()

    def _bild_werte(self):
        self.label.zoom = self.s_zoom.value() / 100.0
        self.label.dreh = float(self.s_dreh.value())
        self.geaendert(schnell=True)

    def _lut_werte(self):
        self.label.hell = self.s_hell.value(); self.label.kontr = self.s_kontr.value()
        self.label.gamma = self.s_gamma.value() / 100.0
        self.label.anpassen()
        self.geaendert(schnell=True)

    def _bild_zurueck(self, nur_regler=False):
        for s, v in ((self.s_zoom, 100), (self.s_dreh, 0), (self.s_hell, 0), (self.s_kontr, 0), (self.s_gamma, 100)):
            s.blockSignals(True); s.setValue(v); s.blockSignals(False)
        self.label.zoom = 1.0; self.label.dreh = 0.0; self.label.dx = self.label.dy = 0.0
        self.label.hell = self.label.kontr = 0; self.label.gamma = 1.0
        self.label.anpassen()
        if not nur_regler:
            self.geaendert()

    def geaendert(self, schnell=False, zoom_slider=False):
        if zoom_slider:
            self.s_zoom.blockSignals(True); self.s_zoom.setValue(int(round(self.label.zoom * 100))); self.s_zoom.blockSignals(False)
        self.timer.start(30 if schnell else 0)

    def label_speichern(self):
        pfad, _ = QFileDialog.getSaveFileName(self, T("label_speichern"), os.path.join(os.path.expanduser("~"), "label.png"), "PNG (*.png)")
        if pfad:
            self.label.rendern().save(pfad)

    # ---------- Tab 2: Text
    def _tab_text(self):
        w = QWidget(); v = QVBoxLayout(w)
        for i, z in enumerate(self.label.texte):
            box = QGroupBox(T("text_n") % (i + 1)); box.setCheckable(True); box.setChecked(z.aktiv)
            f = QFormLayout(box)
            ed = QLineEdit(z.text); ed.setPlaceholderText(T("text_platzhalter"))
            art = QComboBox(); art.addItems(T("art_liste"))
            art.setCurrentIndex({"bogen_oben": 0, "bogen_unten": 1, "gerade": 2}[z.art])
            pos = QDoubleSpinBox(); pos.setRange(-58.0, 58.0); pos.setDecimals(1); pos.setSuffix(" mm"); pos.setValue(z.pos)
            gr = QDoubleSpinBox(); gr.setRange(1.5, 40.0); gr.setDecimals(1); gr.setSuffix(" mm"); gr.setValue(z.groesse)
            schrift = QFontComboBox(); schrift.setCurrentFont(QFont(z.schrift))
            fett = QCheckBox(T("fett")); fett.setChecked(z.fett)
            weiss = QCheckBox(T("weiss")); weiss.setChecked(z.weiss)
            zeile = QHBoxLayout(); zeile.addWidget(fett); zeile.addWidget(weiss)
            f.addRow(ed); f.addRow(T("art"), art); f.addRow(T("radius"), pos); f.addRow(T("groesse"), gr)
            f.addRow(T("schrift"), schrift); f.addRow(zeile)

            def upd(*_, z=z, box=box, ed=ed, art=art, pos=pos, gr=gr, schrift=schrift, fett=fett, weiss=weiss):
                z.aktiv = box.isChecked(); z.text = ed.text()
                z.art = ["bogen_oben", "bogen_unten", "gerade"][art.currentIndex()]
                z.pos = pos.value(); z.groesse = gr.value(); z.schrift = schrift.currentFont().family()
                z.fett = fett.isChecked(); z.weiss = weiss.isChecked()
                self.geaendert(schnell=True)
            box.toggled.connect(upd); ed.textChanged.connect(upd); art.currentIndexChanged.connect(upd)
            pos.valueChanged.connect(upd); gr.valueChanged.connect(upd); schrift.currentFontChanged.connect(upd)
            fett.toggled.connect(upd); weiss.toggled.connect(upd)
            v.addWidget(box)
        h = QLabel(T("tipp_text")); h.setWordWrap(True); h.setStyleSheet("color: gray"); v.addWidget(h)
        v.addStretch(1)
        sc = QScrollArea(); sc.setWidget(w); sc.setWidgetResizable(True)
        return sc

    # ---------- Tab 3: Brennen
    def _tab_brennen(self):
        w = QWidget(); v = QVBoxLayout(w)
        gb = QGroupBox(T("modus")); f = QFormLayout(gb)
        self.r_std = QRadioButton(T("ganz")); self.r_ring = QRadioButton(T("ring"))
        self.r_std.setChecked(True)
        self.sp_von = QDoubleSpinBox(); self.sp_von.setRange(R_INNEN, R_AUSSEN); self.sp_von.setDecimals(1); self.sp_von.setSuffix(" mm"); self.sp_von.setValue(46.0)
        self.sp_bis = QDoubleSpinBox(); self.sp_bis.setRange(R_INNEN, R_AUSSEN); self.sp_bis.setDecimals(1); self.sp_bis.setSuffix(" mm"); self.sp_bis.setValue(R_AUSSEN)
        self.sp_von.setEnabled(False); self.sp_bis.setEnabled(False)
        ringz = QHBoxLayout(); ringz.addWidget(QLabel(T("von"))); ringz.addWidget(self.sp_von); ringz.addWidget(QLabel(T("bis"))); ringz.addWidget(self.sp_bis)
        f.addRow(self.r_std); f.addRow(self.r_ring); f.addRow(T("ring_lbl"), ringz)
        self.cb_q = QComboBox()
        for k in ("normal", "draft", "best"):
            self.cb_q.addItem(T("q_" + k), k)
        f.addRow(T("qualitaet"), self.cb_q)
        for x in (self.r_std, self.r_ring):
            x.toggled.connect(self._modus)
        self.sp_von.valueChanged.connect(self._modus); self.sp_bis.valueChanged.connect(self._modus)
        v.addWidget(gb)

        gl = QGroupBox(T("laufwerk")); fl = QFormLayout(gl)
        self.cb_dev = QComboBox(); self.cb_dev.setEditable(True)
        self.cb_dev.addItems(laufwerke())
        letzt = self.einst.value("geraet", "")
        if letzt:
            self.cb_dev.setCurrentText(letzt)
        b_pr = QPushButton(T("pruefen")); b_pr.clicked.connect(self.disc_pruefen)
        self.lbl_disc = QLabel(T("nicht_geprueft")); self.lbl_disc.setWordWrap(True)
        fl.addRow(T("geraet"), self.cb_dev); fl.addRow(b_pr); fl.addRow(self.lbl_disc)
        v.addWidget(gl)

        self.b_vor = QPushButton(T("exakt")); self.b_vor.clicked.connect(self.exakte_vorschau)
        self.b_bren = QPushButton(T("brennen")); self.b_bren.clicked.connect(self.brennen)
        self.b_bren.setStyleSheet("font-weight: bold; padding: 8px")
        self.b_abbr = QPushButton(T("abbrechen")); self.b_abbr.clicked.connect(self.abbrechen); self.b_abbr.setEnabled(False)
        self.fort = QProgressBar(); self.fort.setRange(0, 100); self.fort.setValue(0)
        self.lbl_stat = QLabel(""); self.lbl_stat.setWordWrap(True)
        v.addWidget(self.b_vor); v.addWidget(self.b_bren); v.addWidget(self.fort); v.addWidget(self.lbl_stat); v.addWidget(self.b_abbr)
        self.log = QPlainTextEdit(); self.log.setReadOnly(True); self.log.setMaximumBlockCount(400)
        self.log.setPlaceholderText(T("ausgabe"))
        v.addWidget(self.log, 1)
        unten = QHBoxLayout()
        sp = QComboBox(); sp.addItem("Deutsch", "de"); sp.addItem("English", "en")
        sp.setCurrentIndex(0 if SPRACHE == "de" else 1)
        sp.currentIndexChanged.connect(lambda i, sp=sp: self._sprache(sp.itemData(i)))
        ueber = QPushButton(T("ueber")); ueber.setFlat(True); ueber.clicked.connect(self.ueber)
        unten.addWidget(sp); unten.addStretch(1); unten.addWidget(ueber)
        v.addLayout(unten)
        return w

    def _sprache(self, s):
        self.einst.setValue("sprache", s)
        QMessageBox.information(self, APP, TEXTE["sprache_neustart"][0 if s == "de" else 1])

    def ueber(self):
        ver = ""
        if self.ls64:
            ver = self._ls64_lauf(["version"], 5)[1].strip()
        QMessageBox.about(self, APP, f"<b>{APP} {VERSION}</b><br>{T('ueber_text')}"
                          f"<br><br>{T('brennprogramm')}: {ver or T('nicht_gefunden')}<br>Qt: {QT}"
                          f"<br><br>{T('ueber_lizenz')}<br><a href='https://github.com/Efratsy/ls-label-studio'>github.com/Efratsy/ls-label-studio</a>"
                          f"<br><br>{T('ueber_ki')}<br><br><small>{T('ueber_marke')}</small>")

    def _modus(self):
        ring = self.r_ring.isChecked()
        self.sp_von.setEnabled(ring); self.sp_bis.setEnabled(ring)
        self.ansicht.band = (min(self.sp_von.value(), self.sp_bis.value()), max(self.sp_von.value(), self.sp_bis.value())) if ring else None
        self.ansicht.update()

    def _args_spuren(self, bild, lsp, vorschau=None):
        a = [self.ls64, "spuren", bild, lsp, "-q", self.cb_q.currentData()]
        if self.ansicht.band:
            a += ["-b", "%.2f:%.2f" % self.ansicht.band]
        if vorschau:
            a += ["-v", vorschau]
        return a

    def _schreibe_label(self):
        bmp = os.path.join(CACHE, "label.bmp")          # BMP: liest ls64 auf allen Systemen
        if not self.label.rendern().save(bmp, "BMP"):
            raise RuntimeError(bmp)
        return bmp

    # ---------- Prozesse
    def _starte(self, phase, args):
        self.phase = phase; self.puffer = ""
        self.proc = QProcess(self)
        self.proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.proc.readyReadStandardOutput.connect(self._lesen)
        self.proc.finished.connect(self._fertig)
        self.log.appendPlainText("$ " + " ".join(args))
        self._knoepfe(False)
        self.proc.start(args[0], args[1:])

    def _knoepfe(self, frei):
        self.b_bren.setEnabled(frei); self.b_vor.setEnabled(frei)
        self.b_abbr.setEnabled(not frei and self.phase == "brennen")

    def _lesen(self):
        if self.proc is None:
            return
        txt = bytes(self.proc.readAllStandardOutput()).decode("utf-8", "replace")
        self.puffer += txt
        teile = re.split(r"[\r\n]", self.puffer)
        self.puffer = teile.pop()
        for t in teile:
            t = t.strip()
            if not t:
                continue
            m = re.search(r"Brenne:\s+(\d+)\s*%.*vergangen\s+(\S+)(?:\s+Rest ca\.\s+(\S+))?", t)
            if m:
                self.fort.setValue(int(m.group(1)))
                s = T("brennt") % (m.group(1), m.group(2)) + (T("rest") % m.group(3) if m.group(3) else "")
                if "Laufwerk laeuft an" in t:
                    s += "\n" + T("laeuft_an")
                self.lbl_stat.setText(s)
                continue
            self.log.appendPlainText(t)
            self.ausgabe.append(t)

    def _fertig(self, code=0, status=None):
        self._lesen()
        if self.puffer.strip():
            self.log.appendPlainText(self.puffer.strip()); self.ausgabe.append(self.puffer.strip())
        phase = self.phase; self.phase = None
        self._knoepfe(True)
        ausgabe = "\n".join(self.ausgabe)
        if code == 4 or "Tabellen fehlen" in ausgabe:
            QMessageBox.warning(self, APP, T("einr_fehlt")); self.einrichten_dialog(); return
        if phase == "info":
            self._info_auswerten(ausgabe)
        elif phase == "vorschau":
            if code == 0:
                self._zeige_vorschau(os.path.join(CACHE, "vorschau.bmp"), ausgabe)
            else:
                QMessageBox.warning(self, APP, T("vorschau_fehler") + ausgabe[-800:])
        elif phase == "spuren":
            if code != 0:
                QMessageBox.warning(self, APP, T("berech_fehler") + ausgabe[-800:]); return
            m = re.search(r"Brenndauer\s*:\s*ca\.\s*(\S+)", ausgabe)
            r = QMessageBox.question(self, APP, T("frage_brennen") % (m.group(1) if m else "?"))
            if r != QMessageBox.StandardButton.Yes:
                self.lbl_stat.setText(T("nicht_gebrannt")); return
            self.ausgabe = []; self.t_start = time.time(); self.fort.setValue(0)
            self.lbl_stat.setText(T("laeuft_an"))
            try:
                if os.path.exists(self.stopdatei):
                    os.remove(self.stopdatei)
            except OSError:
                pass
            args = [self.ls64, "brennen", os.path.join(CACHE, "label.lsp"), "-g", self.cb_dev.currentText(), "--ja",
                    "--stopdatei", self.stopdatei, "--log", os.path.join(CACHE, "letzter_brand.bin")]
            inh = None if WINDOWS else shutil.which("systemd-inhibit")
            if inh:
                args = [inh, "--what=sleep:idle", "--who=LS Label Studio", "--why=Disc labeling"] + args
            self._starte("brennen", args)
        elif phase == "brennen":
            if "Fertig! Das Label ist gebrannt." in ausgabe:
                self.fort.setValue(100)
                self.lbl_stat.setText(T("fertig"))
                QMessageBox.information(self, APP, T("fertig_box"))
            elif "abgebrochen" in ausgabe:
                self.lbl_stat.setText(T("abgebrochen"))
            else:
                self.lbl_stat.setText(T("fehler"))
                QMessageBox.warning(self, APP, T("brenn_fehler") + ausgabe[-1000:] + self._rechte_hinweis(ausgabe))

    def _rechte_hinweis(self, txt):
        if "nicht oeffnen" in txt or "Permission" in txt or "not permitted" in txt or "Administrator" in txt:
            return T("rechte_win") if WINDOWS else T("rechte_linux")
        return ""

    def _bereit(self):
        if not self.ls64:
            QMessageBox.critical(self, APP, T("kein_ls64")); return False
        if self.proc is not None and self.proc.state() != QProcess.ProcessState.NotRunning:
            return False
        return True

    def disc_pruefen(self):
        if not self._bereit():
            return
        self.einst.setValue("geraet", self.cb_dev.currentText())
        self.ausgabe = []; self.lbl_disc.setText(T("pruefe"))
        self._starte("info", [self.ls64, "info", self.cb_dev.currentText()])

    def _info_auswerten(self, t):
        if "BEREIT" in t:
            m = re.search(r"Modell\s*:\s*(.+)", t)
            self.lbl_disc.setText(T("disc_bereit") + (f"  ({m.group(1).split('(')[0].strip()})" if m else ""))
        elif "erkennt die Disc gerade noch" in t:
            self.lbl_disc.setText(T("disc_erkennt"))
        elif "LightScribe-Disc: nein" in t or "Labelseite unten: nein" in t or "falsch herum" in t:
            self.lbl_disc.setText(T("disc_falsch"))
        elif "Disc eingelegt : nein" in t or "Keine Disc" in t:
            self.lbl_disc.setText(T("disc_keine"))
        else:
            self.lbl_disc.setText(T("disc_nicht") + self._rechte_hinweis(t))

    def exakte_vorschau(self):
        if not self._bereit():
            return
        try:
            bild = self._schreibe_label()
        except Exception as e:
            QMessageBox.warning(self, APP, str(e)); return
        self.ausgabe = []
        self._starte("vorschau", self._args_spuren(bild, os.path.join(CACHE, "vorschau.lsp"), os.path.join(CACHE, "vorschau.bmp")))

    def _zeige_vorschau(self, pfad, txt):
        d = QDialog(self); d.setWindowTitle(T("vorschau_titel"))
        l = QVBoxLayout(d)
        lab = QLabel(); pm = QPixmap(pfad)
        lab.setPixmap(pm.scaled(720, 720, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation))
        l.addWidget(lab)
        m = re.search(r"Brenndauer\s*:\s*ca\.\s*(\S+)", txt)
        l.addWidget(QLabel(T("dauer") % m.group(1) if m else ""))
        d.exec()

    def brennen(self):
        if not self._bereit():
            return
        if self.label.grau is None and not any(z.aktiv and z.text.strip() for z in self.label.texte):
            QMessageBox.information(self, APP, T("leer")); return
        self.einst.setValue("geraet", self.cb_dev.currentText())
        try:
            bild = self._schreibe_label()
        except Exception as e:
            QMessageBox.warning(self, APP, str(e)); return
        self.ausgabe = []
        self.lbl_stat.setText(T("berechne"))
        self._starte("spuren", self._args_spuren(bild, os.path.join(CACHE, "label.lsp")))

    def abbrechen(self):
        if self.proc is not None and self.phase == "brennen":
            r = QMessageBox.question(self, APP, T("wirklich_abbrechen"))
            if r == QMessageBox.StandardButton.Yes:
                try:      # ls64 prueft die Stopdatei jede Sekunde und beendet dann sauber (alle Systeme)
                    with open(self.stopdatei, "w") as f:
                        f.write("stop")
                except OSError:
                    pass
                self.lbl_stat.setText(T("breche_ab"))

    def closeEvent(self, e):
        if self.phase == "brennen":
            QMessageBox.warning(self, APP, T("schliessen_brennt"))
            e.ignore(); return
        e.accept()


# ---------------------------------------------------------------- Selbsttest (ohne Fenster)
def selbsttest():
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    app = QApplication(sys.argv)
    print("Qt:", QT, "| numpy:", "yes" if np is not None else "no", "|", sys.platform)
    lb = Label()
    t0 = time.time()
    testbild = os.path.join(basisordner(), "testbild.png")
    if os.path.exists(testbild):
        print("load image:", "ok" if lb.bild_setzen(testbild) else "FAILED")
    lb.hell, lb.kontr, lb.gamma = 10, 20, 1.1; lb.anpassen()
    lb.texte[0].aktiv, lb.texte[0].text = True, "LS LABEL STUDIO"
    lb.texte[1].aktiv, lb.texte[1].text = True, "self test"
    lb.texte[2].aktiv, lb.texte[2].text = True, "Straight line"
    os.makedirs(CACHE, exist_ok=True)
    bmp = os.path.join(CACHE, "selbsttest.bmp")
    ok = lb.rendern().save(bmp, "BMP")
    print("render label:", "ok" if ok else "FAILED", "(%.1f s)" % (time.time() - t0), bmp)
    ls = finde_ls64()
    print("ls64:", ls or "NOT FOUND")
    if not (ls and ok):
        return 1
    r = subprocess.run([ls, "einrichten", "--pruefen"], capture_output=True, **prozess_ohne_fenster())
    print(r.stdout.decode("utf-8", "replace").strip())
    if r.returncode != 0:
        print("tables missing -> run 'ls64 einrichten' (setup) first; skipping track test")
        return 0
    r = subprocess.run([ls, "spuren", bmp, os.path.join(CACHE, "selbsttest.lsp"), "-q", "normal",
                        "-v", os.path.join(CACHE, "selbsttest_vorschau.bmp")], capture_output=True, **prozess_ohne_fenster())
    print(r.stdout.decode("utf-8", "replace").strip())
    print("ls64 spuren:", "ok" if r.returncode == 0 else "FAILED " + r.stderr.decode("utf-8", "replace"))
    return 0 if r.returncode == 0 else 1


def main():
    if "--selbsttest" in sys.argv:
        sys.exit(selbsttest())
    app = QApplication(sys.argv)
    app.setApplicationName(APP); app.setApplicationVersion(VERSION); app.setDesktopFileName("ls-label-studio")
    app.setWindowIcon(QIcon.fromTheme("ls-label-studio"))
    einst = QSettings("ls-label-studio", "LSLabelStudio")
    sprache_bestimmen(einst)
    w = Hauptfenster(einst); w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
