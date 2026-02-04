# Risk Sentinel

Risk Sentinel is a low-latency, on-chain risk monitoring engine for DeFi protocols and DAOs.

The goal of the project is to provide early, low-noise alerts for critical on-chain risks such as:

- sudden liquidity drain or imbalance

- oracle anomalies

- governance-related attacks

- treasury exfiltration patterns

Unlike generic blockchain analytics dashboards, Risk Sentinel focuses on customer-specific exposure:
alerts are triggered only when an event is actually relevant to a given protocol or treasury.

This repository currently contains the MVP infrastructure and core scaffolding.
Detection logic and risk models are evolving as part of an iterative build.

## Project status

Early development / MVP phase

The repository currently focuses on infrastructure, data ingestion,
and event normalization. Risk detection logic is being added incrementally.

Current state:

- C++ project scaffold (CMake)

- Unit test setup (Catch2)

- Dockerized runtime with PostgreSQL

- CI via GitHub Actions

- Target MVP chain: Arbitrum

## Requirements

### Development

- Linux (Ubuntu 22.04 recommended)

- C++20 compatible compiler (g++ ≥ 11 or clang ≥ 14)

- CMake ≥ 3.20

- Ninja

- Docker + Docker Compose v2

- Git

### Docker runtime

- Docker

- Docker Compose (v2)

## Environment configuration

The application expects a PostgreSQL connection string via the `DATABASE_URL`
environment variable.

Create a `.env` file in the project root:

```env
DATABASE_URL=postgresql://sentinel:sentinel@127.0.0.1:5433/sentinel
```
This setup is intended for local development, where PostgreSQL runs in Docker
and the application runs natively on the host.

## Local development

For local development, PostgreSQL is started via Docker, while the application
is built and executed natively on the host. This avoids expensive Docker rebuilds
during active development while keeping the runtime environment consistent.

Start the development environment:

```bash
./dev.sh
```

## Dependencies

### Build-time
- C++20 compatible compiler (g++ ≥ 11 or clang ≥ 14)
- CMake ≥ 3.20
- Ninja (recommended)
- libpqxx (PostgreSQL C++ client library)
- libpq (PostgreSQL client library)

### Runtime
- PostgreSQL ≥ 14
- libpqxx runtime library
- libpq runtime library

### Bundled / vendored
- nlohmann/json (via CPM)
- Catch2 (via CPM, tests only)

## Repository layout (excerpt)

- `docker-compose.dev.yml` – PostgreSQL for local development
- `dev.sh` – local development helper script
- `.env` – local environment variables (not committed)
- `build/` – out-of-tree build artifacts

## Building locally (without Docker)

This builds the Risk Sentinel binary without starting any Docker services.
A running PostgreSQL instance is still required at runtime.

```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Run unit tests:

```bash
ctest --test-dir build --output-on-failure
```

Run the binary:

```bash
./build/sentinel
```

## Running with Docker

### Build and start services

This starts:

- the sentinel application

- a PostgreSQL database (for state and configuration)

```bash
docker compose up --build
```
or
```bash
docker compose build --no-cache

docker compose up
```

### Stop services

```bash
docker compose down
```

### Stop and remove volumes
Warning: this deletes database data

```bash
docker compose down -v
```

> Note: Docker is primarily used for integration testing and production-like
> environments.
> During daily development, Risk Sentinel is typically built and run natively
> on the host, while PostgreSQL runs in Docker to keep iteration times short.

## Docker services overview

### app

- C++ Risk Sentinel binary

- built via multi-stage Dockerfile

### db

- PostgreSQL 16

- used for configuration, checkpoints, and alert history

Configuration is currently provided via environment variables
(to be replaced by structured config in later stages).

## Repository structure

```text
.
├── src/ # Application entry point
├── include/ # Shared headers
├── tests/ # Unit tests
├── cmake/ # CMake helpers (CPM)
├── docker/ # Dockerfile
├── .github/workflows/ # CI configuration
├── docker-compose.yml
└── CMakeLists.txt
```

## Design principles

- Deterministic core logic (no black-box decision making)

- Chain-agnostic architecture via adapters

- Low alert noise over maximum coverage

- Operationally simple MVP, scalable later

- AI-based components are planned as optional, external services and are not part of the core decision logic.

## Disclaimer

Risk Sentinel is a monitoring and alerting system.
It does not prevent exploits and does not replace key management, audits, or operational security processes.

## License

License to be defined.