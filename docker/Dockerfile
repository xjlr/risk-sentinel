# =========================
# Build stage
# =========================
FROM ubuntu:22.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

# System dependencies for building C++ + PostgreSQL client
RUN set -eux; \
    apt-get update -o Acquire::Retries=5 -o Acquire::ForceIPv4=true; \
    apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      ninja-build \
      curl \
      ca-certificates \
      pkg-config \
      git \
      libpqxx-dev \
      libpq-dev; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code (make sure .dockerignore excludes build/)
COPY . .

# Configure & build
RUN cmake -S . -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DENABLE_TESTS=OFF \
 && cmake --build build \
 && cp build/sentinel /sentinel

# =========================
# Runtime stage
# =========================
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Runtime dependencies only (no compiler, no cmake, no git)
RUN set -eux; \
    apt-get update -o Acquire::Retries=5 -o Acquire::ForceIPv4=true; \

    apt-get install -y --no-install-recommends \
      ca-certificates \
      libpqxx-dev \
      libpq5; \

    rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the built binary
COPY --from=build /sentinel /app/sentinel

ENV CHAIN=arbitrum
ENTRYPOINT ["/app/sentinel"]
