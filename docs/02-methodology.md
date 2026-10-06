# 2 Methodology

## 2.1 Black-box observation first

The engine library is stripped and undocumented. The first source of truth was therefore **its
behavior on the wire**: the SCSI commands it sends to the drive.

- On Linux, the library talks to the drive only through `ioctl(SG_IO)`, which passes raw SCSI/MMC
  commands. No kernel module or daemon is involved.
- A small preload library intercepted these calls of the program we started ourselves. It logged
  every command with its data and timing into a binary log format.
- Sessions were recorded for drive enumeration, media detection, preview and **one real burn** of a
  test image in the smallest mode, to save discs.

## 2.2 A simulated drive

From the recorded answers, a **simulated drive** was built. It answers all commands like the real
drive with a label-side-down disc, accepts the track data and writes it to a log. The real drive is
never touched.

The original engine could now be "burned" with *any* image, quality and mode in seconds and
without discs. This produced **reference data**: for a given image, exactly the bits the original
engine sends.

Validation: a burn recorded on the real drive and the same job on the simulator gave
**byte-identical** track data (all 153 packets).

## 2.3 Reference-driven development

Every part of the re-implementation was compared against reference data from the original engine:

- **Packet structure, geometry:** compared byte by byte.
- **Tone reproduction:** mean mark density per gray level, in windows.
- **Pattern statistics:** mean run length of burned and unburned marks.
- **Bit agreement:** where an exact reproduction was the goal.

Test images were synthetic where possible: flat gray fields, gray wedges, orientation marks, radius
rings, color swatches. That way each property could be measured in isolation.

## 2.4 Studying the Windows software

When the native labels turned out paler than the Windows reference, the Windows engine came into
focus. It was examined in two ways:

1. **Statically:** class names, exported functions and the type library of its COM server. This
   showed a newer engine generation than on Linux.
2. **Through its own public test interface:** the official COM server has a test method that
   writes the complete burn job (laser parameters and all tracks) **to a file instead of the drive**.

With the second route, the exact Windows output for any image could be captured with a small client
program of our own and compared bit by bit (chapter 5).

**What was deliberately not done:**

- Injecting code into or patching running processes, proxy DLLs, or import-table hooks were considered
  and rejected. These techniques are typical of malware and unnecessary for the task.
- All experiments used either read-only commands or the engine's own interfaces.
- Real burns happened only with explicit confirmation and a disc inserted for that purpose.

## 2.5 Interoperability and third-party material

The goal was interoperability: continuing to use legally owned drives and media on current systems.

- The published program contains **no code and no data** from the original software. It needs two
  sets of tables (drive laser parameters and halftone tables). At setup, it extracts them **from the
  user's own copy** of the LightScribe System Software and checks them against a checksum.
- This report describes formats and algorithms **in our own words**. Proprietary tables, code and
  addresses are not reproduced.

## 2.6 Role of the AI model

All analysis, code and documentation were produced in a dialogue between the project owner and an AI
model (Claude, Anthropic). The owner provided the hardware, ran every test on real drives and judged
the label quality. The model:

- proposed and carried out the analysis steps
- wrote all programs
- evaluated recorded logs and measurements
- documented each step

For a few parallel sub-tasks, several model instances were used, such as statically analysing the
halftoning code while another instance measured image geometry. Their results were cross-checked
against each other and against the reference data.

Wrong turns are part of the record. For example, a suspected "image enhancement" in the Windows engine
turned out not to be used, and an early analysis read the bit order of the reference data wrongly.
These were corrected by later measurements and are documented in the chapters.
