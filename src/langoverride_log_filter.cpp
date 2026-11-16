#include "langoverride_log_filter.h"

#include <rex/logging.h>

#include <spdlog/sinks/base_sink.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace {

std::atomic<uint64_t> g_suppressed{0};

// Forwards krnl messages to the original sinks, but drops repeated
// langoverride.txt NtCreateFile FAILED warnings after the first miss.
class LangOverrideOnceSink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  explicit LangOverrideOnceSink(std::vector<spdlog::sink_ptr> sinks)
      : sinks_(std::move(sinks)) {}

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    constexpr std::string_view kOptionalMiss =
        "[NtCreateFile] FAILED: path='d:\\langoverride.txt' -> 0xc000000f";
    if (msg.level == spdlog::level::warn &&
        std::string_view(msg.payload.data(), msg.payload.size()) == kOptionalMiss) {
      if (!seen_langoverride_) {
        seen_langoverride_ = true;
        Forward(msg);
        return;
      }
      const uint64_t count = g_suppressed.fetch_add(1, std::memory_order_relaxed) + 1;
      // Report at 128, 256, 512, etc. even if the game never exits cleanly.
      if (count >= 128 && (count & (count - 1)) == 0) {
        const auto text = fmt::format("Suppressed {} repeated langoverride.txt file-not-found warnings", count);
        auto summary = msg;
        summary.payload = spdlog::string_view_t(text.data(), text.size());
        Forward(summary);
      }
      return;
    }
    Forward(msg);
  }

  void Forward(const spdlog::details::log_msg& msg) {
    for (auto& sink : sinks_) {
      sink->log(msg);
    }
  }

  void flush_() override {
    for (auto& sink : sinks_) {
      sink->flush();
    }
  }

 private:
  std::vector<spdlog::sink_ptr> sinks_;
  bool seen_langoverride_ = false;  // protected by base_sink's mutex
};

}  // namespace

void InstallLangOverrideLogFilter() {
  auto logger = rex::GetLogger(rex::log::krnl());
  if (!logger) {
    return;
  }
  auto& sinks = logger->sinks();
  if (sinks.empty()) {
    return;
  }
  // Preserve existing file/console sinks; wrap them so only this path is gated.
  auto filter = std::make_shared<LangOverrideOnceSink>(sinks);
  sinks.clear();
  sinks.push_back(std::move(filter));
}

void ReportSuppressedLangOverrideWarnings() {
  const auto count = g_suppressed.load(std::memory_order_relaxed);
  if (count != 0) REXLOG_INFO("Suppressed {} repeated langoverride.txt warnings this session", count);
}
