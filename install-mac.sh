#!/bin/bash
# BREAK KAP FM-1: installs the latest build (the release "dev", built by GitHub from main) on the FM-1 plugged in by USB.
# On a Mac, in the Terminal:
#   bash -c "$(curl -fsSL https://raw.githubusercontent.com/debmasta/break-kap-fm1/main/install-mac.sh)"
# The first time it sets up what it needs in ~/.break-kap-fm1 (a minute). Keep the FM-1 plugged in and charged until it
# restarts. Back to CHOPPAH or the stock firmware: their web installers, as always.
set -e
R=debmasta/break-kap-fm1
D=~/.break-kap-fm1
command -v python3 >/dev/null 2>&1 || { echo "Il faut Python 3 :  brew install python"; exit 1; }
mkdir -p "$D"
if [ ! -x "$D/venv/bin/python" ]; then
  echo "== préparation (une seule fois)"
  python3 -m venv "$D/venv"
  "$D/venv/bin/pip" install -q --upgrade pip
  "$D/venv/bin/pip" install -q mido python-rtmidi
fi
echo "== l'installateur"
if [ -d "$D/base/.git" ]; then git -C "$D/base" pull -q --ff-only 2>/dev/null || true
else git clone -q --depth 1 https://github.com/charlesvestal/fm1-base.git "$D/base"; fi
echo "== le firmware (la dernière version compilée)"
curl -fL --progress-bar -o "$D/break-kap-fm1.fwsc" "https://github.com/$R/releases/download/dev/break-kap-fm1.fwsc" ||
  { echo "Pas encore de version compilée : regarde l'onglet Actions du repo"; exit 1; }
echo "== installation : branche le FM-1 en USB, ne le débranche pas avant qu'il redémarre"
"$D/venv/bin/python" "$D/base/tools/fm1_install.py" "$D/break-kap-fm1.fwsc" "$@"
