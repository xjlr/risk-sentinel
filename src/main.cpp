#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <chrono>
#include <thread>

#include <pqxx/pqxx>

#include <rigtorp/SPSCQueue.h>

#include "sentinel/db_checkpoint_store.hpp"
#include "sentinel/json.hpp"
#include "sentinel/log.hpp"
#include "sentinel/model.hpp"
#include "sentinel/version.hpp"

#include "sentinel/rpc/JsonRpcClient.hpp"
#include "sentinel/chains/arbitrum/ArbitrumAdapter.hpp"
#include "sentinel/events/NormalizedEvent.hpp"
#include "sentinel/events/EventSource.hpp"

//TODO : Remove 
#include "sentinel/events/RawLog.hpp"

using namespace sentinel::events;
using RingBuffer = rigtorp::SPSCQueue<NormalizedEvent>;

constexpr std::size_t RING_SIZE = 65536;

static std::string getenv_or(const char* k, const char* defv) {
  if (const char* v = std::getenv(k)) return std::string(v);
  return std::string(defv);
}

static bool env_is_true(const char* k) {
  const char* v = std::getenv(k);
  if (!v) return false;
  const std::string s(v);
  return (s == "1" || s == "true" || s == "TRUE" || s == "yes" || s == "on");
}

int main(int argc, char** argv) {
  (void)argc; (void)argv;

  // Logging init (LOG_LEVEL=debug or DEBUG=1)
  const std::string log_level = getenv_or("LOG_LEVEL", "info");
  const bool debug = (log_level == "debug") || env_is_true("DEBUG");
  sentinel::init_logging(debug);

  auto& Lcore   = sentinel::logger(sentinel::LogComponent::Core);
  auto& Ldb     = sentinel::logger(sentinel::LogComponent::Db);
  auto& Lrpc    = sentinel::logger(sentinel::LogComponent::Rpc);
  auto& Lsource = sentinel::logger(sentinel::LogComponent::EventSource);

  RingBuffer buffer(RING_SIZE);
  // TODO : Remove
  sentinel::events::RawLog test;

  buffer.push(NormalizedEvent{});

  if (buffer.front()) { 
        NormalizedEvent value = *buffer.front();
        buffer.pop();
        //std::cout << "Kivettem: " << ertek << std::endl;
        Lcore.info("value from ring buffer={}", value.chain_id);
  }

  Lcore.info("sentinel starting version={}", sentinel::kVersion);

  const std::string chain = getenv_or("CHAIN", "arbitrum");
  const std::string dburl = getenv_or("DATABASE_URL", "");

  Lcore.info("chain={}", chain);

  if (dburl.empty()) {
    Lcore.critical("DATABASE_URL is empty");
    return 2;
  }

  Ldb.info("connecting to database");
  std::shared_ptr<pqxx::connection> conn;
  try {
    conn = std::make_shared<pqxx::connection>(dburl);
    if (!conn->is_open()) {
      Ldb.critical("database connection not open");
      return 3;
    }
  } catch (const std::exception& e) {
    Ldb.critical("database connection failed: {}", e.what());
    return 3;
  }
  Ldb.info("database connection OK");

  sentinel::DbCheckpointStore store(conn);

  Ldb.info("ensuring schema");
  try {
    store.ensure_schema();
  } catch (const std::exception& e) {
    Ldb.critical("ensure_schema failed: {}", e.what());
    return 4;
  }

  Ldb.info("loading checkpoint");
  std::uint64_t last = 0;
  try {
    last = store.get_or_init_checkpoint(chain);
  } catch (const std::exception& e) {
    Ldb.critical("get_or_init_checkpoint failed: {}", e.what());
    return 5;
  }
  Ldb.info("checkpoint loaded chain={} last_block={}", chain, last);

  // Demo event JSON (debug only)
  if (debug) {
    sentinel::NormalizedEvent ev;
    ev.block = {chain, last + 1, "0xdeadbeef", 0};
    ev.tx = {"0x01", "0xfrom", "0xto", 0};
    ev.log = {"0xcontract", {"0xtopic0"}, "0xdata", 0};

    Lcore.debug("sample event json={}", sentinel::to_json(ev).dump());
  }

  // readiness marker
  {
    std::ofstream f("/tmp/sentinel.ready");
    f << "ready\n";
  }
  Lcore.info("sentinel is ready");

  // --- RPC / polling ---
  const std::string rpc_url = getenv_or("ARBITRUM_RPC_URL", "");
  if (rpc_url.empty()) {
    Lrpc.critical("ARBITRUM_RPC_URL is empty");
    return 6;
  }

  JsonRpcClient rpc(rpc_url);
  ArbitrumAdapter arbitrum_adapter{rpc};

  EventSourceConfig cfg{
    .start_block = 0,
    .max_block_range = 1000,
    .poll_interval = std::chrono::milliseconds(500)
  };
  EventSource source{ arbitrum_adapter, buffer, cfg };

  std::jthread event_source_thread([&] {
    source.run();
  });

  Lcore.info("Event Source started");
  std::this_thread::sleep_for(std::chrono::seconds(3));
  source.stop();

#if 0
  ArbitrumAdapter arbitrum(rpc);

  // Sanity check
  try {
    const std::uint64_t cid = arbitrum.chainId();
    Lrpc.info("eth_chainId={}", cid);
  } catch (const std::exception& e) {
    Lrpc.critical("eth_chainId failed: {}", e.what());
    return 7;
  }

  std::uint64_t lastProcessed = last;
  Lsource.info("starting block polling from {}", lastProcessed);

  while (true) {
    try {
      const std::uint64_t latest = arbitrum.latestBlock();
      Lsource.info("latest_block={} last_processed={}", latest, lastProcessed);

      if (latest > lastProcessed) {
        for (std::uint64_t b = lastProcessed + 1; b <= latest; ++b) {
          store.upsert_checkpoint(chain, b);
          lastProcessed = b;
          Lsource.debug("checkpoint advanced -> {}", b);
        }
      }
    } catch (const std::exception& e) {
      // Non-fatal: keep running
      Lsource.warn("polling iteration failed: {}", e.what());
    }

    std::this_thread::sleep_for(std::chrono::seconds(5));
  }
  #endif

  return 0;
}
