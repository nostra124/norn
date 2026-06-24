#!/usr/bin/env bash
# Test helper for SIT tests

# Set up environment
export CC="${CC:-gcc}"
export CFLAGS="${CFLAGS:--Wall -Wextra -Werror -std=c99}"