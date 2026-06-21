#!/bin/bash
# Build distribution tarball for norn
set -e
cd "$(dirname "$0")/.."

VERSION=$(grep AC_INIT configure.ac | sed 's/.*\[\([0-9.]*\)\].*/\1/')
TARBALL="norn-${VERSION}.tar.gz"

echo "Building norn ${VERSION}..."

# Ensure autogen has run
if [ ! -f configure ]; then
    ./autogen.sh
fi

# Create distribution
make dist

echo "Created ${TARBALL}"
sha256sum "${TARBALL}" || sha256 "${TARBALL}"