#!/bin/sh
set -eu
fixture=$1
xvfb=$2
xclip=$3
tmp=$(mktemp -d)
server=
owner=
cleanup() {
    for pid in "$owner" "$server"; do
        [ -n "$pid" ] || continue
        kill "$pid" 2>/dev/null || :
        wait "$pid" 2>/dev/null || :
    done
    rm -rf "$tmp"
}
trap cleanup EXIT
"$xvfb" -displayfd 3 -screen 0 640x480x24 -nolisten tcp 3>"$tmp/display" &
server=$!
wait_ready() {
    for attempt in $(seq 1 60); do
        [ ! -s "$1" ] || return 0
        sleep .05
    done
    return 1
}
wait_ready "$tmp/display"
export DISPLAY=:$(cat "$tmp/display")
export GDK_BACKEND=x11
"$fixture" >"$tmp/ready" &
owner=$!
wait_ready "$tmp/ready"
"$xclip" -selection clipboard -out -target image/png >"$tmp/image.png"
"$xclip" -selection clipboard -out -target image/bmp >"$tmp/image.bmp"
[ "$(od -An -N8 -tx1 "$tmp/image.png" | tr -d ' \n')" = 89504e470d0a1a0a ]
[ "$(od -An -N2 -tx1 "$tmp/image.bmp" | tr -d ' \n')" = 424d ]
