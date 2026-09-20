#include "LocalData/ContentRepository.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

namespace
{
	std::filesystem::path TempDatabase()
	{
		return std::filesystem::temp_directory_path() /
			   ("vir_content_repository_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite3");
	}

	void RemoveDatabase(const std::filesystem::path &Path)
	{
		std::error_code Ignored;
		std::filesystem::remove(Path, Ignored);
		std::filesystem::remove(Path.string() + "-wal", Ignored);
		std::filesystem::remove(Path.string() + "-shm", Ignored);
	}

	ContentRuntime::FInstalledContentRecord Record(std::string Id, std::uint64_t Revision)
	{
		ContentRuntime::FInstalledContentRecord Value;
		Value.ContentSetId = std::move(Id);
		Value.RouteId = "route.han-river.5k";
		Value.SemanticVersion = "1.0." + std::to_string(Revision);
		Value.ManifestHashHex = std::string(64, static_cast<char>('a' + Revision));
		Value.CatalogRevision = Revision;
		Value.IssuedAtUnixSeconds = 1'700'000'000;
		Value.ExpiresAtUnixSeconds = 1'700'604'800;
		Value.InstallPath = "/private/content/" + Value.ContentSetId;
		return Value;
	}

	template <typename FCallable>
	void ExpectError(ContentRuntime::EContentError Expected, FCallable &&Callable)
	{
		try
		{
			Callable();
			assert(false && "expected content error");
		}
		catch (const ContentRuntime::FContentValidationError &Error)
		{
			assert(Error.GetCode() == Expected);
		}
	}

	void TestLifecycleRetentionAndRestart()
	{
		const auto Path = TempDatabase();
		RemoveDatabase(Path);
		{
			LocalData::FContentRepository Repository(Path);
			ExpectError(ContentRuntime::EContentError::WorkoutActive, [&]
						{ Repository.SaveStaged(Record("set-a", 1), true); });
			Repository.SaveStaged(Record("set-a", 1), false);
			ExpectError(ContentRuntime::EContentError::WorkoutActive, [&]
						{ Repository.MarkVerified("set-a", true); });
			Repository.MarkVerified("set-a", false);
			Repository.ActivateVerified("set-a", false);
			assert(Repository.FindByState(ContentRuntime::EInstalledContentState::Active)->ContentSetId == "set-a");

			Repository.SaveStaged(Record("set-b", 2), false);
			Repository.MarkVerified("set-b", false);
			ExpectError(ContentRuntime::EContentError::WorkoutActive, [&]
						{ Repository.ActivateVerified("set-b", true); });
			Repository.ActivateVerified("set-b", false);
			assert(Repository.FindByState(ContentRuntime::EInstalledContentState::Active)->ContentSetId == "set-b");
			assert(Repository.FindByState(ContentRuntime::EInstalledContentState::LastKnownGood)->ContentSetId == "set-a");

			Repository.SaveStaged(Record("set-c", 3), false);
			Repository.MarkVerified("set-c", false);
			Repository.ActivateVerified("set-c", false);
			assert(Repository.FindByState(ContentRuntime::EInstalledContentState::Active)->ContentSetId == "set-c");
			assert(Repository.FindByState(ContentRuntime::EInstalledContentState::LastKnownGood)->ContentSetId == "set-b");
			assert(!Repository.Find("set-a"));
		}
		{
			LocalData::FContentRepository Repository(Path);
			assert(Repository.FindByState(ContentRuntime::EInstalledContentState::Active)->ContentSetId == "set-c");
			assert(Repository.FindByState(ContentRuntime::EInstalledContentState::LastKnownGood)->ContentSetId == "set-b");
			Repository.ApplyWithdrawal("set-c", false);
			assert(!Repository.FindByState(ContentRuntime::EInstalledContentState::Active));
			assert(Repository.Find("set-c")->State == ContentRuntime::EInstalledContentState::Withdrawn);
		}
		RemoveDatabase(Path);
	}

	void TestCatalogAndResumeState()
	{
		const auto Path = TempDatabase();
		RemoveDatabase(Path);
		LocalData::FContentRepository Repository(Path);
		assert(Repository.AcceptedCatalogRevision() == 0);
		Repository.AcceptCatalogRevision(12, std::string(64, 'a'));
		assert(Repository.AcceptedCatalogRevision() == 12);
		ExpectError(ContentRuntime::EContentError::RevisionRollback, [&]
					{ Repository.AcceptCatalogRevision(11, std::string(64, 'b')); });

		LocalData::FContentDownloadRecord Download{"set-a", "https://content.invalid/set-a/1.0.0/package.vircontent", "/private/staging/set-a", 1000, 400, "etag-1"};
		Repository.SaveDownload(Download, false);
		assert(Repository.FindDownload("set-a")->ReceivedSizeBytes == 400);
		Download.ReceivedSizeBytes = 750;
		Repository.SaveDownload(Download, false);
		assert(Repository.FindDownload("set-a")->ReceivedSizeBytes == 750);
		ExpectError(ContentRuntime::EContentError::WorkoutActive, [&]
					{ Repository.RemoveDownload("set-a", true); });
		Repository.RemoveDownload("set-a", false);
		assert(!Repository.FindDownload("set-a"));
		RemoveDatabase(Path);
	}
} // namespace

int main()
{
	TestLifecycleRetentionAndRestart();
	TestCatalogAndResumeState();
	std::cout << "content repository tests passed\n";
}
