# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LS Label Studio contributors
# ls64 – LightScribe-Brennprogramm von LS Label Studio
#   make            Linux, statisch (braucht libpng/zlib als statische Bibliotheken)
#   make dynamisch  Linux, gegen die Systembibliotheken
#   Windows: build_windows.bat (eine Ebene hoeher)
CC ?= gcc
CFLAGS ?= -O2 -Wall
QUELLEN = ls64.c bild.c spuren.c brennen.c daten.c plattform.c
KOPF = ls64.h daten.h plattform.h
ls64: $(QUELLEN) $(KOPF)
	$(CC) $(CFLAGS) -static -o $@ $(QUELLEN) -lpng -lz -lm
dynamisch: $(QUELLEN) $(KOPF)
	$(CC) $(CFLAGS) -o ls64 $(QUELLEN) -lpng -lz -lm
clean:
	rm -f ls64
.PHONY: dynamisch clean
