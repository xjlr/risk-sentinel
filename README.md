# Risk Sentinel

A low-latency, on-chain risk monitoring engine for DeFi protocols, DAOs, and Web3 treasuries.

## Overview

Risk Sentinel ingests raw EVM logs from an Arbitrum RPC endpoint, normalizes them into typed signals, evaluates a set of customer-defined risk rules against each signal, and dispatches structured alerts to configured output channels. The full path from an on-chain event to a delivered alert typically completes in under a second.

What distinguishes it from generic analytics tools: it is a self-hostable C++ engine where the hot path is completely lock-free, rule logic is transparent and auditable in source, alerts are scoped per customer so a single instance serves multiple protocols, and all alert channels — including webhooks — are production-grade with request signing and encrypted secret storage.

## Architecture

```mermaid
graph TD
    %% Style definition
    classDef external fill:#f9f,stroke:#333,stroke-width:2px,color:#000;
    classDef fastThread fill:#d4edda,stroke:#28a745,stroke-width:2px,color:#000;
    classDef slowThread fill:#fff3cd,stroke:#ffc107,stroke-width:2px,color:#000;
    classDef buffer fill:#e2e3e5,stroke:#333,stroke-dasharray: 5 5,color:#000;
    classDef db fill:#cce5ff,stroke:#004085,stroke-width:2px,color:#000;

    %% External resources
    ArbRPC[Arbitrum RPC / WSS]:::external
    BaseRPC[Base RPC / WSS]:::external

    %% CHAIN 1 PIPELINE (ARBITRUM)
    subgraph "Chain 1: Arbitrum Silo (Lock-Free)"
        Adapter1("Adapter Thread<br/>(WSS Client)"):::fastThread
        Ring1[("SPSC RingBuffer<br/>(NormalizedEvent)")]:::buffer
        Engine1("Risk Engine Thread<br/>(In-Memory State)"):::fastThread

        %% Output Queues
        Q_DB1[("DB Queue (Events + CheckpointHints) 1<br/>(SPSC)")]:::buffer
        Q_Alert1[("Alert Queue 1<br/>(SPSC)")]:::buffer
    end

    %% CHAIN 2 PIPELINE (BASE)
    subgraph "Chain 2: Base Silo (Lock-Free)"
        Adapter2("Adapter Thread<br/>(WSS Client)"):::fastThread
        Ring2[("SPSC RingBuffer<br/>(NormalizedEvent)")]:::buffer
        Engine2("Risk Engine Thread<br/>(In-Memory State)"):::fastThread

        %% Output Queues
        Q_DB2[("DB Queue (Events + CheckpointHints) 2<br/>(SPSC)")]:::buffer
        Q_Alert2[("Alert Queue 2<br/>(SPSC)")]:::buffer
    end

    %% GLOBAL THREADS
    subgraph "Global IO Handlers"
        Persister("Persister Thread<br/>(Batch Writer)"):::slowThread
        Alerter("AlertDispatcher Thread<br/>(Fan-out & Network)"):::slowThread
    end

    %% STORAGE & OUTPUTS
    Postgres[("PostgreSQL DB<br/>(Config, Checkpoints)")]:::db
    Clients[("Console / Telegram / Webhook")]:::external

    %% Flow

    %% Input
    ArbRPC -->|JSON Stream| Adapter1
    BaseRPC -->|JSON Stream| Adapter2

    %% Internal fast path
    Adapter1 -->|Push| Ring1
    Ring1 -->|Pop & Process| Engine1

    Adapter2 -->|Push| Ring2
    Ring2 -->|Pop & Process| Engine2

    %% Output to queues
    Engine1 -->|Push Event/State| Q_DB1
    Engine1 -->|Push Alert| Q_Alert1

    Engine2 -->|Push Event/State| Q_DB2
    Engine2 -->|Push Alert| Q_Alert2

    %% Global consumption
    Q_DB1 -.->|Poll| Persister
    Q_DB2 -.->|Poll| Persister

    Q_Alert1 -.->|Poll| Alerter
    Q_Alert2 -.->|Poll| Alerter

    %% Final IO
    Persister -->|Batch TRANSACTION| Postgres
    Alerter -->|HTTP POST| Clients
```

**Thread model (current implementation):**

| Thread | Class | Role |
|--------|-------|------|
| EventSource | `EventSource` | Polls Arbitrum RPC, deserializes JSON logs, normalizes into typed `Signal` structs, pushes to the SPSC ring buffer |
| RiskEngine | `RiskEngine` | Pops signals from the ring buffer, iterates registered rules via interest-mask dispatch, pushes `Alert` structs to the `AlertDispatcher` queue |
| AlertDispatcher | `AlertDispatcher` | Dequeues alerts, fans out to every registered `IAlertChannel` (Console, Telegram, Webhook), records Prometheus metrics |

**Why the hot path is lock-free:** the `EventSource → RingBuffer → RiskEngine` path uses rigtorp's `SPSCQueue`, a single-producer / single-consumer lock-free queue with no atomic CAS loops. The `RiskEngine` thread neither acquires a mutex nor allocates heap memory in its evaluation loop. Locking only appears in the `AlertDispatcher` queue, which runs on its own thread and is never called from the hot path.

## Signal Types

All signals are derived from raw EVM log entries by matching `topic0`. The normalizer runs on the `EventSource` thread; the resulting `Signal` struct is what rule engines receive.

| Signal Type | Solidity Event | topic0 |
|---|---|---|
| Transfer | `Transfer(address indexed from, address indexed to, uint256 value)` | `0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef` |
| MintBurn | Derived from Transfer: `from == address(0)` → Mint; `to == address(0)` → Burn | `0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef` |
| Governance — OwnershipTransferred | `OwnershipTransferred(address indexed previousOwner, address indexed newOwner)` | `0x8be0079c531659141344cd1fd0a4f28419497f9722a3daafe3b4186f6b6457e0` |
| Governance — Paused | `Paused(address account)` | `0x62e78cea01bee320cd4e420270b5ea74000d11b0c9f74754ebdbfc544b05a258` |
| Governance — Unpaused | `Unpaused(address account)` | `0x5db9ee0a495bf2e6ff9c91a7834c1ba4fdd244a5e8aa4e537bd38aeae4b073aa` |
| Governance — RoleGranted | `RoleGranted(bytes32 indexed role, address indexed account, address indexed sender)` | `0x2f8788117e7eff1d82e926ec794901d17c78024a50270940304540a733656f0d` |
| Governance — RoleRevoked | `RoleRevoked(bytes32 indexed role, address indexed account, address indexed sender)` | `0xf6391f5c32d9c69d2a47ea670b442974b53935d1edc7fd64eb21e047a839171b` |
| Governance — Upgraded | `Upgraded(address indexed implementation)` | `0xbc7cd75a20ee27fd9adebab32041f755214dbc6bffa90cc0225b39da2e5c2d3b` |
| Approval | `Approval(address indexed owner, address indexed spender, uint256 value)` | `0x8c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925` |
| Swap (Uniswap V2) | `Swap(address indexed sender, uint256 amount0In, uint256 amount1In, uint256 amount0Out, uint256 amount1Out, address indexed to)` | `0xd78ad95fa46c994b6551d0da85fc275fe613ce37657fb8d5e3d130840159d822` |
| Swap (Uniswap V3) | `Swap(address indexed sender, address indexed recipient, int256 amount0, int256 amount1, uint160 sqrtPriceX96, uint128 liquidity, int24 tick)` | `0xc42079f94a6350d7e5735f2a153e368fc95153e172ee90824036511a8fb417b3` |
| LiquidityChange (Mint) | `Mint(address indexed sender, uint256 amount0, uint256 amount1)` | `0x4c209b5fc8ad50758f13e2e1088ba56a560dff694af163234d7f6c31034c568d` |
| LiquidityChange (Burn) | `Burn(address indexed sender, uint256 amount0, uint256 amount1, address indexed to)` | `0xdccd28e36055d506992d9d40a08e08d6691c9569707248066f2c2f82998396e9` |

> Swap and LiquidityChange signals are normalized and classified but no alert rules are implemented for them yet.

## Risk Rules

Rules are evaluated by the `RiskEngine` using an interest-mask: each rule declares which `SignalType` values it handles; the engine skips rules that don't match. All rule configs are loaded from PostgreSQL at startup, scoped to a `customer_id`.

| Rule | Signal Type | Description |
|---|---|---|
| LargeTransfer | Transfer | Fires when `amount > threshold` for a monitored token on a given chain |
| OwnershipChange | Governance | Fires on `OwnershipTransferred` for any contract in the customer's watchlist |
| RoleGranted | Governance | Fires on `RoleGranted` for any contract in the customer's watchlist |
| RoleRevoked | Governance | Fires on `RoleRevoked` for any contract in the customer's watchlist |
| PauseStateChanged | Governance | Fires on `Paused` or `Unpaused` for any contract in the customer's watchlist |
| Upgraded | Governance | Fires on `Upgraded` (proxy implementation change) for any contract in the customer's watchlist |
| MintBurn | MintBurn | Fires when a mint or burn amount exceeds the per-customer threshold for a monitored token |
| Approval (large) | Approval | Fires when an ERC-20 allowance exceeds a per-customer threshold |
| Approval (infinite) | Approval | Fires when `allowance == uint256.max`; configurable per customer |

## Alert Channels

Multiple channels can be active simultaneously. The `AlertDispatcher` fans out every alert to all registered channels.

| Channel | Delivery | Per-Customer | Signed |
|---|---|---|---|
| Console | `spdlog` to stdout | No | No |
| Telegram | HTTP POST to Telegram Bot API (`/sendMessage`) | No — shared `chat_id` | No |
| Webhook | HTTPS POST to customer-supplied URL | Yes — one or more URLs per customer | Yes — HMAC-SHA256; secrets AES-256-GCM encrypted at rest |

## Database Schema

| Table | Purpose |
|---|---|
| `customers` | Customer registry; root foreign key for all rule and channel tables |
| `customer_risk_rules` | Generic rule config store (JSONB params); currently used for `large_transfer` rules |
| `checkpoints` | Per-chain last-processed block; created automatically at startup by `DbCheckpointStore` |
| `customer_governance_rules` | Governance monitoring config: one row per (customer, chain, contract address) |
| `customer_mint_burn_rules` | Mint/burn alert thresholds: mint and burn limits per (customer, chain, token) |
| `customer_approval_rules` | Approval alert config: allowance threshold and infinite-approval flag per (customer, chain, token) |
| `customer_webhook_channels` | Webhook endpoint registry: URL and AES-256-GCM encrypted HMAC secret per customer |

## Observability

Risk Sentinel exposes Prometheus-compatible metrics at `http://<host>:8080/metrics` (configurable via `METRICS_LISTEN_ADDRESS`).

### Counters

| Metric | Labels | Description |
|---|---|---|
| `events_ingested_total` | `chain` | Raw EVM log entries received from the RPC endpoint |
| `signals_normalized_total` | `chain` | Signals successfully classified and pushed to the ring buffer |
| `alerts_generated_total` | `chain`, `rule_type` | Alerts produced by the risk engine |
| `alerts_sent_total` | `chain`, `channel` | Alerts successfully delivered by a channel |
| `alerts_send_failures_total` | `chain`, `channel` | Alert delivery failures (network errors, non-2xx HTTP, etc.) |
| `rpc_calls_total` | `chain`, `method`, `status` | Total JSON-RPC calls made — `method` is the RPC method name (e.g. `eth_getLogs`); `status` is `success` or `error` |

### Gauges

| Metric | Labels | Description |
|---|---|---|
| `ring_buffer_depth` | `chain` | Current number of signals in the SPSC ring buffer |
| `alert_queue_depth` | `chain` | Current number of alerts waiting in the dispatcher queue |
| `last_rpc_success_timestamp_seconds` | `chain` | Unix timestamp of the last successful RPC call |
| `last_alert_success_timestamp_seconds` | `chain` | Unix timestamp of the last successfully delivered alert |
| `last_seen_block` | `chain` | Latest block number observed from the RPC |
| `last_processed_block` | `chain` | Latest block number fully processed by the risk engine |

### Histograms

| Metric | Labels | Description |
|---|---|---|
| `alert_send_duration_seconds` | `chain`, `channel` | End-to-end time for one channel's `send()` call |
| `signal_to_alert_seconds` | `chain` | Time from signal ingress to alert dispatch |
| `rpc_call_duration_seconds` | `chain` | Round-trip time for each JSON-RPC call |

### Prometheus config

```yaml
global:
  scrape_interval: 5s

scrape_configs:
  - job_name: "risk-sentinel"
    static_configs:
      - targets: ["127.0.0.1:8080"]
```

### Grafana

File-based provisioning is included under `ops/grafana/`. To run Grafana locally:

```bash
docker run -d \
  --name grafana \
  --network host \
  -e GF_SECURITY_ADMIN_USER=admin \
  -e GF_SECURITY_ADMIN_PASSWORD=admin \
  -v "$HOME/dev/risk-sentinel/ops/grafana/provisioning/datasources:/etc/grafana/provisioning/datasources" \
  -v "$HOME/dev/risk-sentinel/ops/grafana/provisioning/dashboards:/etc/grafana/provisioning/dashboards" \
  -v "$HOME/dev/risk-sentinel/ops/grafana/dashboards:/var/lib/grafana/dashboards" \
  grafana/grafana-oss
```

## Webhook Integration

The webhook channel delivers a signed HTTPS POST to one or more customer-supplied URLs whenever an alert fires for that customer. Each customer can have multiple endpoints; all receive the same payload independently (fan-out, not failover).

### JSON payload

```json
{
  "customer_id":   42,
  "rule_type":     "large_transfer",
  "message":       "Large transfer detected",
  "timestamp_ms":  1714000000000,
  "chain_id":      42161,
  "token_address": "0xfd086bc7cd5c481dcc9c85ebe478a1c0b69fcbb9",
  "amount_decimal": "250000.000000"
}
```

| Field | Type | Required |
|---|---|---|
| `customer_id` | uint64 | Always |
| `rule_type` | string | Always |
| `message` | string | Always |
| `timestamp_ms` | uint64 | Always |
| `chain_id` | uint64 | Only when applicable |
| `token_address` | string (0x hex) | Only when applicable |
| `amount_decimal` | string (decimal) | Only when applicable |

Optional fields are **omitted entirely** when not set — they are never sent as `null`.

### Request headers

```
Content-Type: application/json
X-Risk-Sentinel-Timestamp: 1714000000000
X-Risk-Sentinel-Signature: sha256=f3a1b2c3d4...
```

`X-Risk-Sentinel-Signature` is only present when an HMAC secret is configured for the endpoint.

### Signing algorithm

The signature covers both the timestamp and the body to prevent timestamp substitution on replayed requests. This matches the pattern used by Stripe and GitHub webhooks.

```
signing_string = timestamp_ms + "." + body
signature      = "sha256=" + HMAC-SHA256(secret, signing_string)
```

### Verification (Python)

```python
import hashlib
import hmac
import time

def verify_sentinel_webhook(
    secret: str,
    body: bytes,
    timestamp_ms: str,
    signature: str,
    max_age_ms: int = 300_000,  # 5 minutes
) -> bool:
    # Replay protection: reject requests outside the tolerance window
    age_ms = abs(time.time() * 1000 - int(timestamp_ms))
    if age_ms > max_age_ms:
        return False

    signing_string = (timestamp_ms + "." + body.decode()).encode()
    expected = "sha256=" + hmac.new(
        secret.encode(), signing_string, hashlib.sha256
    ).hexdigest()
    return hmac.compare_digest(expected, signature)
```

### Replay protection

The `X-Risk-Sentinel-Timestamp` header carries the dispatch time in milliseconds. Receivers should reject any request where `|now_ms - timestamp_ms| > 300_000` (five minutes). The HMAC covers this timestamp, so it cannot be forged or swapped independently.

## Customer Onboarding (Webhook)

### Step 1 — Generate the master encryption key (once per deployment)

```bash
openssl rand -hex 32
# Example output: a3f2b1c4d5e6f7a8b9c0d1e2f3a4b5c6d7e8f9a0b1c2d3e4f5a6b7c8d9e0f1a2
```

Store this value securely (e.g. in Vault or a secrets manager). It never enters the database.

### Step 2 — Export the master key

```bash
export SENTINEL_SECRET_MASTER_KEY=<64-char hex from Step 1>
```

Add to `.env` for local development. In production, inject via your secrets management system.

### Step 3 — Create the customer row

```sql
INSERT INTO customers (customer_key, display_name)
VALUES ('acme', 'Acme Protocol')
ON CONFLICT DO NOTHING;

-- Note the id returned; use it as --customer-id below
SELECT id FROM customers WHERE customer_key = 'acme';
```

### Step 4 — Run the admin CLI

```bash
# Let the CLI generate a random 64-char hex secret:
./build/dev/sentinel admin encrypt-secret \
  --customer-id 42 \
  --url https://hooks.acme.com/risk-sentinel

# Or supply the customer's own secret:
./build/dev/sentinel admin encrypt-secret \
  --customer-id 42 \
  --url https://hooks.acme.com/risk-sentinel \
  --secret <customer-provided-plaintext>
```

The CLI prints the plaintext secret and a ready-to-run `INSERT` statement.

### Step 5 — Run the generated SQL

Copy the `INSERT` statement from the CLI output and run it against your database:

```sql
INSERT INTO customer_webhook_channels
    (customer_id, url, hmac_secret_encrypted, hmac_secret_nonce, enabled)
VALUES (
    42,
    'https://hooks.acme.com/risk-sentinel',
    decode('<ciphertext_hex>', 'hex'),
    decode('<nonce_hex>', 'hex'),
    true
);
```

### Step 6 — Share the plaintext secret with the customer

The CLI prints the plaintext secret exactly once. Send it to the customer through a secure out-of-band channel (e.g. 1Password, HashiCorp Vault, encrypted email). Do not store it anywhere.

The customer uses this secret to verify the `X-Risk-Sentinel-Signature` header on incoming requests.

## Requirements

### Development

- Linux (Ubuntu 22.04 or 24.04 recommended)
- C++20-compatible compiler (GCC ≥ 11 or Clang ≥ 14)
- CMake ≥ 3.20
- Ninja
- Docker + Docker Compose v2
- Git
- PostgreSQL client tools (`psql`, `pg_isready`)

```bash
sudo apt update
sudo apt install postgresql-client
```

- OpenSSL 3.x development headers

```bash
sudo apt install libssl-dev
```

### Docker runtime

- Docker
- Docker Compose v2

### clangd setup

```bash
cmake -S . -B build/dev -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ln -s build/dev/compile_commands.json compile_commands.json
```

## Environment Configuration

| Variable | Required | Default | Description |
|---|---|---|---|
| `DATABASE_URL` | Yes | — | PostgreSQL connection string, e.g. `postgresql://sentinel:sentinel@127.0.0.1:5433/sentinel` |
| `ARBITRUM_RPC_URL` | Yes | — | Arbitrum JSON-RPC endpoint (HTTP or WSS) |
| `CHAIN` | No | `arbitrum` | Chain name label used for metrics and checkpointing |
| `SENTINEL_SECRET_MASTER_KEY` | Required for webhook | — | 64-char hex (32 bytes); used to decrypt HMAC secrets at startup. Webhook channel is disabled if absent or malformed. |
| `TELEGRAM_BOT_TOKEN` | No | — | Telegram Bot API token; Telegram channel is disabled if absent |
| `TELEGRAM_CHAT_ID` | No | — | Telegram chat ID to deliver alerts to |
| `LOG_LEVEL` | No | `info` | Set to `debug` for verbose output |
| `DEBUG` | No | `false` | Alias for `LOG_LEVEL=debug`; accepts `1`, `true`, `yes`, `on` |

Create a `.env` file for local development:

```env
DATABASE_URL=postgresql://sentinel:sentinel@127.0.0.1:5433/sentinel
ARBITRUM_RPC_URL=https://arb-mainnet.g.alchemy.com/v2/<key>
CHAIN=arbitrum
SENTINEL_SECRET_MASTER_KEY=<64-char hex>
```

## Local Development

PostgreSQL runs in Docker; the application is built and executed natively. This avoids Docker rebuild overhead during active development.

```bash
./dev.sh
```

To reset the database before starting:

```bash
RESET_DB=1 ./dev.sh
```

The `dev.sh` script:
1. Starts the PostgreSQL container via Docker Compose
2. Waits for the database using `pg_isready`
3. Applies all migrations under `db/`
4. Builds and runs the Risk Sentinel binary

## Building

```bash
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_TESTS=ON
cmake --build build/dev -j$(nproc)
```

Run unit tests:

```bash
ctest --test-dir build/dev --output-on-failure
```

Run the binary:

```bash
./build/dev/sentinel
```

Run the admin CLI:

```bash
./build/dev/sentinel admin encrypt-secret --customer-id <id> --url <url>
```

## Docker

### Build and start

```bash
docker compose up --build
```

or

```bash
docker compose build --no-cache
docker compose up
```

### Stop

```bash
docker compose down
```

### Stop and remove volumes

```bash
docker compose down -v
```

> Warning: `down -v` deletes the PostgreSQL data volume.

> Docker is used for integration testing and production-like environments. During daily development, the application runs natively while PostgreSQL runs in Docker.

## Monitoring

See the [Observability](#observability) section for the full metrics reference.

### Prometheus

Start Prometheus:

```bash
prometheus --config.file ops/prometheus/prometheus.yml
```

### Grafana

Start Grafana with the included provisioning config (see [Observability](#observability) for the `docker run` command). Dashboards and datasources are managed from:

- `ops/grafana/provisioning/datasources/`
- `ops/grafana/provisioning/dashboards/`
- `ops/grafana/dashboards/`

## Repository Structure

```text
.
├── src/
│   ├── main.cpp
│   ├── app/                    # App lifecycle and module wiring
│   ├── risk/                   # Alert dispatcher, channels, rules
│   │   └── rules/
│   ├── events/                 # Normalization and EventSource
│   ├── chains/arbitrum/        # Arbitrum RPC adapter
│   ├── security/               # AES-256-GCM + HMAC-SHA256 (crypto.cpp)
│   ├── admin/                  # Admin CLI subcommands (encrypt_secret.cpp)
│   ├── metrics/                # Prometheus metric definitions
│   └── rpc/                    # JSON-RPC client
├── include/sentinel/
│   ├── app/
│   ├── risk/
│   │   └── rules/
│   ├── events/
│   ├── chains/
│   ├── security/               # crypto.hpp
│   ├── admin/                  # encrypt_secret.hpp
│   ├── metrics/
│   └── rpc/
├── tests/
│   ├── test_crypto.cpp
│   ├── test_webhook_alert_channel.cpp
│   └── ...
├── db/
│   ├── 001_schema.sql          # customers, customer_risk_rules
│   ├── 002_seed_dev.sql        # Development seed data
│   ├── 003_governance_rules.sql
│   ├── 004_mint_burn_rules.sql
│   ├── 005_approval_rules.sql
│   └── 006_webhook_channels.sql
├── cmake/                      # CPM.cmake
├── docker/                     # Dockerfile
├── ops/
│   ├── prometheus/
│   └── grafana/
├── scripts/
│   └── db_apply.sh
├── docker-compose.yml
├── docker-compose.dev.yml
└── CMakeLists.txt
```

## Design Principles

- **Deterministic core logic** — no black-box decision making; every alert maps to a specific rule and event
- **Chain-agnostic architecture** — chain-specific behavior is isolated in adapters; the risk engine is chain-neutral
- **Low alert noise over maximum coverage** — rules are threshold-based and customer-scoped; no global default triggers
- **Operationally simple** — single binary, standard PostgreSQL, no external message brokers
- **Secrets encrypted at rest** — HMAC secrets for webhook endpoints are stored as AES-256-GCM ciphertext; the master key never touches the database
- **Single binary, multiple roles** — the same `sentinel` binary serves as both the monitoring engine (`./sentinel`) and the admin CLI (`./sentinel admin encrypt-secret ...`)

## Disclaimer

Risk Sentinel is a monitoring and alerting system. It does not prevent exploits and does not replace key management, audits, or operational security processes.

## License

License to be defined.
