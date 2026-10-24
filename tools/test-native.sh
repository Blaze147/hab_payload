#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .pio/native-check
${CXX:-c++} -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -g -Iinclude -Ilib/MessageManager/src -Ilib/StatisticsManager/src -Ilib/LinkManager/src -Ilib/RadioManager/src -Ilib/ProtocolManager/src lib/StatisticsManager/src/StatisticsManager.cpp lib/MessageManager/src/MessageManager.cpp lib/MessageManager/src/MessageCodec.cpp lib/LinkManager/src/LinkManager.cpp lib/ProtocolManager/src/ProtocolManager.cpp lib/ProtocolManager/src/ProtocolTypes.cpp lib/RadioManager/src/RadioManager.cpp lib/RadioManager/src/RadioTiming.cpp test/test_native/test_main.cpp -o .pio/native-check/tests
.pio/native-check/tests
