#!/bin/sh
# Run unit tests with coverage enforcement
set -e
cd "$(dirname "$0")/.."

echo "==> Running unit tests"
make check

echo "==> Running coverage gate"
# Reconfigure with coverage flags if needed
if [ ! -f Makefile ] || ! grep -q --coverage Makefile; then
    ./configure --enable-coverage CFLAGS="-O0 -g"
fi

make coverage

echo "==> All tests passed"