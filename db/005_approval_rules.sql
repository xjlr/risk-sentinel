CREATE TABLE IF NOT EXISTS customer_approval_rules (
    id BIGSERIAL PRIMARY KEY,
    customer_id BIGINT NOT NULL REFERENCES customers(id) ON DELETE CASCADE,
    chain_id BIGINT NOT NULL,
    token_address TEXT NOT NULL,
    threshold_raw TEXT NOT NULL,
    alert_on_infinite BOOLEAN NOT NULL DEFAULT TRUE,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(customer_id, chain_id, token_address)
);

CREATE INDEX IF NOT EXISTS idx_approval_rules_token
    ON customer_approval_rules(token_address);
