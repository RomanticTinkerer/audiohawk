#!/bin/sh
# One-command AudioHawk install or update. Picks the package for your distro:
# .deb on apt systems, .rpm on dnf/zypper/rpm systems, the tarball elsewhere.
# Run from an AppImage (APPIMAGE set), it swaps in the new AppImage without root.
#
#   curl -fsSL https://github.com/RomanticTinkerer/audiohawk/releases/latest/download/install.sh | sudo sh
#
# From a downloaded tarball:
#
#   sudo ./install.sh
#
# Override the tarball prefix with PREFIX=/usr sudo ./install.sh
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

# Installed .deb version, empty when missing or only config files are left.
deb_version() {
    case "$(dpkg-query -W -f='${db:Status-Status} ${Version}' audiohawk 2>/dev/null)" in
    "installed "*) dpkg-query -W -f='${Version}' audiohawk ;;
    esac
}

fetch() {
    echo "Downloading $1"
    curl -fsSL --retry 3 -o "$2" "$1" || die "download failed"
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
    [ "$ARCH" = "x86_64" ] || [ "$ARCH" = "amd64" ] || die "only x86_64 packages are published"

    tmp=$(mktemp -d)
    chmod 0755 "$tmp" # apt's sandbox user has to read the .deb
    trap 'rm -rf "$tmp"' EXIT

    tag=${AUDIOHAWK_VERSION:-}
    if [ -z "$tag" ]; then
        json=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest") ||
            die "could not read GitHub latest release"
        tag=$(printf '%s\n' "$json" | sed -n 's/.*"tag_name": *"\([^"]*\)".*/\1/p' | head -n1)
    fi
    [ -n "$tag" ] || die "no release tag found"
    ver=${tag#v}
    base="https://github.com/${REPO}/releases/download/${tag}"

    if [ -n "${APPIMAGE:-}" ] && [ -w "$APPIMAGE" ]; then
        fetch "$base/AudioHawk-${ver}-x86_64.AppImage" "$APPIMAGE.new"
        chmod 0755 "$APPIMAGE.new"
        mv -f "$APPIMAGE.new" "$APPIMAGE"
        echo "Updated $APPIMAGE to $ver"
        exit 0
    fi

    if command -v apt-get >/dev/null 2>&1; then
        need_root
        have=$(deb_version)
        if [ -n "$have" ] && dpkg --compare-versions "$have" ge "$ver"; then
            echo "AudioHawk $have is already up to date"
            exit 0
        fi
        fetch "$base/audiohawk_${ver}_amd64.deb" "$tmp/audiohawk.deb"
        # A broken apt hook elsewhere on the system fails the run after the
        # package is in, so trust the installed version over apt's exit code.
        if ! DEBIAN_FRONTEND=noninteractive apt-get install -y "$tmp/audiohawk.deb"; then
            [ "$(deb_version)" = "$ver" ] ||
                die "apt-get could not install the package"
            echo "apt-get reported an error, but AudioHawk $ver is installed"
        fi
        echo "Installed AudioHawk $ver (.deb). Run: audiohawk"
        exit 0
    fi

    rpm_cmd=""
    if command -v dnf >/dev/null 2>&1; then
        rpm_cmd="dnf install -y"
    elif command -v zypper >/dev/null 2>&1; then
        rpm_cmd="zypper --non-interactive install --allow-unsigned-rpm"
    elif command -v rpm >/dev/null 2>&1; then
        rpm_cmd="rpm -U --replacepkgs"
    fi
    if [ -n "$rpm_cmd" ]; then
        need_root
        fetch "$base/audiohawk-${ver}-1.x86_64.rpm" "$tmp/audiohawk.rpm"
        $rpm_cmd "$tmp/audiohawk.rpm"
        echo "Installed AudioHawk $ver (.rpm). Run: audiohawk"
        exit 0
    fi

    command -v tar >/dev/null 2>&1 || die "tar is required"
    fetch "$base/audiohawk-${ver}-x86_64.tar.gz" "$tmp/audiohawk.tar.gz"
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
