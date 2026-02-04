#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>


#include <pqxx/pqxx>

#include "sentinel/db_checkpoint_store.hpp"
#include "sentinel/json.hpp"
#include "sentinel/model.hpp"
#include "sentinel/version.hpp"

#include "sentinel/rpc/JsonRpcClient.hpp"
#include "sentinel/chains/arbitrum/ArbitrumAdapter.hpp"


static std::string getenv_or(const char* k, const char* defv) {
  if (const char* v = std::getenv(k)) return std::string(v);
  return std::string(defv);
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;

  std::cout << "[BOOT] sentinel starting version=" << sentinel::kVersion << std::endl;

  const std::string chain = getenv_or("CHAIN", "arbitrum");
  const std::string dburl = getenv_or("DATABASE_URL", "");

  std::cout << "[BOOT] chain=" << chain << std::endl;

  if (dburl.empty()) {
    std::cerr << "[FATAL] DATABASE_URL is empty" << std::endl;
    return 2;
  }

  std::cout << "[BOOT] connecting to database" << std::endl;
  std::shared_ptr<pqxx::connection> conn;
  try {
    conn = std::make_shared<pqxx::connection>(dburl);
    if (!conn->is_open()) {
      std::cerr << "[FATAL] database connection not open" << std::endl;
      return 3;
    }
  } catch (const std::exception& e) {
    std::cerr << "[FATAL] database connection failed: " << e.what() << std::endl;
    return 3;
  }
  std::cout << "[BOOT] database connection OK" << std::endl;

  sentinel::DbCheckpointStore store(conn);

  std::cout << "[BOOT] ensuring schema" << std::endl;
  try {
    store.ensure_schema();
  } catch (const std::exception& e) {
    std::cerr << "[FATAL] ensure_schema failed: " << e.what() << std::endl;
    return 4;
  }

  std::cout << "[BOOT] loading checkpoint" << std::endl;
  std::uint64_t last = 0;
  try {
    last = store.get_or_init_checkpoint(chain);
  } catch (const std::exception& e) {
    std::cerr << "[FATAL] get_or_init_checkpoint failed: " << e.what() << std::endl;
    return 5;
  }
  std::cout << "[BOOT] checkpoint loaded chain=" << chain << " last_block=" << last << std::endl;

  // Demo: create one fake normalized event + print JSON (debug)
  sentinel::NormalizedEvent ev;
  ev.block = {chain, last + 1, "0xdeadbeef", 0};
  ev.tx = {"0x01", "0xfrom", "0xto", 0};
  ev.log = {"0xcontract", {"0xtopic0"}, "0xdata", 0};

  std::cout << "[BOOT] sample event json=" << sentinel::to_json(ev).dump() << std::endl;

  // readiness marker
  {
    std::ofstream f("/tmp/sentinel.ready");
    f << "ready" << std::endl;
  }
  std::cout << "[READY] sentinel is ready" << std::endl;
// ###### POLLing

const std::string rpc_url = getenv_or("ARBITRUM_RPC_URL", "");
if (rpc_url.empty()) {
  std::cerr << "[FATAL] ARBITRUM_RPC_URL is empty" << std::endl;
  return 6;
}

JsonRpcClient rpc(rpc_url);
ArbitrumAdapter arbitrum(rpc);

// Sanity check
try {
  std::uint64_t cid = arbitrum.chainId();
  std::cout << "[RPC] eth_chainId=" << cid << std::endl;
} catch (const std::exception& e) {
  std::cerr << "[FATAL] eth_chainId failed: " << e.what() << std::endl;
  return 7;
}

// last = már be van töltve get_or_init_checkpoint()-ből
std::uint64_t lastProcessed = last;

std::cout << "[POLL] starting block polling from " << lastProcessed << std::endl;

// --- Replace the old while(true) ---
while (true) {
  try {
    const std::uint64_t latest = arbitrum.latestBlock();

    std::cout << "[POLL] latest_block=" << latest
              << " last_processed=" << lastProcessed << std::endl;

    if (latest > lastProcessed) {
      for (std::uint64_t b = lastProcessed + 1; b <= latest; ++b) {
        // Day 3: no block/log fetch yet, only checkpoint advance
        store.upsert_checkpoint(chain, b);
        lastProcessed = b;

        std::cout << "[POLL] checkpoint advanced -> " << b << std::endl;
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "[WARN] polling iteration failed: " << e.what() << std::endl;
  }

  std::this_thread::sleep_for(std::chrono::seconds(5));
}

  return 0;
}
