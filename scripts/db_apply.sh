#!/usr/bin/env bash
set -euo pipefail

# Find project root (assumes script is in project_root/scripts)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Load .env if it exists
if [[ -f .env ]]; then
    set -a
    source .env
    set +a
fi

if [[ -z "${DATABASE_URL:-}" ]]; then
    echo "Error: DATABASE_URL is not set." >&2
    exit 1
fi

echo "Applying database bootstrap files..."

# Ensure we have sql files
if [[ ! -d "db" ]] || ! ls db/*.sql >/dev/null 2>&1; then
    echo "No SQL files found in db/"
    exit 0
fi

# Bash globs are sorted alphabetically by default
for file in db/*.sql; do
    echo "Applying $file..."
    psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f "$file"
done

echo "Database bootstrap complete."
