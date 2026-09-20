#pragma once

#include "ContentRuntime/ContentState.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LocalData
{
	struct FContentDownloadRecord
	{
		std::string ContentSetId;
		std::string PackageUrl;
		std::string StagingPath;
		std::uint64_t ExpectedSizeBytes = 0;
		std::uint64_t ReceivedSizeBytes = 0;
		std::string EntityTag;
	};

	class FContentRepository
	{
	  public:
		explicit FContentRepository(std::filesystem::path DatabasePath);
		~FContentRepository();

		FContentRepository(const FContentRepository &) = delete;
		FContentRepository &operator=(const FContentRepository &) = delete;

		void SaveStaged(const ContentRuntime::FInstalledContentRecord &Record, bool bWorkoutActive);
		void MarkVerified(std::string_view ContentSetId, bool bWorkoutActive);
		void ActivateVerified(std::string_view ContentSetId, bool bWorkoutActive);
		void MarkFailed(std::string_view ContentSetId, std::string FailureCategory, bool bWorkoutActive);
		void ApplyWithdrawal(std::string_view ContentSetId, bool bWorkoutActive);

		std::optional<ContentRuntime::FInstalledContentRecord> Find(std::string_view ContentSetId) const;
		std::optional<ContentRuntime::FInstalledContentRecord> FindByState(ContentRuntime::EInstalledContentState State) const;
		std::vector<ContentRuntime::FInstalledContentRecord> ListRetained() const;

		std::uint64_t AcceptedCatalogRevision() const;
		void AcceptCatalogRevision(std::uint64_t Revision, std::string ManifestHashHex);

		void SaveDownload(const FContentDownloadRecord &Record, bool bWorkoutActive);
		std::optional<FContentDownloadRecord> FindDownload(std::string_view ContentSetId) const;
		void RemoveDownload(std::string_view ContentSetId, bool bWorkoutActive);

	  private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};
} // namespace LocalData
