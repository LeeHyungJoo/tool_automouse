#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_build="$(mktemp -d)"
trap 'rm -rf -- "$test_build"' EXIT
test_compiler="${CXX:-g++}"
flags=(-std=c++17 -Wall -Wextra -Wpedantic -Werror -DNDEBUG)

"$test_compiler" "${flags[@]}" "$project_root/tests/chord_tests.cpp" -o "$test_build/chord_tests"
"$test_build/chord_tests"
"$test_compiler" "${flags[@]}" "$project_root/tests/interval_tests.cpp" -o "$test_build/interval_tests"
"$test_build/interval_tests"
"$test_compiler" "${flags[@]}" -pthread -I "$project_root/tests/win32_stub" \
    "$project_root/AutoClicker/ClickEngine.cpp" "$project_root/tests/engine_tests.cpp" \
    -o "$test_build/engine_tests"
"$test_build/engine_tests"
