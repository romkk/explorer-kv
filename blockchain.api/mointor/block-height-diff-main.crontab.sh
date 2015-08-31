#!/usr/bin/env bash

set -e

if [[ -z "$BM_HOST" || -z "$BLOCKR_HOST" || -z "$SERVICE_NAME" ]]; then
	echo 'BM_HOST or BLOCKR_HOST or SERVICE_NAME not set' >&2
	exit	
fi

http='wget --timeout=10 --tries=3 -q -O-'
monitor='http://monitor.bitmain.com/monitor/api/v1/message?service=%s&value=%d'

function blockr() {
    local endpoint="$BLOCKR_HOST"
    $http "$endpoint" | sed -E 's/.+"nb":([[:digit:]]+),.+$/\1/g'
}

function bm() {
    local endpoint="$BM_HOST"
    $http "$endpoint" | sed -E 's/\{"height":([[:digit:]]+).+$/\1/g'
}

blockr_height=`blockr`
bm_height=`bm`

echo '[INFO] blockr ->' "$blockr_height"
echo '[INFO] bm ->' "$bm_height"

(( diff = "$blockr_height" - "$bm_height" )) || true

THRESHOLD=2

if [[ "$diff" -ge "$THRESHOLD" ]]; then
    printf '[WARN] diff = %d, threshold = %d\n' "$diff" "$THRESHOLD"
else
    echo '[OK] EVERYTHING OK'
    if [[ -z "$DEBUG" ]]; then
        $http `printf "$monitor" "$SERVICE_NAME" "$diff"` >/dev/null
    fi    
fi
