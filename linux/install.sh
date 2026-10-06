#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 LS Label Studio contributors
# LS Label Studio – installation on Linux.  Run WITHOUT sudo:  bash install.sh [lightscribe-1.18.27.10-linux-2.6-intel.deb|.rpm]
# LS Label Studio – Installation unter Linux.  OHNE sudo starten.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/.." && pwd)"
[ "$(id -u)" -ne 0 ] || { echo "Please run WITHOUT sudo / bitte OHNE sudo starten: bash $0"; exit 1; }
DEST="$HOME/.local/share/ls-label-studio"; APPS="$HOME/.local/share/applications"; ICONS="$HOME/.local/share/icons/hicolor/scalable/apps"
ok() { echo "  ✔ $*"; }; warn() { echo "  ! $*"; }
BIN="$ROOT/bin/ls64"; [ -f "$BIN" ] || BIN="$ROOT/src/ls64"
[ -f "$BIN" ] || { echo "ls64 binary missing (bin/ls64) – build it with: make -C src"; exit 1; }
echo "=== LS Label Studio – install ==="

echo "[1/5] ls64 -> /usr/local/bin (sudo) ..."
sudo install -m 0755 "$BIN" /usr/local/bin/ls64 || exit 1
SETCAP="$(command -v setcap || ls /usr/sbin/setcap /sbin/setcap 2>/dev/null | head -1)"
if [ -n "$SETCAP" ] && sudo "$SETCAP" cap_sys_rawio+ep /usr/local/bin/ls64; then ok "burning without sudo enabled"
else warn "setcap missing – install package 'libcap' / 'libcap2-bin' and run again"; fi
ok "$(/usr/local/bin/ls64 version)"

echo "[2/5] drive access ..."
RELOGIN=0
for d in /dev/sr[0-9]; do
    [ -e "$d" ] || continue
    if [ -r "$d" ]; then ok "$d readable"
    else grp="$(stat -c %G "$d")"
        if [ -n "$grp" ] && [ "$grp" != "root" ]; then sudo usermod -aG "$grp" "$USER" && { ok "added $USER to group '$grp'"; RELOGIN=1; }
        else warn "$d readable by root only"; fi
    fi
done

echo "[3/5] Qt for Python ..."
have_qt() { python3 -c "import PyQt6.QtWidgets" 2>/dev/null || python3 -c "import PySide6.QtWidgets" 2>/dev/null || python3 -c "import PyQt5.QtWidgets" 2>/dev/null; }
if have_qt; then ok "found"
else
    if   command -v dnf     >/dev/null; then sudo dnf install -y python3-pyqt6
    elif command -v zypper  >/dev/null; then sudo zypper --non-interactive install python3-PyQt6
    elif command -v pacman  >/dev/null; then sudo pacman -S --needed --noconfirm python-pyqt6
    elif command -v apt-get >/dev/null; then sudo apt-get install -y python3-pyqt6
    fi
    have_qt && ok "PyQt6 installed" || { warn "could not install PyQt6"; exit 1; }
fi

echo "[4/5] LightScribe tables (from the original LightScribe System Software) ..."
if /usr/local/bin/ls64 einrichten --pruefen >/dev/null 2>&1; then ok "already set up"
elif [ $# -ge 1 ] && /usr/local/bin/ls64 einrichten "$1"; then ok "set up from $1"
elif /usr/local/bin/ls64 einrichten >/dev/null 2>&1; then ok "set up from installed LightScribe software"
else warn "not set up yet – the program will ask for lightscribe-1.18.27.10-linux-2.6-intel.deb/.rpm on first start"; fi

echo "[5/5] application ..."
rm -rf "$HOME/.local/share/lightscribe-studio" "$HOME/.local/share/ls64-gui"
rm -f "$APPS/lightscribe-studio.desktop" "$APPS/ls64-gui.desktop" "$ICONS/lightscribe-studio.svg" "$ICONS/ls64-gui.svg"
mkdir -p "$DEST" "$APPS" "$ICONS"
cp "$ROOT/gui/ls_label_studio.py" "$ROOT/gui/ls-label-studio.svg" "$ROOT/gui/testbild.png" "$DEST/"
cp "$ROOT/gui/ls-label-studio.svg" "$ICONS/"
cat > "$APPS/ls-label-studio.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=LS Label Studio
GenericName=Disc Labeling
GenericName[de]=Disc-Beschriftung
Comment=Design and burn LightScribe labels
Comment[de]=LightScribe-Labels gestalten und brennen
Exec=python3 $DEST/ls_label_studio.py
Icon=ls-label-studio
Terminal=false
Categories=AudioVideo;DiscBurning;Graphics;
Keywords=LightScribe;Label;Disc;CD;DVD;
StartupWMClass=ls-label-studio
EOF
command -v update-desktop-database >/dev/null && update-desktop-database "$APPS" 2>/dev/null
for k in kbuildsycoca6 kbuildsycoca5; do command -v $k >/dev/null && $k >/dev/null 2>&1; done
ok "menu entry 'LS Label Studio'"
python3 "$DEST/ls_label_studio.py" --selbsttest > "$HOME/.cache/ls-label-studio-selftest.txt" 2>&1 && ok "self test passed" || warn "self test failed – see ~/.cache/ls-label-studio-selftest.txt"
[ "$RELOGIN" = 1 ] && echo ">>> Please log out and back in once / bitte einmal ab- und wieder anmelden."
echo ">>> Done – start 'LS Label Studio' from the menu."
