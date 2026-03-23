CREATE TABLE IF NOT EXISTS customer_governance_rules (
    id BIGSERIAL PRIMARY KEY,
    customer_id BIGINT NOT NULL REFERENCES customers(id) ON DELETE CASCADE,
    chain_id BIGINT NOT NULL,
    contract_address TEXT NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(customer_id, chain_id, contract_address)
);

CREATE INDEX IF NOT EXISTS idx_gov_rules_contract ON customer_governance_rules(contract_address);
