#!/bin/sh
set -eu

version='10.0~repack-12ubuntu1'
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
artifact="$repo/runtime/ubuntu-wine-10.0-amd64"
base="${XDG_DATA_HOME:-$HOME/.local/share}/kakao-linux-bridge/wine-clipboard/$version"
wine_dir=usr/lib/x86_64-linux-gnu/wine
data_dir=usr/share/wine

if [ "$#" -ne 0 ] || [ "$(id -u)" -eq 0 ]; then
    echo "Run this script without sudo or arguments." >&2
    exit 2
fi
for package in wine64:amd64 libwine:amd64; do
    if [ "$(dpkg-query -W -f='${Status} ${Version}' "$package" 2>/dev/null || :)" != "install ok installed $version" ]; then
        printf '%s %s is required. The installed Wine has been left alone.\n' "$package" "$version" >&2
        exit 1
    fi
done
(cd "$artifact" && sha256sum -c SHA256SUMS)
umask 077
mkdir -p "$base"
exec 9>"$base/install.lock"
flock 9
digest=$(cut -c1-12 "$artifact/SHA256SUMS")
runtime="$base/installed-$digest"
if [ -x "$runtime/wine-kakao" ]; then
    cmp "$artifact/winex11.so" "$runtime/$wine_dir/x86_64-unix/winex11.so"
    "$runtime/wine-kakao" --version
    printf '\nBridge Settings → Wine executable:\n%s/wine-kakao\n' "$runtime"
    exit 0
fi
stage=$(mktemp -d "$base/staging.XXXXXX")
mkdir -p "$stage/$wine_dir" "$stage/$data_dir"
cp -a --reflink=auto "/$wine_dir/." "$stage/$wine_dir/"
cp -a --reflink=auto "/$data_dir/." "$stage/$data_dir/"
# Some packaged wineserver builds also look below share/wine/wine.
if [ ! -e "$stage/$data_dir/wine/nls" ]; then
    mkdir -p "$stage/$data_dir/wine"
    ln -s ../nls "$stage/$data_dir/wine/nls"
fi
# Remove the destination entry before writing, in case it is a package symlink.
cp --remove-destination -- "$artifact/winex11.so" "$stage/$wine_dir/x86_64-unix/winex11.so"
test -f "$stage/$wine_dir/i386-windows/ntdll.dll"

cat > "$stage/wine-kakao" <<'WRAPPER'
#!/bin/sh
set -eu
base=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
wine_dir="$base/usr/lib/x86_64-linux-gnu/wine"
if [ "${1:-}" != --version ]; then
    if [ -z "${DISPLAY:-}" ]; then
        echo "KakaoTalk clipboard support requires X11 or XWayland." >&2
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
mv -T -- "$stage" "$runtime"
printf '\nBridge Settings → Wine executable:\n%s/wine-kakao\n' "$runtime"
