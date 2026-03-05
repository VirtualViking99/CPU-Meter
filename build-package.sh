#!/usr/bin/env bash
set -euo pipefail

APP_NAME="cpu-meter"
VERSION="1.0-1"
PKG_DIR="pkg/${APP_NAME}_${VERSION}"

echo "==> Building binary..."
g++ src/main.cpp -o "${APP_NAME}" `pkg-config --cflags --libs gtkmm-4.0`

echo "==> Preparing package directories..."
rm -rf "${PKG_DIR}"
mkdir -p "${PKG_DIR}/usr/local/bin"
mkdir -p "${PKG_DIR}/usr/share/applications"
mkdir -p "${PKG_DIR}/usr/share/icons/hicolor/256x256/apps"
mkdir -p "${PKG_DIR}/DEBIAN"

echo "==> Copying binary..."
cp "${APP_NAME}" "${PKG_DIR}/usr/local/bin/"

echo "==> Writing desktop file..."
cat > "${PKG_DIR}/usr/share/applications/${APP_NAME}.desktop" <<'EOF'
[Desktop Entry]
Name=CPU Meter
Exec=/usr/local/bin/cpu-meter
Icon=cpu-meter
Type=Application
Categories=System;Monitor;
Terminal=false
EOF

echo "==> Copying icon..."
# expects icon at: assets/cpu-meter.png
cp assets/cpu-meter.png "${PKG_DIR}/usr/share/icons/hicolor/256x256/apps/cpu-meter.png"

echo "==> Writing control file..."
cat > "${PKG_DIR}/DEBIAN/control" <<'EOF'
Package: cpu-meter
Version: 1.0-1
Section: utils
Priority: optional
Architecture: amd64
Maintainer: William Weidner
Depends: libc6, libgtkmm-4.0-0
Description: Real-time CPU meter for Linux
 A GTK4 CPU monitor with per-core radial meters,
 clock speed bars, temperature display, and dynamic resizing.
EOF

echo "==> Building .deb..."
dpkg-deb --build "${PKG_DIR}"

echo "==> Done!"
echo "Created: ${PKG_DIR}.deb"
