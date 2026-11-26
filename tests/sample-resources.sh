#!/bin/sh
# Usage: sample-resources.sh LABEL SECONDS PID...
set -eu
if [ "$#" -lt 3 ]; then
    echo "Usage: $0 LABEL SECONDS PID..." >&2
    exit 2
fi
label=$1
interval=$2
shift 2
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
ticks=$(getconf CLK_TCK)
snapshot() {
    for pid in "$@"; do
        case "$pid" in ''|*[!0-9]*) exit 2 ;; esac
        [ -r "/proc/$pid/smaps_rollup" ] || continue
        stat=$(sed 's/^.*) //' "/proc/$pid/stat") || continue
        set -- $stat
        [ "$#" -ge 20 ] || continue
        memory=$(awk '/^Rss:/{r=$2} /^Pss:/{p=$2} END{if(r!="") print r,p}' "/proc/$pid/smaps_rollup") || continue
        [ -n "$memory" ] || continue
        printf '%s %s %s %s\n' "$pid" "${20}" "$(( ${12} + ${13} ))" "$memory"
    done
}
start=$(cut -d' ' -f1 /proc/uptime)
snapshot "$@" > "$tmp/before"
sleep "$interval"
snapshot "$@" > "$tmp/after"
end=$(cut -d' ' -f1 /proc/uptime)
awk -v label="$label" -v start="$start" -v end="$end" -v hz="$ticks" '
    BEGIN { print "scenario,pid,rss_kib,pss_kib,cpu_percent_single_core" }
    FILENAME==ARGV[1] { before[$1 " " $2]=$3; next }
    !seen[$1]++ {
        key=$1 " " $2
        cpu=(key in before && end>start) ? sprintf("%.3f",100*($3-before[key])/hz/(end-start)) : "NA"
        printf "%s,%s,%s,%s,%s\n",label,$1,$4,$5,cpu
    }
' "$tmp/before" "$tmp/after"
