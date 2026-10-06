# Changelog

## 1.1 – 2026-10-05
- **Windows 10/11 (64-bit) support:** drive access via SCSI pass-through, single-file `.exe`.
- One-time setup reads the drive and halftone tables from the user's own LightScribe System Software
  (`.deb`, `.rpm`, `liblightscribe.so.1` or `LSPrtEn.dll`). No HP data is shipped anymore.
- English/German user interface.
- Clean cancel via stop file on all platforms.
- Renamed to **LS Label Studio**.

## 1.0 – 2026-10-04
- First release for Linux: graphical label designer (image, text, ring mode, exact dot preview).
- `ls64`: native 64-bit drive control, halftoning compatible with the original engine, color
  conversion matching the original, calibrated burn-time estimate.
