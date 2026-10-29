#!/bin/sh
# One-command AudioHawk install.
#
#   curl -fsSL https://github.com/RomanticTinkerer/audiohawk/releases/latest/download/install.sh | sudo sh
#
# From a downloaded tarball:
#
#   sudo ./install.sh
#
# Override prefix with PREFIX=/usr sudo ./install.sh
set -eu

REPO="RomanticTinkerer/audiohawk"
PREFIX="${PREFIX:-/usr/local}"
ARCH="$(uname -m)"

die() {
    echo "audiohawk: $*" >&2
    exit 1
}

need_root() {
    if [ "$(id -u)" -ne 0 ]; then
        die "run as root (sudo). Example:
  curl -fsSL https://github.com/${REPO}/releases/latest/download/install.sh | sudo sh"
    fi
}

stage=""
script_dir=""
if [ -n "${0:-}" ] && [ -f "$0" ]; then
    script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fi

if [ -n "$script_dir" ] && [ -x "$script_dir/usr/bin/audiohawk" ]; then
    stage=$script_dir
fi

if [ -z "$stage" ]; then
    command -v curl >/dev/null 2>&1 || die "curl is required"
    command -v tar >/dev/null 2>&1 || die "tar is required"
    [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "amd64" ] || die "only x86_64 packages are published"

    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT

    tag=${AUDIOHAWK_VERSION:-}
    if [ -z "$tag" ]; then
        json=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest") ||
            die "could not read GitHub latest release"
        tag=$(printf '%s\n' "$json" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n1)
    fi
    [ -n "$tag" ] || die "no release tag found"
    ver=${tag#v}
    url="https://github.com/${REPO}/releases/download/${tag}/audiohawk-${ver}-x86_64.tar.gz"
    echo "Downloading $url"
    curl -fL --retry 3 -o "$tmp/audiohawk.tar.gz" "$url" || die "download failed"
    tar -xzf "$tmp/audiohawk.tar.gz" -C "$tmp"
    stage=$(find "$tmp" -maxdepth 2 -type d -name usr -exec dirname {} \; | head -n1)
    [ -n "$stage" ] && [ -x "$stage/usr/bin/audiohawk" ] || die "tarball missing usr/bin/audiohawk"
fi

need_root

echo "Installing AudioHawk to $PREFIX"
mkdir -p "$PREFIX"
cp -a "$stage/usr/." "$PREFIX/"
chmod 0755 "$PREFIX/bin/audiohawk" "$PREFIX/bin/audiohawk-gtk" 2>/dev/null || true

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
fi

echo "Installed. Run: audiohawk"
echo "PipeWire, GTK4, and libadwaita must be on the system."
