#pragma once

#include <sstream>

namespace minirpc {

enum class LogLevel {
  kTrace,
  kDebug,
  kInfo,
  kWarn,
  kError,
  kFatal,
};

// 线程安全日志：默认输出到 stderr，支持 LOG_INFO << ... 流式写法。
class Logger {
 public:
  static Logger& instance();

  void setLogLevel(LogLevel level);
  LogLevel logLevel() const;

  class LogStream {
   public:
    LogStream(Logger& logger, LogLevel level, const char* file, int line);
    ~LogStream();

    template <typename T>
    LogStream& operator<<(const T& value) {
      os_ << value;
      return *this;
    }

   private:
    Logger& logger_;
    LogLevel level_;
    std::ostringstream os_;
  };

  void log(LogLevel level, const std::string& message);

 private:
  Logger() = default;
  LogLevel level_ = LogLevel::kInfo;
};

}  // namespace minirpc

#define LOG_TRACE minirpc::Logger::LogStream( \
    minirpc::Logger::instance(), minirpc::LogLevel::kTrace, __FILE__, __LINE__)
#define LOG_DEBUG minirpc::Logger::LogStream( \
    minirpc::Logger::instance(), minirpc::LogLevel::kDebug, __FILE__, __LINE__)
#define LOG_INFO minirpc::Logger::LogStream( \
    minirpc::Logger::instance(), minirpc::LogLevel::kInfo, __FILE__, __LINE__)
#define LOG_WARN minirpc::Logger::LogStream( \
    minirpc::Logger::instance(), minirpc::LogLevel::kWarn, __FILE__, __LINE__)
#define LOG_ERROR minirpc::Logger::LogStream( \
    minirpc::Logger::instance(), minirpc::LogLevel::kError, __FILE__, __LINE__)
#define LOG_FATAL minirpc::Logger::LogStream( \
    minirpc::Logger::instance(), minirpc::LogLevel::kFatal, __FILE__, __LINE__)

