#!/usr/bin/env bash
# Build an installable AudioHawk .deb (amd64).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${AUDIOHAWK_VERSION:-0.1.0}"
ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
BUILD="$ROOT/build-deb"
STAGE="$BUILD/stage"
OUT="$ROOT/dist"

rm -rf "$BUILD"
mkdir -p "$STAGE" "$OUT"

cmake -S "$ROOT" -B "$BUILD/cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD/cmake" -j"$(nproc)"
DESTDIR="$STAGE" cmake --install "$BUILD/cmake"

# Debian package metadata
mkdir -p "$STAGE/DEBIAN"
SIZE_KB="$(du -sk "$STAGE" | awk '{print $1}')"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: audiohawk
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: ${ARCH}
Depends: libpipewire-0.3-0 | libpipewire-0.3-0t64, libgtk-4-1, libadwaita-1-0, libqt6widgets6, libglib2.0-0 | libglib2.0-0t64
Maintainer: AudioHawk <audiohawk@localhost>
Installed-Size: ${SIZE_KB}
Homepage: https://github.com/audiohawk/audiohawk
Description: A simple audio enhancer.
 AudioHawk is a PipeWire equalizer and listening-profile controller.
 It can run in the system tray so processing continues after the window
 is closed.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 0755 "$STAGE/DEBIAN/postrm"

# Fix permissions for packaging
find "$STAGE" -type d -exec chmod 0755 {} \;
find "$STAGE/usr" -type f -exec chmod 0644 {} \;
chmod 0755 "$STAGE/usr/bin/"* || true

DEB="$OUT/audiohawk_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group --build "$STAGE" "$DEB"
echo "Built $DEB"
dpkg-deb -I "$DEB"
ls -lh "$DEB"
