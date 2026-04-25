-- Per-customer Chainlink oracle feed monitoring config.
--
-- No rows are seeded here. Operators populate this table with verified
-- Chainlink aggregator addresses (from the Chainlink "Price Feed Contract
-- Addresses" documentation, per chain) for each customer that wishes to
-- monitor a feed.
--
-- spike_threshold_bps is in basis points (1 bps = 0.01%); a value of 500
-- means alert on a >5% change between consecutive on-chain updates.

CREATE TABLE IF NOT EXISTS customer_oracle_rules (
    id BIGSERIAL PRIMARY KEY,
    customer_id BIGINT NOT NULL REFERENCES customers(id) ON DELETE CASCADE,
    chain_id BIGINT NOT NULL,
    aggregator_address TEXT NOT NULL,        -- lowercase 0x-prefixed
    feed_label TEXT NOT NULL,                -- e.g. "ETH/USD"
    spike_threshold_bps INT NOT NULL,        -- basis points (1bps = 0.01%)
    decimals INT NOT NULL DEFAULT 8,         -- Chainlink default
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(customer_id, chain_id, aggregator_address),
    CHECK (spike_threshold_bps > 0 AND spike_threshold_bps <= 100000),
    CHECK (decimals >= 0 AND decimals <= 30)
);

CREATE INDEX IF NOT EXISTS idx_customer_oracle_rules_customer
    ON customer_oracle_rules(customer_id);
