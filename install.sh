#!/usr/bin/env bash
# Installs Pantomimus for this user on Linux: ~/.local/share/pantomimus, a launcher in ~/.local/bin and a menu entry
# named "Atelier - Pantomimus". Build it first: cmake -S . -B build && cmake --build build
set -e
cd "$(dirname "$0")"
SRC=build
[ -f Pantomimus ] && SRC=.
[ -f "$SRC/Pantomimus" ] || { echo "Build it first: cmake -S . -B build && cmake --build build"; exit 1; }
INST=~/.local/share/pantomimus
mkdir -p "$INST" ~/.local/bin ~/.local/share/applications
cp "$SRC/Pantomimus" "$INST/"
for f in "$SRC"/libonnxruntime.so* "$SRC"/libSDL2*.so*; do [ -e "$f" ] && cp -P "$f" "$INST/"; done
for d in assets data lang models blender; do [ -d "$SRC/$d" ] && cp -r "$SRC/$d" "$INST/"; done
ln -sf "$INST/Pantomimus" ~/.local/bin/pantomimus
cat > ~/.local/share/applications/atelier-pantomimus.desktop <<EOF
[Desktop Entry]
Type=Application
Name=Atelier - Pantomimus
Comment=Motion capture from your webcam
Exec=$INST/Pantomimus
Path=$INST
Categories=Graphics;3DGraphics;
EOF
echo "Installed: $INST"
