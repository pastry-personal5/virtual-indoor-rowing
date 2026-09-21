#pragma once

#include "ContentRuntime/ContentManifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ContentRuntime
{
	struct FContentLicenseRecord
	{
		std::string LicenseName;
		std::string LicenseVersion;
		std::string LicenseUrl;
		std::string Creator;
		std::string CanonicalSourceUrl;
		FSha256 SourceSha256{};
		std::string ModificationDescription;
		std::string ShareAlikeRelease;
		bool bHasSourceSha256 = false;
	};

	struct FContentInventoryEntry
	{
		std::string RelativePath;
		std::string AssetClass;
		std::uint64_t SizeBytes = 0;
		FSha256 Sha256{};
		std::optional<FContentLicenseRecord> License;
	};

	struct FContentInventory
	{
		std::string ContentSetId;
		std::vector<FContentInventoryEntry> Entries;
	};

	struct FValidatedContentPackage
	{
		std::filesystem::path ContainerPath;
		FContentInventory Inventory;
		std::vector<std::string> ArchivePaths;
		FSha256 InventorySha256{};
		FSha256 RouteDefinitionSha256{};
		std::string PackageNotice;
	};

	FContentInventory ParseAndValidateInventory(std::string_view InventoryBytes,
												const FContentManifest &Manifest);

	FValidatedContentPackage ValidateStagedPackage(const std::filesystem::path &StagingDirectory,
												   const FContentManifest &Manifest,
												   std::string_view InventoryBytes);
	// The inventory is carried inside the package. This bounded read does not
	// trust it; callers must pass the bytes to ValidateStagedPackage.
	std::string ReadStagedPackageInventory(const std::filesystem::path &StagingDirectory);
	void ExtractValidatedPackage(const FValidatedContentPackage &Package,
								 const std::filesystem::path &DestinationDirectory);
	bool HasStorageAdmission(const std::filesystem::path &StagingDirectory, const FContentManifest &Manifest) noexcept;

	bool IsSafeRelativeContentPath(std::string_view Path) noexcept;
	bool IsAllowedAssetClass(std::string_view AssetClass) noexcept;
} // namespace ContentRuntime
