-- Operator-maintained global registry of known bridge contracts.
-- Operators should curate this list with verified addresses from bridge protocol documentation.
CREATE TABLE IF NOT EXISTS bridge_contracts (
    id BIGSERIAL PRIMARY KEY,
    chain_id BIGINT NOT NULL,
    address TEXT NOT NULL,              -- lowercase 0x-prefixed hex
    bridge_name TEXT NOT NULL,          -- human-readable, e.g. "Stargate"
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(chain_id, address)
);

CREATE INDEX IF NOT EXISTS idx_bridge_contracts_chain
    ON bridge_contracts(chain_id);

-- Per-customer bridge transfer rules.
CREATE TABLE IF NOT EXISTS customer_bridge_rules (
    id BIGSERIAL PRIMARY KEY,
    customer_id BIGINT NOT NULL REFERENCES customers(id) ON DELETE CASCADE,
    chain_id BIGINT NOT NULL,
    token_address TEXT NOT NULL,        -- lowercase 0x-prefixed hex
    threshold_raw TEXT NOT NULL,        -- decimal string, 256-bit safe
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(customer_id, chain_id, token_address)
);

CREATE INDEX IF NOT EXISTS idx_customer_bridge_rules_customer
    ON customer_bridge_rules(customer_id);

-- bridge_contracts is intentionally left empty by this migration.
-- Operators must populate it with verified addresses sourced directly from
-- each bridge protocol's official documentation. Seeding unverified or
-- fabricated addresses here would risk shipping a misrouted allowlist to
-- production; an empty registry makes BridgeTransferRule a safe no-op until
-- an operator explicitly curates entries.
