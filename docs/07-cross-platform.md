# 7 Cross-platform and distribution

## 7.1 Structure

| Part | Language | Role |
|---|---|---|
| `ls64` | C (no dependencies beyond libc; libpng and zlib on Linux) | drive control, image → tracks, burning, table setup |
| LS Label Studio | Python + Qt (PyQt6/PySide6) | label design, preview, calls `ls64` |

`ls64` is self-contained and can be scripted. The GUI renders the label (image, adjustments, text)
into a bitmap and lets `ls64` do the rest.

## 7.2 Platform layer

All operating-system specifics are in one module:

| | Linux | Windows |
|---|---|---|
| Drive access | `ioctl(SG_IO)` on `/dev/srN` | `IOCTL_SCSI_PASS_THROUGH_DIRECT` on `\\.\X:` |
| Permissions | file capability `cap_sys_rawio` (set by the installer) | administrator (UAC prompt) |
| Keep awake | `systemd-inhibit` (GUI) | `SetThreadExecutionState` |
| Unicode paths | UTF-8 | UTF-8 ↔ UTF-16 (`_wfopen`, `CommandLineToArgvW`) |
| Cancel | stop file + signals | stop file + console handler |

**Build.** The Windows build was compiled on the target PC with the Zig toolchain
(`zig cc -target x86_64-windows-gnu`). PyInstaller turned it into a single `.exe` with the GUI
included. The Windows code path compiled and burned correctly on its first build.

## 7.3 No third-party data in the release

The re-implementation needs two sets of tables from the original software:

- the drive/media laser parameters
- the halftone tables (17 920 bytes)

They are **not shipped**. On first start, the program extracts them from the user's own copy of the
*LightScribe System Software 1.18.27.10*:

- **Sources:** `liblightscribe.so.1`, the Linux `.deb` or `.rpm` package (gzip-compressed payload
  unpacked in memory), or the Windows `LSPrtEn.dll`.
- **Drive table:** parsed from the embedded XML descriptors.
- **Halftone tables:** located by their structure and accepted only if their **SHA-256 checksum**
  matches the version the algorithm was verified with.

All four sources yield an identical local data file. With the extracted tables, the output is
bit-identical to the previous release, which had them built in.

## 7.4 User interface

- live disc preview with the printable ring, the hub, and the non-burned area dimmed in ring mode
- image adjustments with a lookup table, three text lines (curved or straight)
- exact dot preview computed by `ls64`
- progress, remaining time and a clean cancel
- English and German
