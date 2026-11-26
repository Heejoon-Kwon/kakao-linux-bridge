#!/bin/sh
set -eu
if [ "${1:-}" != in-session ]; then
    exec dbus-run-session -- "$0" in-session "$@"
fi
bridge=$2
fixture=$3
xvfb=$4
temp=$(mktemp -d)
server=
service=
supervisor=
cleanup() {
    for pid in "$supervisor" "$service" "$server"; do
        [ -n "$pid" ] || continue
        kill -KILL "$pid" 2>/dev/null || :
        wait "$pid" 2>/dev/null || :
    done
    if [ -f "$temp/client.pid" ]; then
        kill "$(cat "$temp/client.pid")" 2>/dev/null || :
    fi
    rm -rf "$temp"
}
trap cleanup EXIT
wait_until() {
    for attempt in $(seq 1 100); do
        if "$@"; then return 0; fi
        sleep .05
    done
    return 1
}

"$xvfb" -displayfd 3 -screen 0 640x480x24 -nolisten tcp 3>"$temp/display" &
server=$!
wait_until test -s "$temp/display"
export DISPLAY=:$(cat "$temp/display")
"$fixture" serve >"$temp/notifications" &
service=$!
notifications_ready() {
    gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.NameHasOwner org.freedesktop.Notifications | grep -q true
}
wait_until notifications_ready

export HOME="$temp/home"
export XDG_CONFIG_HOME="$temp/config"
export XDG_DATA_HOME="$temp/data"
export WINE="$temp/wine"
export LC_ALL=C.UTF-8 XMODIFIERS=@im=fcitx GTK_IM_MODULE=fcitx QT_IM_MODULE=fcitx
export WINEDLLOVERRIDES='d3d9=b' WINEDEBUG='warn+all'
unset WINEPREFIX LC_CTYPE
mkdir -p "$HOME" "$XDG_CONFIG_HOME/kakao-linux-bridge"
client="$XDG_DATA_HOME/kakao-linux-bridge/wine/drive_c/Program Files (x86)/Kakao/KakaoTalk/KakaoTalk.exe"
mkdir -p "$(dirname "$client")"
touch "$client"
printf '[Bridge]\nWatchAttention=true\nUbuntuKoreanProfile=true\n' > "$XDG_CONFIG_HOME/kakao-linux-bridge/settings.ini"
cat >"$WINE" <<'FAKE'
#!/bin/sh
set -eu
base=${0%/*}
case "$1" in
    taskkill) kill "$(cat "$base/client.pid")" ;;
    *KakaoTalk.exe)
        {
            printf '%s\n' "$XMODIFIERS" "$GTK_IM_MODULE" "$QT_IM_MODULE" "$WINEDLLOVERRIDES" "$WINEDEBUG"
            printf '%s\n' "${LC_ALL-unset}" "${LC_CTYPE-unset}" "$PWD"
        } > "$base/launch-environment"
        echo $$ > "$base/client.pid"
        exec sleep 45 ;;
    *) exit 1 ;;
esac
FAKE
chmod +x "$WINE"

"$bridge" start >"$temp/bridge.log" 2>&1 &
supervisor=$!
wait_until test -s "$temp/client.pid"
[ "$("$bridge" status)" = client ]
{
    printf '%s\n' '@im=ibus' 'ibus' 'ibus' 'd3d9=b;winewayland.drv=' 'warn+all'
    if "$bridge" doctor | grep -q '^Korean UTF-8 locale: installed$'; then
        printf '%s\n' 'unset' 'ko_KR.UTF-8'
    else
        printf '%s\n' 'C.UTF-8' 'unset'
    fi
    dirname "$client"
} > "$temp/expected-environment"
diff -u "$temp/expected-environment" "$temp/launch-environment"
pid=$(cat "$temp/client.pid")
"$fixture" "$pid" wrong-class
"$fixture" "$pid" wrong-pid
"$fixture" "$pid" remove
[ ! -s "$temp/notifications" ]
"$fixture" "$pid" valid
wait_until test -s "$temp/notifications"
[ "$(cat "$temp/notifications")" = "카카오톡|카카오톡에서 확인할 항목이 있습니다." ]
awk '/^(Pss|Rss):/{print}' "/proc/$supervisor/smaps_rollup"
before=$(awk '{print $14+$15}' "/proc/$supervisor/stat")
sleep 2
after=$(awk '{print $14+$15}' "/proc/$supervisor/stat")
echo "Bridge with X11 notifications enabled, idle CPU ticks over 2s: $((after - before))"
"$bridge" stop
wait "$supervisor"
supervisor=
rm "$temp/client.pid"
[ "$("$bridge" status)" = idle ]

DISPLAY= "$bridge" start >"$temp/bridge.log" 2>&1 &
supervisor=$!
wait_until test -s "$temp/client.pid"
{
    printf '%s\n' '@im=fcitx' 'fcitx' 'fcitx' 'd3d9=b' 'warn+all'
    tail -n 3 "$temp/expected-environment"
} > "$temp/expected-fallback"
diff -u "$temp/expected-fallback" "$temp/launch-environment"
"$bridge" stop
wait "$supervisor"
supervisor=
rm "$temp/client.pid"
