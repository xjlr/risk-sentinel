#include "sentinel/db_checkpoint_store.hpp"
#include <pqxx/pqxx>
#include <stdexcept>

namespace sentinel {

DbCheckpointStore::DbCheckpointStore(std::shared_ptr<pqxx::connection> conn)
  : conn_(std::move(conn)) {}

void DbCheckpointStore::ensure_schema() {
  pqxx::work tx(*conn_);
  tx.exec(R"SQL(
    CREATE TABLE IF NOT EXISTS checkpoints (
      chain TEXT PRIMARY KEY,
      last_block BIGINT NOT NULL,
      updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
    );
  )SQL");
  tx.commit();
}

#if 0
std::uint64_t DbCheckpointStore::get_or_init_checkpoint(const std::string& chain) {
  pqxx::work tx(*conn_);

  // If exists
  auto row = tx.exec_params1(
    "SELECT last_block FROM checkpoints WHERE chain = $1",
    chain
  );

  if (!row.empty()) {
    // exec_params1 returns 1-row result set; but may throw if no rows.
  }

  // The above is awkward with exec_params1. Use exec_params + check size.
  tx.commit();
  return 0;
}
#endif

std::uint64_t DbCheckpointStore::get_or_init_checkpoint(const std::string& chain) {
  pqxx::work tx(*conn_);

  auto r = tx.exec_params(
    "SELECT last_block FROM checkpoints WHERE chain = $1",
    chain
  );

  if (r.size() == 1) {
    auto v = r[0][0].as<long long>(0);
    tx.commit();
    return static_cast<std::uint64_t>(v);
  }

  // init to 0
  tx.exec_params(
    "INSERT INTO checkpoints(chain, last_block) VALUES ($1, 0) ON CONFLICT DO NOTHING",
    chain
  );
  tx.commit();
  return 0;
}

void DbCheckpointStore::upsert_checkpoint(const std::string& chain, std::uint64_t last_block) {
  pqxx::work tx(*conn_);
  tx.exec_params(
    R"SQL(
      INSERT INTO checkpoints(chain, last_block)
      VALUES ($1, $2)
      ON CONFLICT (chain)
      DO UPDATE SET last_block = EXCLUDED.last_block, updated_at = now();
    )SQL",
    chain,
    static_cast<long long>(last_block)
  );
  tx.commit();
}

} // namespace sentinel
