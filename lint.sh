#!/usr/bin/env bash
# Runs the same linters as CI. Install them with: pip install -r requirements-lint.txt
# Pass --fix to apply clang-format and ruff's fixes first.
set -euo pipefail

cd "$(dirname "$0")"
cpp=(esphome/components/*/*.cpp esphome/components/*/*.h tests/*.cpp)

if [ "${1:-}" = "--fix" ]; then
    clang-format -i "${cpp[@]}"
    ruff check --fix .
    ruff format .
fi

clang-format --dry-run --Werror "${cpp[@]}"
ruff check .
ruff format --check .
yamllint --strict .
shellcheck tests/*.sh lint.sh
