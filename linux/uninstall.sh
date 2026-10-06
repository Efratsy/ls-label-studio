#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LS Label Studio contributors
# LS Label Studio – uninstall / deinstallieren
rm -rf "$HOME/.local/share/ls-label-studio" "$HOME/.cache/ls-label-studio"
rm -f "$HOME/.local/share/applications/ls-label-studio.desktop" "$HOME/.local/share/icons/hicolor/scalable/apps/ls-label-studio.svg"
rm -f "$HOME/.config/ls-label-studio/LSLabelStudio.conf"
sudo rm -f /usr/local/bin/ls64
command -v update-desktop-database >/dev/null && update-desktop-database "$HOME/.local/share/applications" 2>/dev/null
echo "LS Label Studio removed / entfernt."
