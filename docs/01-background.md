# 1 Background and goal

## LightScribe in short

LightScribe (introduced 2005) uses the drive's own laser to darken a dye layer on the label side of
special discs. Here is how it works:

- The disc is inserted **label side down**.
- The drive reads a control feature ring near the hub, so it knows the disc's angle exactly.
- It writes concentric tracks of laser marks. Each track is a ring of on/off marks at
  600 marks per inch along the circumference.
- The result is a monochrome, sepia-toned image. A full-disc label takes roughly 20–40 minutes,
  depending on quality.

Official software support ended years ago. Drives and media are still around.

## Starting point

| Component | State |
|---|---|
| Linux front end (`4L` labeler: GUI + CLI) | 32-bit binaries from 2006 (GUI statically linked against Qt 4), no source |
| LightScribe System Software for Linux 1.18.27.10 | 32-bit `liblightscribe.so.1`, stripped C++ (gcc 3), bundled `libstdc++.so.5` |
| LightScribe System Software for Windows 1.18.27.10 | 32-bit engine DLLs, COM server, Windows service |
| Hardware | HL-DT-ST (LG) GH24LS50 DVD writer with LightScribe, LightScribe discs |
| Reference | Labels burned with the Windows software on the same drive and media |

The 32-bit stack still ran on a current Linux system with 32-bit libraries installed. That made it
usable as a **reference**, but not as a long-term solution.

## Goal and requirements

1. **Native 64-bit**: no 32-bit runtime, no proprietary library at run time.
2. **Same quality as the original Windows software**: this became the central technical problem.
3. **Safe operation**: read-only commands for detection, a clean stop on cancel, no firmware access.
4. **Usable by non-programmers**: graphical label designer, simple installation, German and English.
5. Later added: **Windows support**, and **no redistribution of third-party code or data**.

The work was done step by step. Each step ended with a test on real hardware before the next one
started.
