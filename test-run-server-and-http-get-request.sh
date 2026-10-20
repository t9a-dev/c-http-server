#!/bin/sh
set -eu

# src/server.cのビルドと実行を行う
./rebuild-and-run-server.sh &

printf "\n"

sleep 1

# curlの標準出力の最終行に改行を入れて、
# 最終行がシェルのプロンプトと同じ行に表示されないように。
curl() {
  command curl -w '\n' "$@"
}

curl -X GET http://127.0.0.1:8081/calc?q=1%2B2
# curl -X POST http://127.0.0.1:8081/calc?q=1%2B2
