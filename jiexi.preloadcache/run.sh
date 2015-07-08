#!/bin/bash
set -e

ROOT=$(cd "$(dirname "$0")"; pwd)

cd "$ROOT"

usage() {
	printf '[Usage] %s cache_dir\n' $0
	exit 0;
}

if [[ $# -ne 1 ]]; then
	echo '[ERROR] cache_dir not specified' >&2
	echo
	usage
fi

if [[ -z "$HOST" ]]; then
	echo '[ERROR] HOST env not set' >&2
	exit 1
fi

if ! which wget >/dev/null 2>&1; then
	echo '[ERROR] Wget not found.' >&2
	exit 1
fi


handler_file=`echo "$HOST" | tr -d '/' `

exec 9<>"$handler_file".lock
flock -n 9 || {
	echo '[ERROR] Another process is running... exit.' >&2
	exit 1
}

dir="$1"

if [[ ! -d "$dir" ]]; then
	printf '[ERROR] invalid cache_dir: %s\n' "$dir" >&2
	exit 1
fi

cd "$dir"

tmp=`mktemp`
find . -type f -not -name '*.tmp' > "$tmp"
( cat "$tmp" | xargs cat | sort | uniq | xargs -I{} -P 10 -n 1 wget --timeout 5 --tries 3 -O /dev/null -a "$ROOT"/"$handler_file".log "$HOST"{} ) || true
cat "$tmp" | xargs rm -f
