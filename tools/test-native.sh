#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .pio/native-check
${CXX:-c++} -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -Ilib/RadioManager/src lib/RadioManager/src/RadioManager.cpp test/test_native/test_main.cpp -o .pio/native-check/tests
.pio/native-check/tests
