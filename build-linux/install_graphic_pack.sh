#!/usr/bin/env bash
# Copies the BetterVR graphic pack into Cemu's default Linux data directory.
# If your Cemu profile lives somewhere else (check Options -> General Settings in
# Cemu for its actual data folder), just run the cp yourself with the right path -
# this script does nothing you couldn't do in one line.
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HERE/resources/BreathOfTheWild_BetterVR"
DEST_ROOT="${BETTERVR_CEMU_DATA_DIR:-$HOME/.local/share/Cemu}"
DEST="$DEST_ROOT/graphicPacks/BreathOfTheWild_BetterVR"

if [ ! -d "$SRC" ]; then
    echo "Couldn't find $SRC - are you running this from the repo root's build-linux/?"
    exit 1
fi

if [ ! -d "$DEST_ROOT" ]; then
    echo "No Cemu data directory found at: $DEST_ROOT"
    echo "If your Cemu install keeps its data somewhere else, set BETTERVR_CEMU_DATA_DIR"
    echo "to that path and run this again, or just copy it yourself:"
    echo "  cp -r \"$SRC\" \"<your Cemu data dir>/graphicPacks/\""
    exit 1
fi

echo "Copying $SRC"
echo "     to $DEST"
rm -rf "$DEST"
cp -r "$SRC" "$DEST"
echo "Done. Enable it in Cemu under Options -> Graphic Packs -> Better VR."
