#!/usr/bin/env bash
set -euo pipefail

COMPOSE_FILE="docker-compose.dev.yml"

if [[ "${RESET_DB:-0}" == "1" ]]; then
  echo "[dev] RESET_DB=1 -> dropping volumes"
  docker compose -f "$COMPOSE_FILE" down -v
fi

docker compose -f "$COMPOSE_FILE" up -d

set -a
source .env
set +a

echo "[dev] Waiting for PostgreSQL to be ready..."
until pg_isready -d "$DATABASE_URL" -q; do
  sleep 1
done

echo "[dev] PostgreSQL is ready. Bootstrapping database..."
./scripts/db_apply.sh

cmake -S . -B build/dev -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_TESTS=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build build/dev -j"$(nproc)"

exec ./build/dev/sentinel
