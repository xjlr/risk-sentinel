#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/file_alert_channel.hpp"

using namespace sentinel::risk;

namespace {

std::filesystem::path unique_tmp_path(const std::string &suffix) {
    static std::atomic<uint64_t> counter{0};
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    const auto pid = static_cast<uint64_t>(::getpid());
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("file_alert_channel_test_" + std::to_string(pid) + "_" +
            std::to_string(now) + "_" + std::to_string(id) + suffix);
}

Alert make_alert(CustomerId cid, const std::string &rule_type) {
    Alert a{};
    a.customer_id = cid;
    a.rule_type = rule_type;
    a.message = "test alert";
    a.timestamp_ms = 1000000ULL;
    return a;
}

std::vector<std::string> read_lines(const std::filesystem::path &path) {
    std::vector<std::string> lines;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        lines.push_back(std::move(line));
    }
    return lines;
}

} // namespace

TEST_CASE("FileAlertChannel: creates file and parent directory",
          "[file_alert_channel]") {
    const auto base = unique_tmp_path(".d");
    const auto path = base / "nested" / "alerts.jsonl";

    {
        FileAlertChannel ch(path.string());
        REQUIRE(std::filesystem::exists(path));
    }

    std::filesystem::remove_all(base);
}

TEST_CASE("FileAlertChannel: write JSONL line for an alert with all fields",
          "[file_alert_channel]") {
    const auto path = unique_tmp_path(".jsonl");
    {
        FileAlertChannel ch(path.string());
        Alert a = make_alert(42, "large_transfer");
        a.chain_id = 1ULL;
        a.token_address = "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48";
        a.amount_decimal = "500000000000";
        ch.send(a);
        REQUIRE(ch.alert_count() == 1);
    }

    auto lines = read_lines(path);
    REQUIRE(lines.size() == 1);
    auto j = nlohmann::json::parse(lines[0]);
    REQUIRE(j["alert_index"].get<uint64_t>() == 1);
    REQUIRE(j["customer_id"].get<uint64_t>() == 42);
    REQUIRE(j["rule_type"].get<std::string>() == "large_transfer");
    REQUIRE(j["message"].get<std::string>() == "test alert");
    REQUIRE(j["timestamp_ms"].get<uint64_t>() == 1000000ULL);
    REQUIRE(j["chain_id"].get<uint64_t>() == 1ULL);
    REQUIRE(j["token_address"].get<std::string>() ==
            "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48");
    REQUIRE(j["amount_decimal"].get<std::string>() == "500000000000");

    std::filesystem::remove(path);
}

TEST_CASE("FileAlertChannel: optional fields omitted when std::nullopt",
          "[file_alert_channel]") {
    const auto path = unique_tmp_path(".jsonl");
    {
        FileAlertChannel ch(path.string());
        Alert a = make_alert(7, "governance");
        ch.send(a);
    }

    auto lines = read_lines(path);
    REQUIRE(lines.size() == 1);
    auto j = nlohmann::json::parse(lines[0]);
    REQUIRE(j.contains("alert_index"));
    REQUIRE(j.contains("customer_id"));
    REQUIRE(j.contains("rule_type"));
    REQUIRE(j.contains("message"));
    REQUIRE(j.contains("timestamp_ms"));
    REQUIRE_FALSE(j.contains("chain_id"));
    REQUIRE_FALSE(j.contains("token_address"));
    REQUIRE_FALSE(j.contains("amount_decimal"));

    std::filesystem::remove(path);
}

TEST_CASE("FileAlertChannel: alert_index is sequential starting at 1",
          "[file_alert_channel]") {
    const auto path = unique_tmp_path(".jsonl");
    {
        FileAlertChannel ch(path.string());
        for (int i = 0; i < 3; ++i) {
            Alert a = make_alert(static_cast<CustomerId>(i + 1), "mint_burn");
            ch.send(a);
        }
        REQUIRE(ch.alert_count() == 3);
    }

    auto lines = read_lines(path);
    REQUIRE(lines.size() == 3);
    for (size_t i = 0; i < lines.size(); ++i) {
        auto j = nlohmann::json::parse(lines[i]);
        REQUIRE(j["alert_index"].get<uint64_t>() == i + 1);
    }

    std::filesystem::remove(path);
}

TEST_CASE("FileAlertChannel: each line is independently parseable JSON",
          "[file_alert_channel]") {
    const auto path = unique_tmp_path(".jsonl");
    {
        FileAlertChannel ch(path.string());
        for (int i = 0; i < 5; ++i) {
            Alert a = make_alert(static_cast<CustomerId>(100 + i), "approval");
            a.chain_id = 1ULL;
            ch.send(a);
        }
    }

    auto lines = read_lines(path);
    REQUIRE(lines.size() == 5);
    for (const auto &line : lines) {
        nlohmann::json parsed;
        REQUIRE_NOTHROW(parsed = nlohmann::json::parse(line));
        (void)parsed;
    }

    std::filesystem::remove(path);
}

TEST_CASE("FileAlertChannel: throws on unwritable path",
          "[file_alert_channel]") {
    REQUIRE_THROWS_AS(FileAlertChannel(std::string("/proc/this/should/not/be/writable")),
                      std::runtime_error);
}

TEST_CASE("FileAlertChannel: file closes cleanly on destruction",
          "[file_alert_channel]") {
    const auto path = unique_tmp_path(".jsonl");
    {
        FileAlertChannel ch(path.string());
        Alert a = make_alert(1, "oracle_update");
        ch.send(a);
    }
    // Re-open after destruction to confirm flush+close happened.
    auto lines = read_lines(path);
    REQUIRE(lines.size() == 1);
    nlohmann::json parsed;
    REQUIRE_NOTHROW(parsed = nlohmann::json::parse(lines[0]));
    (void)parsed;

    std::filesystem::remove(path);
}

TEST_CASE("FileAlertChannel: failed write does not increment count",
          "[file_alert_channel]") {
    // /dev/full is a Linux character device that opens normally but every
    // write returns ENOSPC. This is the cleanest way to force a write
    // failure without exposing internal stream state from the channel.
    // Skip the test on platforms where /dev/full is not present.
    if (!std::filesystem::exists("/dev/full")) {
        SUCCEED("/dev/full not available on this platform; skipping");
        return;
    }

    FileAlertChannel ch("/dev/full");
    REQUIRE(ch.alert_count() == 0);

    Alert a = make_alert(1, "large_transfer");
    ch.send(a);
    // Every write to /dev/full returns ENOSPC, so the count must remain 0.
    REQUIRE(ch.alert_count() == 0);

    // A second attempt also fails and the count still does not advance.
    ch.send(a);
    REQUIRE(ch.alert_count() == 0);
}
