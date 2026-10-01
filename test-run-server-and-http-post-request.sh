#!/bin/sh
set -eu

# src/server.cのビルドと実行を行う
./rebuild-and-run-server.sh &

printf "\n"

sleep 1

curl -X GET http://127.0.0.1:8080/calc?q=1%2B2
