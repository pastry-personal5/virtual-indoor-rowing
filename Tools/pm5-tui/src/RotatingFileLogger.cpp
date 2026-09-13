#include "RotatingFileLogger.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace PM5Tui
{
	namespace
	{
		constexpr std::size_t MaxMessageBytes = 4096;

		std::string Sanitize(std::string_view Message)
		{
			std::string Result;
			Result.reserve(std::min(Message.size(), MaxMessageBytes));
			for (const unsigned char Character : Message)
			{
				if (Result.size() == MaxMessageBytes)
					break;
				if (Character == '\n' || Character == '\r')
					Result.push_back(' ');
				else if (Character < 0x20 && Character != '\t')
					Result.push_back('?');
				else
					Result.push_back(static_cast<char>(Character));
			}
			return Result;
		}
	} // namespace

	const char *ToString(ELogLevel Level)
	{
		switch (Level)
		{
		case ELogLevel::Debug:
			return "DEBUG";
		case ELogLevel::Info:
			return "INFO";
		case ELogLevel::Warning:
			return "WARN";
		case ELogLevel::Error:
			return "ERROR";
		}
		return "UNKNOWN";
	}

	FRotatingFileLogger::FRotatingFileLogger(
		std::filesystem::path InDirectory,
		std::size_t InMaxFileBytes,
		std::size_t InBackupCount,
		ELogLevel InMinimumLevel)
		: Directory(std::move(InDirectory)),
		  MaxFileBytes(std::max<std::size_t>(InMaxFileBytes, 1)),
		  BackupCount(InBackupCount), MinimumLevel(InMinimumLevel)
	{
		std::error_code FileError;
		std::filesystem::create_directories(Directory, FileError);
		if (FileError)
		{
			Error = "unable to create log directory: " + FileError.message();
			return;
		}

		std::filesystem::permissions(Directory,
									 std::filesystem::perms::owner_all,
									 std::filesystem::perm_options::replace,
									 FileError);
		if (FileError)
		{
			Error = "unable to restrict log directory permissions: " +
					FileError.message();
			return;
		}
		OpenAppend();
	}

	FRotatingFileLogger::~FRotatingFileLogger()
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		Output.flush();
		Output.close();
	}

	void FRotatingFileLogger::Log(ELogLevel Level, std::string_view Message)
	{
		if (Level < MinimumLevel)
			return;

		std::lock_guard<std::mutex> Lock(Mutex);
		if (!Output.is_open())
			return;

		const std::string Record = FormatRecord(Level, Message);
		std::error_code FileError;
		const std::uintmax_t ExistingBytes =
			std::filesystem::file_size(CurrentLogPath(), FileError);
		if (FileError || ExistingBytes > MaxFileBytes ||
			Record.size() > MaxFileBytes -
								std::min<std::uintmax_t>(ExistingBytes, MaxFileBytes))
			Rotate();

		if (!Output.is_open())
			OpenAppend();
		if (!Output.is_open())
			return;

		Output << Record;
		Output.flush();
		if (!Output)
		{
			Error = "failed writing log record";
			Output.close();
			return;
		}
		std::filesystem::permissions(CurrentLogPath(),
									 std::filesystem::perms::owner_read |
										 std::filesystem::perms::owner_write,
									 std::filesystem::perm_options::replace,
									 FileError);
		if (FileError)
		{
			Error = "unable to restrict log file permissions: " +
					FileError.message();
			Output.close();
		}
	}

	bool FRotatingFileLogger::IsAvailable() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Output.is_open();
	}

	std::string FRotatingFileLogger::LastError() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Error;
	}

	std::filesystem::path FRotatingFileLogger::CurrentLogPath() const
	{
		return Directory / "pm5-tui.log";
	}

	void FRotatingFileLogger::OpenAppend()
	{
		Error.clear();
		Output.open(CurrentLogPath(), std::ios::out | std::ios::app);
		if (!Output)
		{
			Error = "unable to open log file";
			return;
		}

		std::error_code FileError;
		std::filesystem::permissions(CurrentLogPath(),
									 std::filesystem::perms::owner_read |
										 std::filesystem::perms::owner_write,
									 std::filesystem::perm_options::replace,
									 FileError);
		if (FileError)
		{
			Output.close();
			Error = "unable to restrict log file permissions: " +
					FileError.message();
		}
	}

	void FRotatingFileLogger::Rotate()
	{
		Output.flush();
		Output.close();
		std::error_code FileError;
		if (BackupCount == 0)
			std::filesystem::remove(CurrentLogPath(), FileError);
		for (std::size_t Index = BackupCount; Index > 0; --Index)
		{
			const std::filesystem::path Source =
				Index == 1 ? CurrentLogPath()
						   : Directory / ("pm5-tui.log." +
										  std::to_string(Index - 1));
			const std::filesystem::path Destination =
				Directory / ("pm5-tui.log." + std::to_string(Index));
			if (!std::filesystem::exists(Source, FileError) || FileError)
			{
				FileError.clear();
				continue;
			}
			std::filesystem::remove(Destination, FileError);
			FileError.clear();
			std::filesystem::rename(Source, Destination, FileError);
			if (FileError)
			{
				Error = "unable to rotate log file: " + FileError.message();
				FileError.clear();
			}
		}
		OpenAppend();
	}

	std::string FRotatingFileLogger::FormatRecord(
		ELogLevel Level,
		std::string_view Message) const
	{
		const auto Now = std::chrono::system_clock::now();
		const auto Milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
			Now.time_since_epoch());
		const std::time_t Seconds = std::chrono::system_clock::to_time_t(Now);
		std::tm UtcTime{};
		gmtime_r(&Seconds, &UtcTime);
		std::ostringstream Record;
		Record << std::put_time(&UtcTime, "%Y-%m-%dT%H:%M:%S") << '.'
			   << std::setw(3) << std::setfill('0')
			   << (Milliseconds.count() % 1000) << "Z " << ToString(Level) << ' '
			   << Sanitize(Message) << '\n';
		return Record.str();
	}
} // namespace PM5Tui
