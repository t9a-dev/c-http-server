#!/bin/sh
set -eu

# src/server.cのビルドと実行を行う
./rebuild-and-run-server.sh &

printf "\n"

sleep 1

curl -X POST http://127.0.0.1:8080 -d "hello,world"
