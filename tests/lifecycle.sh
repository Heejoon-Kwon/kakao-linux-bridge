#!/bin/sh
set -eu
if [ "${1:-}" != in-session ]; then
    exec dbus-run-session -- "$0" in-session "$1"
fi
bridge=$2
tmp=$(mktemp -d)
supervisor=
cleanup() {
    if [ -n "$supervisor" ]; then
        if [ -f "$tmp/client.pid" ]; then
            kill "$(cat "$tmp/client.pid")" 2>/dev/null || :
        fi
        wait "$supervisor" 2>/dev/null || :
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT
export XDG_CONFIG_HOME="$tmp/config"
export XDG_DATA_HOME="$tmp/data"
export HOME="$tmp/home"
export WINE="$tmp/fake-wine"
unset WINEPREFIX WINEDEBUG XMODIFIERS
export GTK_IM_MODULE=fcitx5
mkdir -p "$HOME" "$XDG_CONFIG_HOME/kakao-linux-bridge"
printf 'XDG_DOWNLOAD_DIR="$HOME/Downloads"\n' > "$XDG_CONFIG_HOME/user-dirs.dirs"
prefix="$XDG_DATA_HOME/kakao-linux-bridge/wine"
dir="$prefix/drive_c/Program Files (x86)/Kakao/KakaoTalk"
mkdir -p "$dir"
touch "$dir/KakaoTalk.exe" "$tmp/KakaoTalk_Setup.exe"
cat > "$WINE" <<'FAKE'
#!/bin/sh
set -eu
base=${0%/*}
printf '%s\n' "$*" >> "$base/operations"
case "$1" in
    reg|wineboot|*Setup.exe) exit 0 ;;
    taskkill) kill "$(cat "$base/client.pid")" ;;
    *KakaoTalk.exe)
        printf '%s\n' "${XMODIFIERS-unset}" "${WINEDEBUG-unset}" > "$base/client-environment"
        echo $$ > "$base/client.pid"
        exec sleep 120 ;;
    *) exit 42 ;;
esac
FAKE
chmod +x "$WINE"
printf '[Bridge]\nDPI=144\nMapDownloads=true\n' > "$XDG_CONFIG_HOME/kakao-linux-bridge/settings.ini"
"$bridge" doctor | grep -F "Client: $dir/KakaoTalk.exe"
[ "$("$bridge" status)" = idle ]
"$bridge" install-client "$tmp/KakaoTalk_Setup.exe"
"$bridge" repair
"$bridge" start > "$tmp/bridge.log" 2>&1 &
supervisor=$!
attempt=0
while [ ! -f "$tmp/client.pid" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -gt 100 ] || ! kill -0 "$supervisor" 2>/dev/null; then
        cat "$tmp/bridge.log"
        exit 1
    fi
    sleep 0.05
done
[ "$("$bridge" status)" = client ]
"$bridge" start
[ "$(grep -c 'KakaoTalk.exe$' "$tmp/operations")" = 1 ]
printf '%s\n' '@im=fcitx' 'fixme-all' > "$tmp/expected-environment"
diff -u "$tmp/expected-environment" "$tmp/client-environment"
grep -F 'LogPixels /t REG_DWORD /d 144' "$tmp/operations"
grep -F 'C:\Ubuntu Downloads' "$tmp/operations"
[ "$(readlink "$prefix/drive_c/Ubuntu Downloads")" = "$HOME/Downloads" ]
gdbus call --session --dest io.github.KakaoLinuxBridge --object-path /StatusNotifierItem \
    --method org.freedesktop.DBus.Properties.Get org.kde.StatusNotifierItem IconName |
    grep -F io.github.KakaoLinuxBridge
awk '/^(Pss|Rss):/{print}' "/proc/$supervisor/smaps_rollup"
ticks_before=$(awk '{print $14+$15}' "/proc/$supervisor/stat")
sleep 2
ticks_after=$(awk '{print $14+$15}' "/proc/$supervisor/stat")
echo "Bridge idle CPU ticks over 2s: $((ticks_after - ticks_before))"
"$bridge" stop
wait "$supervisor"
supervisor=
[ "$("$bridge" status)" = idle ]
rm "$tmp/client.pid"
operations=$(grep -c '^reg ' "$tmp/operations")
"$bridge" start-baseline > "$tmp/bridge.log" 2>&1 &
supervisor=$!
for attempt in $(seq 1 100); do
    [ ! -f "$tmp/client.pid" ] || break
    sleep .05
done
test -f "$tmp/client.pid"
printf '%s\n' 'unset' 'unset' > "$tmp/expected-environment"
diff -u "$tmp/expected-environment" "$tmp/client-environment"
[ "$(grep -c '^reg ' "$tmp/operations")" = "$operations" ]
"$bridge" stop
wait "$supervisor"
supervisor=
mkdir -p "$XDG_CONFIG_HOME/autostart"
touch "$XDG_CONFIG_HOME/autostart/io.github.KakaoLinuxBridge.desktop"
"$bridge" disable-autostart
test ! -e "$XDG_CONFIG_HOME/autostart/io.github.KakaoLinuxBridge.desktop"
printf 'not an ini file\n' > "$XDG_CONFIG_HOME/kakao-linux-bridge/settings.ini"
if "$bridge" doctor; then
    exit 1
fi
