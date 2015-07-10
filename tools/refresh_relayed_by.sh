#!/bin/bash
set -e

usage() {
	printf '[Usage] %s start-block-height end-block-height\n' "$0"
	exit 0
}

if [[ $# -ne 2 ]]; then
	usage
fi

if [[ -z "$ENDPOINT" ]]; then
	echo 'ENDPOINT not set' >&2
	exit 1
fi

start="$1"
end="$2"

http='wget --timeout=10 --tries=3 -q -O-'

seq "$start" "$end" | xargs -P 20 -I{} $http "$ENDPOINT"/{} >/dev/null