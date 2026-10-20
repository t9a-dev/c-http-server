#!/bin/sh
set -eu

# src/server.cのビルドと実行を行う
mkdir -p ./build
gcc ./src/server.c -o ./build/server
./build/server
