#!/bin/bash

BASE=$(cd "$(dirname "$0")"; pwd); cd "$BASE"

rsync -av --exclude .env --exclude node_modules --exclude .git --progress ./ aliyun.explorer-stage-01:/work/Explorer/wallet.api.testnet3
remote_cmd="cd /work/Explorer/wallet.api.testnet3 && npm install --verbose"
ssh aliyun.explorer-stage-01 "$remote_cmd"

rsync -av --exclude .env --exclude node_modules --exclude .git --progress ./ aliyun.explorer-stage-01:/work/Explorer/wallet.api
remote_cmd="cd /work/Explorer/wallet.api && npm install --verbose"
ssh aliyun.explorer-stage-01 "$remote_cmd"
