CREATE TABLE IF NOT EXISTS customer_webhook_channels (
    id BIGSERIAL PRIMARY KEY,
    customer_id BIGINT NOT NULL REFERENCES customers(id) ON DELETE CASCADE,
    url TEXT NOT NULL,
    hmac_secret_encrypted BYTEA,
    hmac_secret_nonce BYTEA,
    enabled BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE(customer_id, url),
    CHECK (
        (hmac_secret_encrypted IS NULL AND hmac_secret_nonce IS NULL) OR
        (hmac_secret_encrypted IS NOT NULL AND hmac_secret_nonce IS NOT NULL)
    )
);

CREATE INDEX IF NOT EXISTS idx_webhook_channels_customer_id
    ON customer_webhook_channels(customer_id);
