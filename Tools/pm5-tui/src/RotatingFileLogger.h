#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace PM5Tui
{
	enum class ELogLevel
	{
		Debug,
		Info,
		Warning,
		Error
	};

	class FRotatingFileLogger
	{
	  public:
		explicit FRotatingFileLogger(
			std::filesystem::path InDirectory,
			std::size_t InMaxFileBytes = 1024 * 1024,
			std::size_t InBackupCount = 3,
			ELogLevel InMinimumLevel = ELogLevel::Debug);
		~FRotatingFileLogger();

		FRotatingFileLogger(const FRotatingFileLogger &) = delete;
		FRotatingFileLogger &operator=(const FRotatingFileLogger &) = delete;

		void Log(ELogLevel Level, std::string_view Message);
		bool IsAvailable() const;
		std::string LastError() const;
		std::filesystem::path CurrentLogPath() const;

	  private:
		void OpenAppend();
		void Rotate();
		std::string FormatRecord(ELogLevel Level, std::string_view Message) const;

		std::filesystem::path Directory;
		std::size_t MaxFileBytes;
		std::size_t BackupCount;
		ELogLevel MinimumLevel;
		mutable std::mutex Mutex;
		std::ofstream Output;
		std::string Error;
	};

	const char *ToString(ELogLevel Level);
} // namespace PM5Tui
