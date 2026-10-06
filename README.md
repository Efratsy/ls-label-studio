<p align="center"><img src="logo.svg" width="96" alt=""></p>

<h1 align="center">LS Label Studio</h1>
<p align="center"><b>Design and burn LightScribe disc labels on modern 64-bit Linux and Windows.</b><br>
<a href="README.de.md">Deutsch</a> · <a href="https://github.com/Efratsy/ls-label-studio/releases/latest">Download</a> · <a href="docs/README.md">How it was built</a></p>

---

LightScribe drives can burn a grayscale image onto the top side of special discs. The official
software was discontinued long ago. On Linux it only exists as a 32-bit library, and current
systems no longer support it well.

**LS Label Studio** is a new, native 64-bit implementation. It talks to the drive directly and reproduces
the image processing of the original engine, so labels look the same as with the original Windows
software. Burn time is the same as well: about 27 minutes for a full disc.

<p align="center"><img src="docs/images/burn_quality_details.jpg" width="720" alt="Detail photos of burned labels: early version vs. original software vs. LS Label Studio"></p>

## Features

- **Image:** open PNG/JPG/BMP/WebP/TIFF, then zoom, rotate, move, and adjust brightness, contrast and midtones. A live disc preview shows the result.
- **Text:** up to three lines, each curved along the top or bottom of the disc or straight, with your choice of font, size, bold and white.
- **Modes:** full disc, or **ring only** (for example just a title), which is much faster.
- **Quality:** Standard (1015 tracks/inch, recommended), Fast (760) or Fine (1398).
- **Exact dot preview:** shows how the laser marks will be distributed before you burn.
- **Clean cancel:** tracks already sent are finished, then the drive stops properly.
- **Languages:** English and German.
- **Linux:** burn without root after installation (`cap_sys_rawio`).
- **Windows:** a single `.exe` (runs as administrator for direct drive access).
- **Command line tool `ls64`** for scripting.

## Download

See the **[latest release](https://github.com/Efratsy/ls-label-studio/releases/latest)**:

| System | File |
|---|---|
| Windows 10/11 (64-bit) | `LS-Label-Studio-1.1-windows-x64.exe` |
| Linux x86-64 | `ls-label-studio-1.1-linux-x86_64.tar.gz` |

## One-time setup: LightScribe System Software

LS Label Studio does **not** contain any code or data from HP. On first start it reads the drive
parameter tables and halftone tables **once** from your own copy of the original
*LightScribe System Software 1.18.27.10* and stores them locally:

- **Windows:** have the LightScribe System Software installed, then choose *Search automatically*.
- **Linux:** select the package `lightscribe-1.18.27.10-linux-2.6-intel.deb` or `.rpm`. Nothing needs to be installed from it.

The program verifies the tables with a checksum. Only the version it was tested with is accepted.

## Installation

**Linux**
```bash
tar xzf ls-label-studio-1.1-linux-x86_64.tar.gz
cd ls-label-studio-1.1
bash linux/install.sh /path/to/lightscribe-1.18.27.10-linux-2.6-intel.deb   # run WITHOUT sudo
```
Then start **LS Label Studio** from the application menu.

The script asks for your password itself. It installs `ls64` to `/usr/local/bin`, enables burning
without root, installs PyQt6 if needed and adds the menu entry.

**Windows**

Start `LS-Label-Studio-1.1-windows-x64.exe` and confirm the administrator prompt. SmartScreen may
warn about an unknown publisher; choose *More info → Run anyway*.

## Using it

1. Insert a LightScribe disc **label side down** and wait about one minute.
2. **Image** tab: open an image and adjust it. **Text** tab: add titles.
3. **Burn** tab: click *Check disc*, then *Burn*.

## Tested hardware

| Drive | Media | Result |
|---|---|---|
| HL-DT-ST (LG) GH24LS50 | LightScribe media (media IDs 112 / 114) | ✔ Linux and Windows |

The drive parameters of all 123 drive models known to the original software are read during setup,
so other LightScribe drives are expected to work. Reports are welcome.

## How it was built

The project started from a 32-bit Linux package and ended with a native implementation on both platforms.
The complete process is documented as a technical report: [docs/](docs/README.md). It covers
methodology, drive protocol, geometry, image quality analysis, halftoning, color conversion, the
cross-platform port and measured results.

> **AI assistance:** This project was developed with the assistance of an AI model
> (Claude, Anthropic). All results were verified on real hardware.

## License and notices

- **Program:** freeware. Free to use, no warranty. See [LICENSE.md](LICENSE.md).
- **Documentation and images in `docs/`:** [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
- *LightScribe* is a trademark of HP Development Company, L.P. This project is independent and not
  affiliated with or endorsed by HP. See [NOTICE.md](NOTICE.md).
