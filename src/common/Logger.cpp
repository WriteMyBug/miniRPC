#include "minirpc/common/Logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

namespace minirpc {

namespace {

const char* levelToString(LogLevel level) {
  switch (level) {
    case LogLevel::kTrace: return "TRACE";
    case LogLevel::kDebug: return "DEBUG";
    case LogLevel::kInfo:  return "INFO";
    case LogLevel::kWarn:  return "WARN";
    case LogLevel::kError: return "ERROR";
    case LogLevel::kFatal: return "FATAL";
  }
  return "UNKNOWN";
}

std::string currentTimestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto ms =
      duration_cast<milliseconds>(now.time_since_epoch()) % milliseconds(1000);
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
  ::localtime_r(&t, &tm);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
  return buf;
}

}  // namespace

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

void Logger::setLogLevel(LogLevel level) {
  level_ = level;
}

LogLevel Logger::logLevel() const {
  return level_;
}

void Logger::log(LogLevel level, const std::string& message) {
  if (level < level_) {
    return;
  }
  static std::mutex mutex;
  std::lock_guard<std::mutex> lock(mutex);
  std::ostringstream oss;
  oss << std::this_thread::get_id();
  std::fprintf(stderr, "[%s] [%s] [tid=%s] %s\n", currentTimestamp().c_str(),
               levelToString(level), oss.str().c_str(), message.c_str());
}

Logger::LogStream::LogStream(Logger& logger, LogLevel level, const char* file,
                             int line)
    : logger_(logger), level_(level) {
  os_ << file << ':' << line << ' ';
}

Logger::LogStream::~LogStream() {
  logger_.log(level_, os_.str());
}

}  // namespace minirpc

