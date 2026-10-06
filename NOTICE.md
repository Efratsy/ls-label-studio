# Notice

- *LightScribe* is a trademark of HP Development Company, L.P. "LS Label Studio" is an independent
  project. It is not affiliated with, sponsored by or endorsed by HP. The name LightScribe is used only
  to describe compatibility.
- LS Label Studio is free software under the GNU GPL v3.0 or later (see `LICENSE`). Documentation
  and images in `docs/` are licensed under CC BY 4.0.
- This repository and its releases contain **no code and no data from the LightScribe System
  Software**. The program reads the drive parameter tables and halftone tables once from a copy of
  the *LightScribe System Software 1.18.27.10* that the user provides, and stores them locally. It
  does not modify, patch or redistribute that software.
- The drive protocol and the image pipeline were reconstructed for **interoperability**: to keep
  using one's own legally acquired drive and media on current operating systems. The methodology is
  documented in [docs/02-methodology.md](docs/02-methodology.md).
- Third-party components of the binary releases: Python (PSF License), Qt 6 / PyQt6 (GPL v3,
  Riverbank Computing / The Qt Company), and for `ls64` on Linux: libpng (libpng license) and zlib
  (zlib license).
- Photos in `docs/images` show only details of labels burned during development. Third-party
  artwork used privately during testing is intentionally not published.
