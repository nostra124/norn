#!/bin/bash
# Run SIT tests in a container
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Container image (Alpine with build tools)
IMAGE="alpine:3.19"

# Install dependencies in container
CONTAINERFILE=$(cat <<EOF
FROM $IMAGE
RUN apk add --no-cache \
    autoconf \
    automake \
    libtool \
    gcc \
    make \
    libsodium-dev \
    bats \
    git
EOF
)

# Build container image if needed
if ! podman image inspect norn-test:latest >/dev/null 2>&1; then
    echo "==> Building test container"
    echo "$CONTAINERFILE" | podman build -t norn-test:latest -f - .
fi

# Run SIT tests
echo "==> Running SIT tests in container"
podman run --rm \
    -v "$PROJECT_ROOT:/workspace:Z" \
    -w /workspace \
    norn-test:latest \
    bash -c "
        cd /workspace
        bats tests/sit/*.bats
    "

echo "==> SIT tests passed"