#include "ContentRuntime/ContentInventory.h"

#include "rowing/v1/content.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>

namespace ContentRuntime
{
	namespace
	{
		constexpr std::array<std::string_view, 11> AllowedAssetClasses = {
			"DataAsset", "Font", "LevelSequence", "LicenseNotice", "Material", "MaterialInstance", "SoundWave", "StaticMesh", "Texture2D", "World", "WorldPartitionRuntimeCell"};

		std::string DeterministicSerialize(const rowing::v1::ContentInventoryV1 &Inventory)
		{
			std::string Bytes;
			Bytes.resize(Inventory.ByteSizeLong());
			google::protobuf::io::ArrayOutputStream Array(Bytes.data(), static_cast<int>(Bytes.size()));
			google::protobuf::io::CodedOutputStream Coded(&Array);
			Coded.SetSerializationDeterministic(true);
			if (!Inventory.SerializeToCodedStream(&Coded) || Coded.HadError())
				throw FContentValidationError(EContentError::Malformed, "inventory serialization failed");
			Bytes.resize(static_cast<std::size_t>(Coded.ByteCount()));
			return Bytes;
		}

		bool IsIdentifier(std::string_view Value)
		{
			return !Value.empty() && Value.size() <= 128 &&
				   std::all_of(Value.begin(), Value.end(), [](unsigned char Character)
							   { return std::isalnum(Character) != 0 || Character == '.' || Character == '-' || Character == '_'; });
		}

		std::uint16_t ReadU16(std::istream &Input)
		{
			std::array<std::uint8_t, 2> Bytes{};
			Input.read(reinterpret_cast<char *>(Bytes.data()), Bytes.size());
			if (!Input)
				throw FContentValidationError(EContentError::Malformed, "truncated content archive");
			return static_cast<std::uint16_t>(Bytes[0]) | static_cast<std::uint16_t>(Bytes[1] << 8U);
		}

		std::uint32_t ReadU32(std::istream &Input)
		{
			std::array<std::uint8_t, 4> Bytes{};
			Input.read(reinterpret_cast<char *>(Bytes.data()), Bytes.size());
			if (!Input)
				throw FContentValidationError(EContentError::Malformed, "truncated content archive");
			return static_cast<std::uint32_t>(Bytes[0]) | (static_cast<std::uint32_t>(Bytes[1]) << 8U) |
				   (static_cast<std::uint32_t>(Bytes[2]) << 16U) | (static_cast<std::uint32_t>(Bytes[3]) << 24U);
		}

		std::uint64_t ReadU64(std::istream &Input)
		{
			std::array<std::uint8_t, 8> Bytes{};
			Input.read(reinterpret_cast<char *>(Bytes.data()), Bytes.size());
			if (!Input)
				throw FContentValidationError(EContentError::Malformed, "truncated content archive");
			std::uint64_t Value = 0;
			for (unsigned Index = 0; Index < 8; ++Index)
				Value |= static_cast<std::uint64_t>(Bytes[Index]) << (Index * 8U);
			return Value;
		}

		bool IsAllowedArchivePath(std::string_view Path)
		{
			if (!IsSafeRelativeContentPath(Path))
				return false;
			constexpr std::array<std::string_view, 6> Extensions = {".pak", ".utoc", ".ucas", ".pb", ".json", ".txt"};
			return std::any_of(Extensions.begin(), Extensions.end(), [&](std::string_view Extension)
							   { return Path.ends_with(Extension); });
		}

		struct FArchiveEntry
		{
			std::string Path;
			std::uint64_t Size = 0;
			FSha256 Hash{};
			std::streamoff DataOffset = 0;
		};

		std::vector<FArchiveEntry> ReadArchiveEntries(const std::filesystem::path &Path, std::uint64_t MaximumUncompressedBytes)
		{
			std::ifstream Input(Path, std::ios::binary);
			if (!Input)
				throw FContentValidationError(EContentError::IoFailure, "cannot open content archive");
			std::array<char, 8> Magic{};
			Input.read(Magic.data(), Magic.size());
			if (!Input || std::string_view(Magic.data(), Magic.size()) != "VIRCNT1\n")
				throw FContentValidationError(EContentError::Malformed, "content archive magic is invalid");
			const std::uint32_t Count = ReadU32(Input);
			if (Count == 0 || Count > 64)
				throw FContentValidationError(EContentError::Oversized, "content archive entry count is invalid");
			std::vector<FArchiveEntry> Entries;
			std::set<std::string> Seen;
			std::uint64_t Total = 0;
			for (std::uint32_t Index = 0; Index < Count; ++Index)
			{
				const std::uint16_t PathLength = ReadU16(Input);
				const std::uint64_t Size = ReadU64(Input);
				FSha256 Hash{};
				Input.read(reinterpret_cast<char *>(Hash.data()), Hash.size());
				if (!Input || PathLength == 0 || PathLength > 512 || Size == 0 || Size > MaximumUncompressedBytes || Total > MaximumUncompressedBytes - Size)
					throw FContentValidationError(EContentError::Oversized, "content archive entry bounds are invalid");
				std::string EntryPath(PathLength, '\0');
				Input.read(EntryPath.data(), PathLength);
				if (!Input || !IsAllowedArchivePath(EntryPath) || !Seen.insert(EntryPath).second)
					throw FContentValidationError(EContentError::InvalidPath, "content archive path is unsafe, duplicated, or disallowed");
				const auto Offset = Input.tellg();
				if (Offset < 0 || Size > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
					throw FContentValidationError(EContentError::Oversized, "content archive offset is invalid");
				Input.seekg(static_cast<std::streamoff>(Size), std::ios::cur);
				if (!Input)
					throw FContentValidationError(EContentError::Malformed, "content archive entry is truncated");
				Entries.push_back({std::move(EntryPath), Size, Hash, Offset});
				Total += Size;
			}
			if (Input.peek() != std::char_traits<char>::eof())
				throw FContentValidationError(EContentError::Malformed, "content archive contains trailing bytes");
			return Entries;
		}

		void CopyArchiveEntry(std::ifstream &Input, const FArchiveEntry &Entry, const std::filesystem::path &Destination)
		{
			Input.clear();
			Input.seekg(Entry.DataOffset);
			std::filesystem::create_directories(Destination.parent_path());
			std::ofstream Output(Destination, std::ios::binary | std::ios::trunc);
			if (!Input || !Output)
				throw FContentValidationError(EContentError::IoFailure, "cannot create extracted content file");
			std::array<char, 1024 * 1024> Buffer{};
			std::uint64_t Remaining = Entry.Size;
			while (Remaining > 0)
			{
				const auto Count = static_cast<std::streamsize>(std::min<std::uint64_t>(Remaining, Buffer.size()));
				Input.read(Buffer.data(), Count);
				if (Input.gcount() != Count)
					throw FContentValidationError(EContentError::Malformed, "content archive changed during extraction");
				Output.write(Buffer.data(), Count);
				if (!Output)
					throw FContentValidationError(EContentError::IoFailure, "cannot write extracted content file");
				Remaining -= static_cast<std::uint64_t>(Count);
			}
			Output.close();
			if (Sha256File(Destination, Entry.Size) != Entry.Hash)
				throw FContentValidationError(EContentError::PackageMismatch, "content archive entry digest is invalid");
		}

		std::string ReadArchiveEntry(std::ifstream &Input, const FArchiveEntry &Entry, std::uint64_t MaximumBytes)
		{
			if (Entry.Size > MaximumBytes)
				throw FContentValidationError(EContentError::Oversized, "content archive notice is too large");
			Input.clear();
			Input.seekg(Entry.DataOffset);
			std::string Bytes(static_cast<std::size_t>(Entry.Size), '\0');
			Input.read(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
			if (!Input || Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size())) != Entry.Hash)
				throw FContentValidationError(EContentError::PackageMismatch, "content archive notice digest is invalid");
			return Bytes;
		}

		bool ContainsAll(std::string_view Text, std::initializer_list<std::string_view> Needles)
		{
			return std::all_of(Needles.begin(), Needles.end(), [&](std::string_view Needle)
							   { return !Needle.empty() && Text.find(Needle) != std::string_view::npos; });
		}

		bool IsStructuralArchivePath(std::string_view Path)
		{
			return Path == "inventory.pb" || Path == "route.pb" || Path.ends_with(".pak") || Path.ends_with(".utoc") || Path.ends_with(".ucas");
		}
	} // namespace

	bool IsSafeRelativeContentPath(std::string_view Path) noexcept
	{
		if (Path.empty() || Path.size() > 512 || Path.front() == '/' || Path.back() == '/' ||
			Path.find('\\') != std::string_view::npos || Path.find("//") != std::string_view::npos)
			return false;
		std::size_t Start = 0;
		while (Start < Path.size())
		{
			const std::size_t End = Path.find('/', Start);
			const std::string_view Segment = Path.substr(Start, End == std::string_view::npos ? Path.size() - Start : End - Start);
			if (Segment.empty() || Segment == "." || Segment == ".." ||
				!std::all_of(Segment.begin(), Segment.end(), [](unsigned char Character)
							 { return std::isalnum(Character) != 0 || Character == '.' || Character == '-' || Character == '_'; }))
				return false;
			if (End == std::string_view::npos)
				break;
			Start = End + 1;
		}
		return true;
	}

	bool IsAllowedAssetClass(std::string_view AssetClass) noexcept
	{
		return std::find(AllowedAssetClasses.begin(), AllowedAssetClasses.end(), AssetClass) != AllowedAssetClasses.end();
	}

	FContentInventory ParseAndValidateInventory(std::string_view InventoryBytes,
												const FContentManifest &Manifest)
	{
		if (InventoryBytes.empty() || InventoryBytes.size() > MaximumInventoryBytes)
			throw FContentValidationError(EContentError::Oversized, "inventory has an invalid size");
		if (Sha256(std::span(reinterpret_cast<const std::uint8_t *>(InventoryBytes.data()), InventoryBytes.size())) != Manifest.InventorySha256)
			throw FContentValidationError(EContentError::InventoryMismatch, "inventory digest does not match the signed manifest");
		rowing::v1::ContentInventoryV1 Wire;
		if (!Wire.ParseFromArray(InventoryBytes.data(), static_cast<int>(InventoryBytes.size())) || !Wire.IsInitialized())
			throw FContentValidationError(EContentError::Malformed, "inventory is malformed");
		if (DeterministicSerialize(Wire) != InventoryBytes)
			throw FContentValidationError(EContentError::NonCanonical, "inventory is not canonical");
		if (Wire.schema_version() != 1 || Wire.content_set_id() != Manifest.Route.ContentSetId || !IsIdentifier(Wire.content_set_id()))
			throw FContentValidationError(EContentError::BadSchema, "inventory schema or content set is invalid");
		if (Wire.entries_size() == 0 || static_cast<std::size_t>(Wire.entries_size()) > MaximumInventoryEntries)
			throw FContentValidationError(EContentError::Oversized, "inventory entry count is invalid");

		FContentInventory Inventory;
		Inventory.ContentSetId = Wire.content_set_id();
		std::set<std::string> SeenPaths;
		std::uint64_t TotalBytes = 0;
		for (const auto &Entry : Wire.entries())
		{
			if (!IsSafeRelativeContentPath(Entry.relative_path()) || !SeenPaths.insert(Entry.relative_path()).second)
				throw FContentValidationError(EContentError::InvalidPath, "inventory path is unsafe or duplicated");
			if (!IsAllowedAssetClass(Entry.asset_class()))
				throw FContentValidationError(EContentError::DisallowedAssetClass, "inventory contains a disallowed asset class");
			const auto Hash = ParseSha256(Entry.sha256());
			if (!Hash || Entry.size_bytes() == 0 || Entry.size_bytes() > Manifest.UncompressedSizeBytes ||
				TotalBytes > Manifest.UncompressedSizeBytes - Entry.size_bytes())
				throw FContentValidationError(EContentError::InvalidSize, "inventory entry size or hash is invalid");
			TotalBytes += Entry.size_bytes();
			std::optional<FContentLicenseRecord> License;
			if (Entry.has_license())
			{
				const auto &WireLicense = Entry.license();
				FContentLicenseRecord ParsedLicense;
				ParsedLicense.LicenseName = WireLicense.license_name();
				ParsedLicense.LicenseVersion = WireLicense.license_version();
				ParsedLicense.LicenseUrl = WireLicense.license_url();
				ParsedLicense.Creator = WireLicense.creator();
				ParsedLicense.CanonicalSourceUrl = WireLicense.canonical_source_url();
				ParsedLicense.ModificationDescription = WireLicense.modification_description();
				ParsedLicense.ShareAlikeRelease = WireLicense.share_alike_release();
				if (const auto SourceHash = ParseSha256(WireLicense.source_sha256()))
				{
					ParsedLicense.SourceSha256 = *SourceHash;
					ParsedLicense.bHasSourceSha256 = true;
				}
				const bool bCcBySa = ParsedLicense.LicenseName.starts_with("CC BY-SA");
				if (bCcBySa && (!ParsedLicense.LicenseName.starts_with("CC BY-SA") || ParsedLicense.LicenseVersion.empty() ||
								ParsedLicense.LicenseUrl.empty() || ParsedLicense.Creator.empty() || ParsedLicense.CanonicalSourceUrl.empty() ||
								!ParsedLicense.bHasSourceSha256 || ParsedLicense.ModificationDescription.empty() || ParsedLicense.ShareAlikeRelease.empty()))
					throw FContentValidationError(EContentError::InvalidLicenseProvenance, "CC BY-SA provenance is incomplete");
				License = std::move(ParsedLicense);
			}
			Inventory.Entries.push_back({Entry.relative_path(), Entry.asset_class(), Entry.size_bytes(), *Hash, std::move(License)});
		}
		if (TotalBytes > Manifest.UncompressedSizeBytes)
			throw FContentValidationError(EContentError::InvalidSize, "inventory exceeds the declared uncompressed package size");
		const auto NoticeCount = std::count_if(Inventory.Entries.begin(), Inventory.Entries.end(), [](const FContentInventoryEntry &Entry)
											   { return Entry.RelativePath == "licenses/NOTICE.txt"; });
		if (NoticeCount != 1 || std::count_if(Inventory.Entries.begin(), Inventory.Entries.end(), [](const FContentInventoryEntry &Entry)
											  { return Entry.AssetClass == "LicenseNotice"; }) != 1)
			throw FContentValidationError(EContentError::MissingLicenseNotice, "inventory must contain exactly one licenses/NOTICE.txt LicenseNotice");
		if (std::any_of(Inventory.Entries.begin(), Inventory.Entries.end(), [](const FContentInventoryEntry &Entry)
						{ return Entry.License && Entry.License->LicenseName.starts_with("CC BY-SA"); }) &&
			std::none_of(Inventory.Entries.begin(), Inventory.Entries.end(), [](const FContentInventoryEntry &Entry)
						 { return Entry.RelativePath == "licenses/NOTICE.txt"; }))
			throw FContentValidationError(EContentError::MissingLicenseNotice, "CC BY-SA provenance requires licenses/NOTICE.txt");
		return Inventory;
	}

	FValidatedContentPackage ValidateStagedPackage(const std::filesystem::path &StagingDirectory,
												   const FContentManifest &Manifest,
												   std::string_view InventoryBytes)
	{
		const std::filesystem::path PackagePath = StagingDirectory / "package.vircontent";
		std::error_code Error;
		if (!std::filesystem::is_regular_file(PackagePath, Error) || Error)
			throw FContentValidationError(EContentError::IoFailure, "staged package is missing");
		const std::uint64_t Size = std::filesystem::file_size(PackagePath, Error);
		if (Error || Size != Manifest.CompressedSizeBytes)
			throw FContentValidationError(EContentError::InvalidSize, "staged package size does not match the signed manifest");
		if (Sha256File(PackagePath, Manifest.CompressedSizeBytes) != Manifest.PackageSha256)
			throw FContentValidationError(EContentError::PackageMismatch, "staged package digest does not match the signed manifest");
		const auto ArchiveEntries = ReadArchiveEntries(PackagePath, Manifest.UncompressedSizeBytes);
		std::vector<std::string> Paths;
		bool bHasUtoc = false;
		bool bHasUcas = false;
		bool bHasPak = false;
		bool bHasInventory = false;
		bool bHasRoute = false;
		bool bHasNotice = false;
		for (const auto &Entry : ArchiveEntries)
		{
			Paths.push_back(Entry.Path);
			bHasUtoc = bHasUtoc || std::string_view(Entry.Path).ends_with(".utoc");
			bHasUcas = bHasUcas || std::string_view(Entry.Path).ends_with(".ucas");
			bHasPak = bHasPak || std::string_view(Entry.Path).ends_with(".pak");
			bHasInventory = bHasInventory || Entry.Path == "inventory.pb";
			bHasRoute = bHasRoute || Entry.Path == "route.pb";
			bHasNotice = bHasNotice || Entry.Path == "licenses/NOTICE.txt";
		}
		if (!bHasPak || !bHasUtoc || !bHasUcas || !bHasInventory || !bHasRoute || !bHasNotice)
			throw FContentValidationError(EContentError::MissingLicenseNotice, "content archive must contain IoStore, inventory, route, and license notice entries");
		FContentInventory Inventory = ParseAndValidateInventory(InventoryBytes, Manifest);
		std::ifstream Input(PackagePath, std::ios::binary);
		std::string PackageNotice;
		std::set<std::string> InventoryPaths;
		for (const auto &Entry : Inventory.Entries)
		{
			InventoryPaths.insert(Entry.RelativePath);
			const auto Match = std::find_if(ArchiveEntries.begin(), ArchiveEntries.end(), [&](const FArchiveEntry &ArchiveEntry)
											{ return ArchiveEntry.Path == Entry.RelativePath; });
			if (Match == ArchiveEntries.end() || Match->Size != Entry.SizeBytes || Match->Hash != Entry.Sha256)
				throw FContentValidationError(EContentError::InventoryMismatch, "archive entry does not match signed inventory");
			if (Entry.RelativePath == "licenses/NOTICE.txt")
				PackageNotice = ReadArchiveEntry(Input, *Match, MaximumNoticeBytes);
		}
		for (const auto &ArchiveEntry : ArchiveEntries)
			if (!IsStructuralArchivePath(ArchiveEntry.Path) && !InventoryPaths.contains(ArchiveEntry.Path))
				throw FContentValidationError(EContentError::InventoryMismatch, "archive contains undeclared inventory entries");
		if (PackageNotice.empty())
			throw FContentValidationError(EContentError::InventoryMismatch, "archive is missing the package notice");
		for (const auto &Entry : Inventory.Entries)
		{
			if (Entry.License && Entry.License->LicenseName.starts_with("CC BY-SA"))
			{
				const auto &License = *Entry.License;
				if (!ContainsAll(PackageNotice, {License.LicenseName, License.LicenseVersion, License.LicenseUrl, License.Creator, License.CanonicalSourceUrl, License.ModificationDescription, License.ShareAlikeRelease}))
					throw FContentValidationError(EContentError::InvalidLicenseProvenance, "license notice omits CC BY-SA attribution or share-alike terms");
			}
		}
		return {PackagePath, std::move(Inventory), std::move(Paths), Manifest.InventorySha256, Manifest.RouteDefinitionSha256, std::move(PackageNotice)};
	}

	void ExtractValidatedPackage(const FValidatedContentPackage &Package,
								 const std::filesystem::path &DestinationDirectory)
	{
		const auto Entries = ReadArchiveEntries(Package.ContainerPath, MaximumCompressedPackageBytes * 4ULL);
		const std::filesystem::path Temporary = DestinationDirectory.string() + ".installing";
		std::error_code Error;
		std::filesystem::remove_all(Temporary, Error);
		std::filesystem::create_directories(Temporary);
		try
		{
			std::ifstream Input(Package.ContainerPath, std::ios::binary);
			for (const auto &Entry : Entries)
				CopyArchiveEntry(Input, Entry, Temporary / Entry.Path);
			if (Sha256File(Temporary / "inventory.pb", MaximumInventoryBytes) != Package.InventorySha256)
				throw FContentValidationError(EContentError::InventoryMismatch, "extracted inventory differs from the signed inventory");
			if (Sha256File(Temporary / "route.pb", MaximumManifestBytes) != Package.RouteDefinitionSha256)
				throw FContentValidationError(EContentError::PackageMismatch, "extracted route definition differs from the signed route hash");
			std::filesystem::remove_all(DestinationDirectory, Error);
			Error.clear();
			std::filesystem::rename(Temporary, DestinationDirectory, Error);
			if (Error)
				throw FContentValidationError(EContentError::IoFailure, "cannot atomically activate extracted content");
		}
		catch (...)
		{
			std::filesystem::remove_all(Temporary, Error);
			throw;
		}
	}

	bool HasStorageAdmission(const std::filesystem::path &StagingDirectory, const FContentManifest &Manifest) noexcept
	{
		std::error_code Error;
		const auto Space = std::filesystem::space(StagingDirectory, Error);
		if (Error)
			return false;
		// The policy is intentionally independent of a transfer's current size:
		// a new staged replacement must retain 100 GiB of free headroom alongside
		// the active and last-known-good sets.
		return Space.available >= MinimumFreeStorageBytes &&
			   Space.available - MinimumFreeStorageBytes >= Manifest.CompressedSizeBytes;
	}
} // namespace ContentRuntime
