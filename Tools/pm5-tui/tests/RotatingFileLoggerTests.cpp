#include "RotatingFileLogger.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
	const std::filesystem::path TestDirectory =
		std::filesystem::temp_directory_path() /
		("vir-pm5-tui-logger-" +
		 std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now().time_since_epoch())
							.count()));

	{
		PM5Tui::FRotatingFileLogger Logger(TestDirectory, 90, 2);
		assert(Logger.IsAvailable());
		assert(std::filesystem::exists(TestDirectory));
		Logger.Log(PM5Tui::ELogLevel::Debug, "first\nforged-line");
		Logger.Log(PM5Tui::ELogLevel::Info, "second");
		Logger.Log(PM5Tui::ELogLevel::Warning, "third");
		Logger.Log(PM5Tui::ELogLevel::Error, "fourth");
		Logger.Log(PM5Tui::ELogLevel::Debug, "fifth");
	}

	assert(std::filesystem::exists(TestDirectory / "pm5-tui.log"));
	assert(std::filesystem::exists(TestDirectory / "pm5-tui.log.1"));
	assert(std::filesystem::exists(TestDirectory / "pm5-tui.log.2"));
	std::string Contents;
	for (const char *Suffix : {"", ".1", ".2"})
	{
		const std::filesystem::path LogPath =
			TestDirectory / (std::string("pm5-tui.log") + Suffix);
		assert(std::filesystem::file_size(LogPath) <= 90);
		std::ifstream Input(LogPath);
		Contents.append(std::istreambuf_iterator<char>(Input),
						std::istreambuf_iterator<char>());
	}
	assert(Contents.find(" DEBUG first forged-line\n") != std::string::npos);
	assert(Contents.find("DEBUG fifth") != std::string::npos);
	assert(Contents.find("first\nforged-line") == std::string::npos);

	const auto DirectoryPermissions =
		std::filesystem::status(TestDirectory).permissions();
	assert((DirectoryPermissions & (std::filesystem::perms::group_all |
									std::filesystem::perms::others_all)) ==
		   std::filesystem::perms::none);
	const auto FilePermissions =
		std::filesystem::status(TestDirectory / "pm5-tui.log").permissions();
	assert((FilePermissions & (std::filesystem::perms::group_all |
							   std::filesystem::perms::others_all)) ==
		   std::filesystem::perms::none);

	std::filesystem::remove_all(TestDirectory);
	return 0;
}
