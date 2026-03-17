INSERT INTO customers (customer_key, display_name, is_test)
VALUES ('master_test', 'Master Test Customer', true)
ON CONFLICT (customer_key) DO NOTHING;

INSERT INTO customer_risk_rules (customer_id, rule_type, enabled, params_jsonb)
SELECT id,
       'large_transfer',
       true,
       '{
         "chain_id": 42161,
         "token_address": "0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9",
         "threshold": "1000000"
       }'::jsonb
FROM customers
WHERE customer_key = 'master_test'
ON CONFLICT (customer_id, rule_type) DO NOTHING;
