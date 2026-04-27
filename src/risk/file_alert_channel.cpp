#include "sentinel/risk/file_alert_channel.hpp"

#include <filesystem>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "sentinel/log.hpp"

namespace sentinel::risk {

FileAlertChannel::FileAlertChannel(std::string output_path)
    : output_path_(std::move(output_path)) {
  std::filesystem::path p(output_path_);
  if (p.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec) {
      throw std::runtime_error(
          "FileAlertChannel: failed to create parent directory '" +
          p.parent_path().string() + "': " + ec.message());
    }
  }

  stream_.open(output_path_, std::ios::out | std::ios::trunc);
  if (!stream_.is_open()) {
    throw std::runtime_error(
        "FileAlertChannel: failed to open output file '" + output_path_ + "'");
  }
}

FileAlertChannel::~FileAlertChannel() {
  if (stream_.is_open()) {
    stream_.flush();
    stream_.close();
  }
}

void FileAlertChannel::send(const Alert &alert) {
  const std::size_t tentative_index = alert_count_ + 1;

  nlohmann::ordered_json payload;
  payload["alert_index"] = static_cast<uint64_t>(tentative_index);
  payload["customer_id"] = alert.customer_id;
  payload["rule_type"]   = alert.rule_type;
  payload["message"]     = alert.message;
  payload["timestamp_ms"] = alert.timestamp_ms;
  if (alert.chain_id.has_value())
    payload["chain_id"] = *alert.chain_id;
  if (alert.token_address.has_value())
    payload["token_address"] = *alert.token_address;
  if (alert.amount_decimal.has_value())
    payload["amount_decimal"] = *alert.amount_decimal;

  try {
    stream_ << payload.dump() << '\n';
    stream_.flush();
    if (!stream_.good()) {
      auto &Lalert = sentinel::logger(sentinel::LogComponent::Alert);
      Lalert.error("FileAlertChannel: write failure on '{}' (alert_index={})",
                   output_path_, tentative_index);
      stream_.clear();
      return;
    }
    alert_count_ = tentative_index;
  } catch (const std::exception &e) {
    auto &Lalert = sentinel::logger(sentinel::LogComponent::Alert);
    Lalert.error("FileAlertChannel: exception serializing alert: {}", e.what());
    stream_.clear();
  }
}

} // namespace sentinel::risk
