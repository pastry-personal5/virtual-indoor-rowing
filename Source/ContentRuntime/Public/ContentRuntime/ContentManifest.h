#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ContentRuntime
{
	inline constexpr std::uint32_t ContentManifestSchemaV1 = 1;
	inline constexpr std::uint32_t ContentManifestEnvelopeV1 = 1;
	inline constexpr std::uint32_t RouteDefinitionSchemaV1 = 1;
	inline constexpr std::uint64_t MaximumCompressedPackageBytes = 32ULL * 1024ULL * 1024ULL * 1024ULL;
	inline constexpr std::uint64_t MinimumFreeStorageBytes = 100ULL * 1024ULL * 1024ULL * 1024ULL;
	inline constexpr std::size_t MaximumManifestBytes = 1024 * 1024;
	inline constexpr std::size_t MaximumInventoryBytes = 4 * 1024 * 1024;
	inline constexpr std::size_t MaximumInventoryEntries = 16'384;
	inline constexpr std::size_t MaximumNoticeBytes = 1024 * 1024;

	using FSha256 = std::array<std::uint8_t, 32>;
	using FEd25519PublicKey = std::array<std::uint8_t, 32>;

	struct FTrustedContentKey
	{
		std::string KeyId;
		FEd25519PublicKey PublicKey{};
	};

	struct FClientCompatibility
	{
		std::uint32_t MinimumBuild = 0;
		std::uint32_t MaximumBuild = 0;
		std::uint32_t ContentSchema = 0;
		std::uint32_t RouteSchema = 0;

		bool Supports(std::uint32_t ClientBuild) const noexcept;
	};

	struct FRouteCheckpoint
	{
		std::string CheckpointId;
		std::uint64_t DistanceMm = 0;
	};

	struct FRouteDefinition
	{
		std::uint32_t SchemaVersion = 0;
		std::string RouteId;
		std::string SemanticVersion;
		std::string ContentSetId;
		std::uint64_t LengthMm = 0;
		bool bClosed = false;
		std::vector<FRouteCheckpoint> Checkpoints;
		FClientCompatibility Compatibility;
		std::string DisplayNameKey;
		std::string DescriptionKey;
		FSha256 MetadataSha256{};
	};

	struct FContentManifest
	{
		std::uint64_t CatalogRevision = 0;
		std::int64_t IssuedAtUnixSeconds = 0;
		std::int64_t ExpiresAtUnixSeconds = 0;
		FClientCompatibility Compatibility;
		FRouteDefinition Route;
		std::string PackageUrl;
		FSha256 PackageSha256{};
		std::uint64_t CompressedSizeBytes = 0;
		std::uint64_t UncompressedSizeBytes = 0;
		FSha256 InventorySha256{};
		FSha256 RouteDefinitionSha256{};
		bool bWithdrawn = false;
		std::string WithdrawalReasonKey;
		std::string KeyId;
		FSha256 ManifestSha256{};
		std::string CanonicalUnsignedPayload;
	};

	enum class EContentError : std::uint8_t
	{
		None,
		Malformed,
		Oversized,
		NonCanonical,
		UnknownKey,
		BadSignature,
		BadSchema,
		Incompatible,
		Expired,
		RevisionRollback,
		Withdrawn,
		InvalidRoute,
		InvalidUrl,
		InvalidHash,
		InvalidSize,
		InvalidPath,
		DisallowedAssetClass,
		MissingLicenseNotice,
		InvalidLicenseProvenance,
		InventoryMismatch,
		PackageMismatch,
		StorageInsufficient,
		WorkoutActive,
		IoFailure
	};

	class FContentValidationError final : public std::runtime_error
	{
	  public:
		FContentValidationError(EContentError Code, std::string Message);
		EContentError GetCode() const noexcept;

	  private:
		EContentError Code;
	};

	class IContentSignatureVerifier
	{
	  public:
		virtual ~IContentSignatureVerifier() = default;
		virtual bool Verify(const FEd25519PublicKey &PublicKey,
							std::span<const std::uint8_t> Payload,
							std::span<const std::uint8_t> Signature) const = 0;
	};

	struct FManifestPolicy
	{
		std::uint32_t ClientBuild = 0;
		std::int64_t NowUnixSeconds = 0;
		std::uint64_t MinimumCatalogRevision = 0;
		bool bAllowExpired = false;
		bool bAllowWithdrawn = false;
	};

	FContentManifest ParseAndVerifyManifest(std::string_view EnvelopeBytes,
											const std::vector<FTrustedContentKey> &TrustedKeys,
											const IContentSignatureVerifier &Verifier,
											const FManifestPolicy &Policy);
	FRouteDefinition BuiltInStandardRouteDefinition();
	FRouteDefinition ParseAndValidateRouteDefinition(std::string_view RouteBytes, std::uint32_t ClientBuild);

	std::string CanonicalizeManifestPayload(std::string_view UnsignedPayload);
	FSha256 Sha256(std::span<const std::uint8_t> Bytes);
	FSha256 Sha256File(const std::filesystem::path &Path, std::uint64_t MaximumBytes);
	std::string Sha256Hex(const FSha256 &Hash);
	std::optional<FSha256> ParseSha256(std::string_view Bytes);
	const char *ContentErrorName(EContentError Error) noexcept;
} // namespace ContentRuntime
