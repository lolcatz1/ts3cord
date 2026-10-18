#!/bin/sh
# ts3cord installer: builds, installs to ~/.local
#   ./install-linux.sh
#   ./install-linux.sh --uninstall
set -e
R=$(dirname "$(readlink -f "$0")")
DATA=${XDG_DATA_HOME:-$HOME/.local/share}
D=$DATA/ts3cord
BIN=$HOME/.local/bin

if [ "$1" = "--uninstall" ]; then
  rm -rf "$D" "$BIN/ts3cord" "$DATA/applications/ts3cord.desktop"
  for s in 16 24 32 48 96 256; do rm -f "$DATA/icons/hicolor/${s}x${s}/apps/ts3cord.png"; done
  echo "removed"
  exit 0
fi

miss=""
for c in go cmake make pkg-config; do command -v $c >/dev/null || miss="$miss $c"; done
for p in Qt5Widgets Qt5Svg Qt5Network Qt5Multimedia mpv libqrencode; do pkg-config --exists $p || miss="$miss $p"; done
if [ -n "$miss" ]; then
  echo "missing:$miss"
  echo "arch: pacman -S go cmake qt5-base qt5-svg qt5-multimedia qt5-imageformats qt5-wayland mpv qrencode kvantum-qt5"
  exit 1
fi

echo "building core"
(cd "$R/core" && go build -trimpath -ldflags "-s -w" -o "$R/build/ts3cord-core" .)
echo "building ui"
cmake -S "$R/ui" -B "$R/build/ui" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$R/build/ui" -j"$(nproc)" >/dev/null

mkdir -p "$D" "$BIN"
install -m755 "$R/build/ui/ts3cord" "$D/ts3cord"
install -m755 "$R/build/ts3cord-core" "$D/ts3cord-core"
rm -rf "$D/ts3"; cp -r "$R/ts3" "$D/ts3"
cat > "$BIN/ts3cord" <<EOF
#!/bin/sh
exec "$D/ts3cord" "\$@"
EOF
chmod +x "$BIN/ts3cord"
for s in 16 24 32 48 96 256; do
  mkdir -p "$DATA/icons/hicolor/${s}x${s}/apps"
  cp "$R/ts3/icons/$s.png" "$DATA/icons/hicolor/${s}x${s}/apps/ts3cord.png"
done
mkdir -p "$DATA/applications"
cat > "$DATA/applications/ts3cord.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=TeamSpeak 3
Exec=$BIN/ts3cord
Icon=$D/ts3/icons/256.png
Terminal=false
Categories=Network;Chat;
StartupWMClass=ts3cord
EOF
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -f -t "$DATA/icons/hicolor" >/dev/null 2>&1 || true
command -v kbuildsycoca6 >/dev/null && kbuildsycoca6 >/dev/null 2>&1 || true
echo "installed"
