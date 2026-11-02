#!/bin/sh
set -e
cd "$(dirname "$0")"
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -o /tmp/vrl_test_core test_core.cpp ../timing.cpp
/tmp/vrl_test_core
