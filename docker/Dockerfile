# ---- build stage ----
FROM ubuntu:22.04 AS build
RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF \
 && cmake --build build \
 && cp build/sentinel /sentinel

# ---- runtime stage ----
FROM ubuntu:22.04 AS runtime
RUN apt-get update && apt-get install -y ca-certificates \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /sentinel /usr/local/bin/sentinel
ENTRYPOINT ["sentinel"]
