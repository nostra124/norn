#!/bin/bash
# Run PIT tests in a container with network access
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
    git \
    bind-tools \
    iputils
EOF
)

# Build container image if needed
if ! podman image inspect norn-test:latest >/dev/null 2>&1; then
    echo "==> Building test container"
    echo "$CONTAINERFILE" | podman build -t norn-test:latest -f - .
fi

# Check if we should run PIT tests
if [ -n "$SKIP_PIT" ]; then
    echo "==> PIT tests skipped (SKIP_PIT is set)"
    exit 0
fi

# Check network connectivity
echo "==> Checking network connectivity"
if ! ping -c 1 -W 2 router.bittorrent.com >/dev/null 2>&1; then
    echo "==> No network connectivity, skipping PIT tests"
    echo "==> Set SKIP_PIT=1 to skip these tests"
    exit 0
fi

# Run PIT tests
echo "==> Running PIT tests in container with network access"
podman run --rm \
    -v "$PROJECT_ROOT:/workspace:Z" \
    -w /workspace \
    --network host \
    -e SKIP_PIT="${SKIP_PIT:-}" \
    norn-test:latest \
    bash -c "
        cd /workspace
        bats tests/pit/*.bats
    "

echo "==> PIT tests passed"