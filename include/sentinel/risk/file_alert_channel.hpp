#pragma once

#include <cstddef>
#include <fstream>
#include <string>

#include "sentinel/risk/alert_channel.hpp"

namespace sentinel::risk {

class FileAlertChannel : public IAlertChannel {
public:
  explicit FileAlertChannel(std::string output_path);
  ~FileAlertChannel() override;

  void send(const Alert &alert) override;
  std::string name() const override { return "file"; }

  std::size_t alert_count() const { return alert_count_; }
  const std::string &output_path() const { return output_path_; }

private:
  std::string output_path_;
  std::ofstream stream_;
  std::size_t alert_count_ = 0;
};

} // namespace sentinel::risk
