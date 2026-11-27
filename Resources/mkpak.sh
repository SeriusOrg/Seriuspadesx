#!/usr/bin/env bash

# This script updates GPL part of official .paks to match the latest git version.

rm -f pak002-Base.pak pak005-Models.pak pak010-BaseSkin.pak pak050-Locales.pak pak999-References.pak

OUTPUT_DIR="`pwd`"
SRC_DIR="`dirname "$0"`"
LOG_FILE="/dev/null"

if [ ! "$MKPAK_LOG_FILE" = "" ]; then
	LOG_FILE="$MKPAK_LOG_FILE"
fi

pushd "$SRC_DIR"

EXCLUDELIST=/tmp/mkpak.exclude.txt
find . | grep _Assets_ > $EXCLUDELIST
file . | grep ".DS_Store" >> $EXCLUDELIST
ZIPARGS=-x@${EXCLUDELIST}

zip -r "$OUTPUT_DIR/pak002-Base.pak" \
License/Credits-pak002-Base.md Gfx Scripts/Main.as \
Scripts/Base Shaders \
Sounds/Feedback Sounds/Misc Sounds/Player Sounds/Effects Textures $ZIPARGS > "$LOG_FILE"

zip -r "$OUTPUT_DIR/pak005-Models.pak" Maps Models/MapObjects Models/Player Models/Effects $ZIPARGS > "$LOG_FILE"

# Tower Defense rides a pak of its own, packed straight from the mode
# folder below: everything the folder carries ships as pak006, while
# the game paks above stay free of it.
pushd TowerDefense > /dev/null
zip -r "$OUTPUT_DIR/pak006-TowerDefense.pak" Maps Models Sounds Store WeaponMods > "$LOG_FILE"
popd > /dev/null

zip -r "$OUTPUT_DIR/pak010-BaseSkin.pak" \
License/Credits-pak010-BaseSkin.md \
Scripts/Skin Sounds/Weapons Models/Weapons $ZIPARGS > "$LOG_FILE"

zip -r "$OUTPUT_DIR/pak050-Locales.pak" License/Credits-pak050-Locales.md Locales $ZIPARGS > "$LOG_FILE"

zip -r "$OUTPUT_DIR/pak999-References.pak" License/Credits-pak999-References.md Scripts/Reference $ZIPARGS > "$LOG_FILE"

popd
