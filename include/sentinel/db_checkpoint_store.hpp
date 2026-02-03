#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include <pqxx/pqxx>   // IMPORTANT: pqxx::connection is an alias in libpqxx 6.x

namespace sentinel {

class DbCheckpointStore {
public:
  explicit DbCheckpointStore(std::shared_ptr<pqxx::connection> conn);

  void ensure_schema();
  std::uint64_t get_or_init_checkpoint(const std::string& chain);
  void upsert_checkpoint(const std::string& chain, std::uint64_t last_block);

private:
  std::shared_ptr<pqxx::connection> conn_;
};

} // namespace sentinel
