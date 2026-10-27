#!/usr/bin/env bash
# Builds and runs the host tests of the protocol code. Needs a C++17 compiler
# and mbedtls 2.28 or 3.x (Debian/Ubuntu: libmbedtls-dev, macOS: brew install
# mbedtls@3). Set MBEDTLS_PREFIX when mbedtls is not on the default paths.
set -euo pipefail

cd "$(dirname "$0")/.."
src=esphome/components/cgo
out="${TMPDIR:-/tmp}/cgo-protocol-test"

if [ -z "${MBEDTLS_PREFIX:-}" ] && [ -d /opt/homebrew/opt/mbedtls@3 ]; then
    MBEDTLS_PREFIX=/opt/homebrew/opt/mbedtls@3
fi
flags=()
if [ -n "${MBEDTLS_PREFIX:-}" ]; then
    flags+=(-I"$MBEDTLS_PREFIX/include" -L"$MBEDTLS_PREFIX/lib")
fi

c++ -std=c++17 -Wall -Wextra -Werror -I"$src" "${flags[@]}" \
    tests/protocol_test.cpp "$src/cgo_protocol.cpp" "$src/cgo_crypto.cpp" \
    -lmbedcrypto -o "$out"
"$out"
