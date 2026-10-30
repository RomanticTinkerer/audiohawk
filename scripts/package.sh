#!/usr/bin/env bash
# Build AudioHawk .deb, .rpm, .AppImage, and tarball artifacts into dist/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${AUDIOHAWK_VERSION:-0.3.0}"
DEB_ARCH="$(dpkg --print-architecture 2>/dev/null || echo amd64)"
RPM_ARCH="x86_64"
WORK="$ROOT/build-pkg"
STAGE="$WORK/stage"
TOOLS="$WORK/tools"
APPDIR="$WORK/AppDir"
OUT="$ROOT/dist"
NFPM_VERSION="${NFPM_VERSION:-2.47.0}"

export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64

mkdir -p "$OUT" "$TOOLS"
rm -rf "$STAGE" "$APPDIR" "$WORK/cmake" "$WORK/rpmroot"

echo "==> Building AudioHawk ${VERSION}"
cmake -S "$ROOT" -B "$WORK/cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$WORK/cmake" -j"$(nproc)"
DESTDIR="$STAGE" cmake --install "$WORK/cmake"

if [[ ! -x "$STAGE/usr/bin/audiohawk" || ! -x "$STAGE/usr/bin/audiohawk-gtk" ]]; then
  echo "error: expected launcher and GTK binaries after install" >&2
  exit 1
fi

write_maintainer_scripts() {
  local dest="$1"
  mkdir -p "$dest"
  cat > "$dest/postinst" <<'EOF'
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
  cat > "$dest/postrm" <<'EOF'
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
  chmod 0755 "$dest/postinst" "$dest/postrm"
}

echo "==> .deb"
DEB_STAGE="$WORK/deb"
rm -rf "$DEB_STAGE"
mkdir -p "$DEB_STAGE"
cp -a "$STAGE/." "$DEB_STAGE/"
write_maintainer_scripts "$DEB_STAGE/DEBIAN"
SIZE_KB="$(du -sk "$DEB_STAGE" | awk '{print $1}')"
cat > "$DEB_STAGE/DEBIAN/control" <<EOF
Package: audiohawk
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: ${DEB_ARCH}
Depends: libpipewire-0.3-0 | libpipewire-0.3-0t64, libgtk-4-1, libadwaita-1-0, libglib2.0-0 | libglib2.0-0t64
Maintainer: Animesh Singh <253191443+RomanticTinkerer@users.noreply.github.com>
Installed-Size: ${SIZE_KB}
Homepage: https://github.com/RomanticTinkerer/audiohawk
Description: A simple audio enhancer.
 AudioHawk is a PipeWire equalizer and listening-profile controller.
 It can run in the system tray so processing continues after the window
 is closed.
EOF
find "$DEB_STAGE" -type d -exec chmod 0755 {} \;
find "$DEB_STAGE/usr" -type f -exec chmod 0644 {} \;
chmod 0755 "$DEB_STAGE/usr/bin/"*
DEB="$OUT/audiohawk_${VERSION}_${DEB_ARCH}.deb"
dpkg-deb --root-owner-group --build "$DEB_STAGE" "$DEB"
echo "Built $DEB"

echo "==> .rpm"
if [[ ! -x "$TOOLS/nfpm" ]]; then
  curl -fsSL "https://github.com/goreleaser/nfpm/releases/download/v${NFPM_VERSION}/nfpm_${NFPM_VERSION}_Linux_x86_64.tar.gz" \
    | tar -xz -C "$TOOLS" nfpm
  chmod 0755 "$TOOLS/nfpm"
fi
write_maintainer_scripts "$WORK/rpm-scripts"
cat > "$WORK/nfpm.yaml" <<EOF
name: audiohawk
arch: ${RPM_ARCH}
platform: linux
version: ${VERSION}
release: "1"
section: sound
priority: optional
maintainer: Animesh Singh <253191443+RomanticTinkerer@users.noreply.github.com>
description: |-
  A simple audio enhancer.
  AudioHawk is a PipeWire equalizer and listening-profile controller.
vendor: AudioHawk
homepage: https://github.com/RomanticTinkerer/audiohawk
license: GPL-2.0
contents:
  - src: ${STAGE}/usr
    dst: /usr
    type: tree
scripts:
  postinstall: ${WORK}/rpm-scripts/postinst
  postremove: ${WORK}/rpm-scripts/postrm
overrides:
  rpm:
    depends:
      - pipewire-libs
      - gtk4
      - libadwaita
EOF
RPM="$OUT/audiohawk-${VERSION}-1.${RPM_ARCH}.rpm"
"$TOOLS/nfpm" package --packager rpm --config "$WORK/nfpm.yaml" --target "$RPM"
echo "Built $RPM"

echo "==> AppImage"
download_tool() {
  local url="$1" dest="$2"
  if [[ ! -e "$dest" ]]; then
    curl -fL --retry 3 -o "$dest" "$url"
    chmod 0755 "$dest"
  fi
}
download_tool \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
  "$TOOLS/linuxdeploy-x86_64.AppImage"
download_tool \
  "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/3b67a1d1c1b0c8268f57f2bce40fe2d33d409cea/linuxdeploy-plugin-gtk.sh" \
  "$TOOLS/linuxdeploy-plugin-gtk.sh"

rm -rf "$APPDIR"
mkdir -p "$APPDIR"
cp -a "$STAGE/." "$APPDIR/"
cp "$ROOT/data/audiohawk.desktop" "$APPDIR/audiohawk.desktop"
cp "$ROOT/data/icons/hicolor/256x256/apps/audiohawk.png" "$APPDIR/audiohawk.png"
# linuxdeploy looks for these names at the AppDir root.
cp "$APPDIR/audiohawk.desktop" "$APPDIR/usr/share/applications/audiohawk.desktop"

export PATH="$TOOLS:$PATH"
export DEPLOY_GTK_VERSION=4
export LINUXDEPLOY="$TOOLS/linuxdeploy-x86_64.AppImage"
export OUTPUT="$OUT/AudioHawk-${VERSION}-x86_64.AppImage"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

LINUXDEPLOY_ARGS=(
  --appdir "$APPDIR"
  --executable "$APPDIR/usr/bin/audiohawk"
  --executable "$APPDIR/usr/bin/audiohawk-gtk"
  --desktop-file "$APPDIR/audiohawk.desktop"
  --icon-file "$APPDIR/audiohawk.png"
  --exclude-library "libpipewire-0.3.so.0"
  --exclude-library "libspa-0.2.so.0"
)
# The GTK plugin expects a gtk-4.0 module tree that newer GTK4 builds omit.
if [[ -d /usr/lib/x86_64-linux-gnu/gtk-4.0 || -d /usr/lib/gtk-4.0 || -d /usr/lib64/gtk-4.0 ]]; then
  LINUXDEPLOY_ARGS+=(--plugin gtk)
else
  echo "note: GTK 4 module tree not found; bundling GTK libraries via ldd only"
fi

"$TOOLS/linuxdeploy-x86_64.AppImage" "${LINUXDEPLOY_ARGS[@]}"

# libadwaita should follow the host theme; the GTK plugin forces GTK_THEME.
if [[ -f "$APPDIR/apprun-hooks/linuxdeploy-plugin-gtk.sh" ]]; then
  sed -i '/^export GTK_THEME/d' "$APPDIR/apprun-hooks/linuxdeploy-plugin-gtk.sh"
fi
rm -f "$APPDIR"/usr/lib/gtk-4.0/*/media/libmedia-gstreamer.so

"$TOOLS/linuxdeploy-x86_64.AppImage" \
  --appdir "$APPDIR" \
  --output appimage

echo "==> tarball"
TAR_NAME="audiohawk-${VERSION}-x86_64"
TAR_ROOT="$WORK/tar/${TAR_NAME}"
rm -rf "$WORK/tar"
mkdir -p "$TAR_ROOT"
cp -a "$STAGE/usr" "$TAR_ROOT/"
install -m 0755 "$ROOT/scripts/install.sh" "$TAR_ROOT/install.sh"
cp -a "$ROOT/LICENSE" "$TAR_ROOT/" 2>/dev/null || true
cp -a "$ROOT/README.md" "$TAR_ROOT/" 2>/dev/null || true
cp -a "$ROOT/CHANGELOG.md" "$TAR_ROOT/" 2>/dev/null || true
TARBALL="$OUT/${TAR_NAME}.tar.gz"
tar -C "$WORK/tar" -czf "$TARBALL" "$TAR_NAME"
echo "Built $TARBALL"
cp -a "$ROOT/scripts/install.sh" "$OUT/install.sh"
(
  cd "$OUT"
  sha256sum \
    "$(basename "$DEB")" \
    "$(basename "$RPM")" \
    "$(basename "$OUTPUT")" \
    "$(basename "$TARBALL")" \
    install.sh \
    > SHA256SUMS
)

echo
echo "Artifacts:"
ls -lh "$DEB" "$RPM" "$OUTPUT" "$TARBALL" "$OUT/install.sh" "$OUT/SHA256SUMS"
cat "$OUT/SHA256SUMS"
