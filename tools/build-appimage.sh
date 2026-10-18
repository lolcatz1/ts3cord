#!/bin/sh
# build-appimage.sh: dist/ts3cord-<version>-x86_64.AppImage (qt5, libmpv, core, ts3 files bundled)
# needs linuxdeploy-x86_64.AppImage and linuxdeploy-plugin-qt-x86_64.AppImage in $LINUXDEPLOY_DIR (default dev/tools)
set -e
R=$(dirname "$(readlink -f "$0")")/..
VER=$(cat "$R/VERSION")
LD=${LINUXDEPLOY_DIR:-$R/dev/tools}
B=$R/build/appimage
APP=$B/AppDir
rm -rf "$B"
mkdir -p "$APP/usr/bin" "$R/dist"

# core
(cd "$R/core" && go build -trimpath -ldflags "-s -w" -o "$APP/usr/bin/ts3cord-core" .)

# ui
cmake -S "$R/ui" -B "$B/ui" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$B/ui" -j"$(nproc)" >/dev/null
install -m755 "$B/ui/ts3cord" "$APP/usr/bin/ts3cord"
mkdir -p "$APP/usr/share/ts3cord" && cp -r "$R/ts3" "$APP/usr/share/ts3cord/ts3"

cat > "$B/ts3cord.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=ts3cord
Comment=discord client wearing teamspeak 3
Exec=ts3cord
Icon=ts3cord
Terminal=false
Categories=Network;Chat;
StartupWMClass=ts3cord
EOF

# wayland, multimedia, image formats; kvantum for the windows 7 look
export QMAKE=$(command -v qmake-qt5 || command -v qmake)
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export NO_STRIP=1
export LDAI_OUTPUT="$R/dist/ts3cord-$VER-x86_64.AppImage"
export OUTPUT="$LDAI_OUTPUT"
P=$(qmake -query QT_INSTALL_PLUGINS)
"$LD/linuxdeploy-x86_64.AppImage" --appdir "$APP" \
  -e "$APP/usr/bin/ts3cord" -e "$APP/usr/bin/ts3cord-core" \
  -d "$B/ts3cord.desktop" -i "$R/ui/res/ts3cord.png" \
  --plugin qt >"$B/linuxdeploy.log" 2>&1
# extra plugins linuxdeploy-plugin-qt leaves out, with their libs
for d in styles/libkvantum.so wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client imageformats mediaservice audio; do
  [ -e "$P/$d" ] || continue
  mkdir -p "$APP/usr/plugins/$(dirname "$d")"
  cp -rn "$P/$d" "$APP/usr/plugins/$(dirname "$d")/"
done
for so in $(find "$APP/usr/plugins" -name '*.so'); do DEPS="$DEPS --deploy-deps-only $so"; done
"$LD/linuxdeploy-x86_64.AppImage" --appdir "$APP" $DEPS --output appimage >>"$B/linuxdeploy.log" 2>&1
ls -la "$R/dist/ts3cord-$VER-x86_64.AppImage"
