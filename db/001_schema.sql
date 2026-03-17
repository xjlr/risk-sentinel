CREATE TABLE IF NOT EXISTS customers (
    id BIGSERIAL PRIMARY KEY,
    customer_key TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    is_test BOOLEAN NOT NULL DEFAULT FALSE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS customer_risk_rules (
    id BIGSERIAL PRIMARY KEY,
    customer_id BIGINT NOT NULL REFERENCES customers(id) ON DELETE CASCADE,
    rule_type TEXT NOT NULL,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    params_jsonb JSONB NOT NULL DEFAULT '{}'::jsonb,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(customer_id, rule_type)
);

CREATE INDEX IF NOT EXISTS idx_customer_risk_rules_customer_id ON customer_risk_rules(customer_id);
CREATE INDEX IF NOT EXISTS idx_customer_risk_rules_rule_type ON customer_risk_rules(rule_type);
