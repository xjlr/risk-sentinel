#!/usr/bin/env bash
set -euo pipefail

docker compose -f docker-compose.dev.yml up -d

set -a
source .env
set +a

cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TESTS=ON
cmake --build build/dev -j"$(nproc)"

exec ./build/dev/sentinel
