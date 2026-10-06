<p align="center"><img src="logo.svg" width="96" alt=""></p>

<h1 align="center">LS Label Studio</h1>
<p align="center"><b>LightScribe-Labels unter modernem 64-bit-Linux und -Windows gestalten und brennen.</b><br>
<a href="README.md">English</a> · <a href="https://github.com/Efratsy/ls-label-studio/releases/latest">Download</a> · <a href="docs/README.md">Wie es entstanden ist (englisch)</a></p>

---

LightScribe-Laufwerke brennen ein Graustufenbild auf die Oberseite spezieller Discs. Die offizielle
Software wird seit Langem nicht mehr gepflegt. Unter Linux gab es sie nur als 32-bit-Bibliothek,
und aktuelle Systeme unterstützen das kaum noch.

**LS Label Studio** ist eine komplett neue, native 64-bit-Umsetzung. Sie spricht direkt mit dem
Laufwerk und bildet die Bildverarbeitung der Original-Engine nach. Die Labels sehen deshalb aus wie mit der
Windows-Originalsoftware. Auch die Brenndauer ist gleich: etwa 27 Minuten für eine ganze Disc.

<p align="center"><img src="docs/images/burn_quality_details.jpg" width="720" alt="Detailfotos gebrannter Labels"></p>

## Funktionen

- **Bild:** PNG/JPG/BMP/WebP/TIFF öffnen, dann zoomen, drehen, verschieben, Helligkeit, Kontrast und Mitteltöne einstellen. Eine Live-Disc-Vorschau zeigt das Ergebnis.
- **Text:** bis zu drei Zeilen, jeweils im Bogen oben, im Bogen unten oder gerade, mit Schrift, Größe, fett und weiß.
- **Modi:** ganze Disc oder **nur Ring** (z. B. nur ein Titel), das geht deutlich schneller.
- **Qualität:** Standard (1015 Spuren/Zoll, empfohlen), Schnell (760) oder Fein (1398).
- **Exakte Punkt-Vorschau** vor dem Brennen.
- **Sauberer Abbruch:** Bereits gesendete Spuren werden noch fertig gebrannt, dann stoppt das Laufwerk ordentlich.
- **Sprachen:** Deutsch und Englisch.
- **Linux:** nach der Installation Brennen ohne Root-Rechte.
- **Windows:** eine einzige `.exe` (läuft als Administrator für den direkten Laufwerkszugriff).
- **Kommandozeilenprogramm `ls64`** für Skripte.

## Download

Siehe **[neuestes Release](https://github.com/Efratsy/ls-label-studio/releases/latest)**:

| System | Datei |
|---|---|
| Windows 10/11 (64-bit) | `LS-Label-Studio-1.1-windows-x64.exe` |
| Linux x86-64 | `ls-label-studio-1.1-linux-x86_64.tar.gz` |

## Einmalige Einrichtung: LightScribe System Software

LS Label Studio enthält **keinen** Code und **keine** Daten von HP. Beim ersten Start liest das Programm
die Laufwerks- und Halbton-Tabellen **einmalig** aus deiner eigenen Kopie der originalen
*LightScribe System Software 1.18.27.10* und speichert sie lokal:

- **Windows:** die LightScribe System Software installiert haben, dann *Automatisch suchen* wählen.
- **Linux:** das Paket `lightscribe-1.18.27.10-linux-2.6-intel.deb` oder `.rpm` auswählen. Installieren muss man daraus nichts.

Die Tabellen werden per Prüfsumme kontrolliert. Akzeptiert wird nur die getestete Version.

## Installation

**Linux**
```bash
tar xzf ls-label-studio-1.1-linux-x86_64.tar.gz
cd ls-label-studio-1.1
bash linux/install.sh /pfad/zu/lightscribe-1.18.27.10-linux-2.6-intel.deb   # OHNE sudo starten
```
Danach **LS Label Studio** im Startmenü öffnen.

Das Skript fragt selbst nach dem Passwort. Es installiert `ls64`, richtet das Brennen ohne Root ein,
installiert bei Bedarf PyQt6 und legt den Menüeintrag an.

**Windows**

`LS-Label-Studio-1.1-windows-x64.exe` starten und die Administrator-Abfrage bestätigen. SmartScreen
warnt eventuell vor einem unbekannten Herausgeber: *Weitere Informationen → Trotzdem ausführen*.

## Bedienung

1. LightScribe-Disc mit der **Labelseite nach unten** einlegen und ca. 1 Minute warten.
2. Tab **Bild**: Bild öffnen und anpassen. Tab **Text**: Titel hinzufügen.
3. Tab **Brennen**: *Disc prüfen*, dann *Brennen*.

## Getestete Hardware

| Laufwerk | Medien | Ergebnis |
|---|---|---|
| HL-DT-ST (LG) GH24LS50 | LightScribe-Medien (Medien-ID 112 / 114) | ✔ Linux und Windows |

Bei der Einrichtung werden die Parameter aller 123 Laufwerksbeschreibungen der Originalsoftware gelesen.
Andere LightScribe-Laufwerke sollten daher ebenfalls funktionieren. Rückmeldungen sind willkommen.

## Wie es entstanden ist

Der gesamte Weg vom 32-bit-Paket zur nativen Umsetzung auf beiden Systemen ist als technischer Bericht
dokumentiert (englisch): [docs/](docs/README.md).

> **KI-Unterstützung:** Dieses Projekt wurde mit Unterstützung eines KI-Modells (Claude, Anthropic)
> entwickelt. Alle Ergebnisse wurden an echter Hardware überprüft.

## Aus dem Quellcode bauen

```bash
make -C src dynamisch      # Linux: ls64 (gcc, libpng, zlib)
bash linux/install.sh /pfad/zu/lightscribe-1.18.27.10-linux-2.6-intel.deb
```
Windows: `windows\build_windows.bat` ausführen (Python 3.10+ nötig; erzeugt `dist\LS-Label-Studio.exe`).

Beiträge sind willkommen, besonders Rückmeldungen zu **anderen LightScribe-Laufwerken**
(Ausgabe von `ls64 info` und ein Foto). **Bitte niemals Dateien, Tabellen oder Programme der
LightScribe System Software in dieses Repository legen.**

## Lizenz und Hinweise

- **Programm und Quellcode:** [GNU General Public License v3.0 oder neuer](LICENSE).
- **Dokumentation und Bilder in `docs/`:** [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/deed.de).
- *LightScribe* ist eine Marke von HP Development Company, L.P. Dieses Projekt ist unabhängig und
  nicht mit HP verbunden. Siehe [NOTICE.md](NOTICE.md).
