#!/bin/sh
set -eu

version='10.0~repack-12ubuntu1'
archive='https://archive.ubuntu.com/ubuntu/pool/universe/w/wine'
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
base="${XDG_DATA_HOME:-$HOME/.local/share}/kakao-linux-bridge/wine-clipboard/$version"
dsc="wine_$version.dsc"
source="$base/source"
patch_file="$repo/patches/wine-10-png-clipboard.patch"
mode=${1:-prepare}
if [ "$#" -gt 1 ] || { [ "$mode" != prepare ] && [ "$mode" != build ]; }; then
    echo "Usage: $0 [prepare|build]" >&2
    exit 2
fi
if [ "$(dpkg --print-architecture)" != amd64 ]; then
    echo "This local Wine build targets Ubuntu amd64." >&2
    exit 1
fi
umask 077
mkdir -p "$base"
exec 9>"$base/build.lock"
flock 9
cd "$base"

fetch() {
    checksum=$1
    name=$2
    if [ ! -f "$name" ]; then
        curl -fLSs "$archive/$name" -o "$name.part"
        printf '%s  %s\n' "$checksum" "$name.part" | sha256sum -c -
        mv -- "$name.part" "$name"
    fi
    printf '%s  %s\n' "$checksum" "$name" | sha256sum -c -
}

fetch 51fe140b958b102827ea81b58eb59efed690bbda53f7969865f0c39e57786910 "$dsc"
fetch 7f1504f1dc5b7ad41eb1b426384c7580f2271810d73d4cc1c200ea05c418dc5d "wine_$version.debian.tar.xz"
fetch e87ff2971c9b2e79fc63341f044d7513e66f3c3782b49609746428caa87c6ee6 'wine_10.0~repack.orig.tar.xz'

if [ ! -f "$source/.bridge-patch-applied" ]; then
    prepared=$(mktemp -d "$base/prepare.XXXXXX")
    dpkg-source --no-copy -x "$dsc" "$prepared/source"
    patch --batch --fuzz=0 -d "$prepared/source" -p1 < "$patch_file"
    touch "$prepared/source/.bridge-patch-applied"
    mv -T -- "$prepared/source" "$source"
    rmdir -- "$prepared"
fi

if [ "$mode" = prepare ]; then
    printf 'Wine source patched. Build dependencies descriptor: %s/%s\n' "$base" "$dsc"
    exit 0
fi

if [ "$(id -u)" -eq 0 ]; then
    echo "Run this build as your desktop user." >&2
    exit 1
fi
. /etc/os-release
if [ "${ID:-}" != ubuntu ]; then
    echo "Build the Ubuntu runtime on Ubuntu." >&2
    exit 1
fi
cd "$source"
dpkg-checkbuilddeps
pkg-config --exists libpng || { echo "Install libpng-dev before building." >&2; exit 1; }
DEB_BUILD_OPTIONS="${DEB_BUILD_OPTIONS:-parallel=4 nocheck}" dpkg-buildpackage -b -us -uc
cd "$base"

stage=$(mktemp -d "$base/runtime.XXXXXX")
for package in "wine64_${version}_amd64.deb" "libwine_${version}_amd64.deb" "wine-common_${version}_all.deb"; do
    dpkg-deb -x "$package" "$stage"
done
mkdir -p "$stage/usr/share/wine/wine"
ln -s ../nls "$stage/usr/share/wine/wine/nls"
wine_dir=usr/lib/x86_64-linux-gnu/wine
test -x "$stage/$wine_dir/wine"
test -f "$stage/$wine_dir/i386-windows/ntdll.dll"
readelf -Ws "$stage/$wine_dir/x86_64-unix/winex11.so" > "$stage/winex11.symbols"
grep -q png_image_begin_read_from_memory "$stage/winex11.symbols"
for arch in i386 x86_64; do
    # Ubuntu installs these built-in DLLs in the libwine package trigger.
    install -m 0644 "/$wine_dir/$arch-windows/zlib1.dll" "$stage/$wine_dir/$arch-windows/zlib1.dll"
done

cat > "$stage/wine-kakao" <<'WRAPPER'
#!/bin/sh
set -eu
base=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
wine_dir="$base/usr/lib/x86_64-linux-gnu/wine"
if [ "${1:-}" != --version ]; then
    if [ -z "${DISPLAY:-}" ]; then
        echo "The clipboard patch requires X11 or XWayland." >&2
        exit 1
    fi
    WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}winewayland.drv="
    export WINEDLLOVERRIDES
fi
WINESERVER="$wine_dir/wineserver"
export WINESERVER
exec "$wine_dir/wine" "$@"
WRAPPER
chmod 0755 "$stage/wine-kakao"
"$stage/wine-kakao" --version
printf 'Set Bridge Settings → Wine executable to:\n%s/wine-kakao\n' "$stage"
