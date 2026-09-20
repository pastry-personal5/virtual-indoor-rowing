#include "ContentRuntime/ContentManifest.h"

#include "rowing/v1/content.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace ContentRuntime
{
	namespace
	{
		constexpr std::array<std::uint32_t, 64> ShaConstants = {
			0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

		constexpr std::array<std::uint32_t, 8> ShaInitial = {
			0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

		std::uint32_t RotateRight(std::uint32_t Value, unsigned Count)
		{
			return (Value >> Count) | (Value << (32U - Count));
		}

		class FSha256Builder
		{
		  public:
			void Update(std::span<const std::uint8_t> Bytes)
			{
				TotalBytes += Bytes.size();
				while (!Bytes.empty())
				{
					const std::size_t Count = std::min(Bytes.size(), Block.size() - BlockBytes);
					std::memcpy(Block.data() + BlockBytes, Bytes.data(), Count);
					BlockBytes += Count;
					Bytes = Bytes.subspan(Count);
					if (BlockBytes == Block.size())
					{
						Transform();
						BlockBytes = 0;
					}
				}
			}

			FSha256 Finish()
			{
				const std::uint64_t BitCount = TotalBytes * 8ULL;
				Block[BlockBytes++] = 0x80;
				if (BlockBytes > 56)
				{
					std::fill(Block.begin() + static_cast<std::ptrdiff_t>(BlockBytes), Block.end(), 0);
					Transform();
					BlockBytes = 0;
				}
				std::fill(Block.begin() + static_cast<std::ptrdiff_t>(BlockBytes), Block.begin() + 56, 0);
				for (unsigned Index = 0; Index < 8; ++Index)
					Block[63 - Index] = static_cast<std::uint8_t>(BitCount >> (Index * 8U));
				Transform();
				FSha256 Result{};
				for (std::size_t Word = 0; Word < State.size(); ++Word)
				{
					for (unsigned Byte = 0; Byte < 4; ++Byte)
						Result[Word * 4 + Byte] = static_cast<std::uint8_t>(State[Word] >> (24U - Byte * 8U));
				}
				return Result;
			}

		  private:
			void Transform()
			{
				std::array<std::uint32_t, 64> Words{};
				for (std::size_t Index = 0; Index < 16; ++Index)
				{
					const std::size_t Offset = Index * 4;
					Words[Index] = (static_cast<std::uint32_t>(Block[Offset]) << 24U) |
								   (static_cast<std::uint32_t>(Block[Offset + 1]) << 16U) |
								   (static_cast<std::uint32_t>(Block[Offset + 2]) << 8U) |
								   static_cast<std::uint32_t>(Block[Offset + 3]);
				}
				for (std::size_t Index = 16; Index < Words.size(); ++Index)
				{
					const std::uint32_t S0 = RotateRight(Words[Index - 15], 7) ^ RotateRight(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3U);
					const std::uint32_t S1 = RotateRight(Words[Index - 2], 17) ^ RotateRight(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10U);
					Words[Index] = Words[Index - 16] + S0 + Words[Index - 7] + S1;
				}
				auto Working = State;
				for (std::size_t Index = 0; Index < Words.size(); ++Index)
				{
					const std::uint32_t S1 = RotateRight(Working[4], 6) ^ RotateRight(Working[4], 11) ^ RotateRight(Working[4], 25);
					const std::uint32_t Choice = (Working[4] & Working[5]) ^ (~Working[4] & Working[6]);
					const std::uint32_t Temp1 = Working[7] + S1 + Choice + ShaConstants[Index] + Words[Index];
					const std::uint32_t S0 = RotateRight(Working[0], 2) ^ RotateRight(Working[0], 13) ^ RotateRight(Working[0], 22);
					const std::uint32_t Majority = (Working[0] & Working[1]) ^ (Working[0] & Working[2]) ^ (Working[1] & Working[2]);
					const std::uint32_t Temp2 = S0 + Majority;
					Working[7] = Working[6];
					Working[6] = Working[5];
					Working[5] = Working[4];
					Working[4] = Working[3] + Temp1;
					Working[3] = Working[2];
					Working[2] = Working[1];
					Working[1] = Working[0];
					Working[0] = Temp1 + Temp2;
				}
				for (std::size_t Index = 0; Index < State.size(); ++Index)
					State[Index] += Working[Index];
			}

			std::array<std::uint32_t, 8> State = ShaInitial;
			std::array<std::uint8_t, 64> Block{};
			std::size_t BlockBytes = 0;
			std::uint64_t TotalBytes = 0;
		};

		template <typename TMessage>
		std::string DeterministicSerialize(const TMessage &Message)
		{
			std::string Bytes;
			Bytes.resize(Message.ByteSizeLong());
			google::protobuf::io::ArrayOutputStream Array(Bytes.data(), static_cast<int>(Bytes.size()));
			google::protobuf::io::CodedOutputStream Coded(&Array);
			Coded.SetSerializationDeterministic(true);
			if (!Message.SerializeToCodedStream(&Coded) || Coded.HadError())
				throw FContentValidationError(EContentError::Malformed, "manifest serialization failed");
			Bytes.resize(static_cast<std::size_t>(Coded.ByteCount()));
			return Bytes;
		}

		bool IsIdentifier(std::string_view Value, std::size_t Maximum)
		{
			if (Value.empty() || Value.size() > Maximum)
				return false;
			return std::all_of(Value.begin(), Value.end(), [](unsigned char Character)
							   { return std::isalnum(Character) != 0 || Character == '.' || Character == '-' || Character == '_'; });
		}

		bool IsLocalizationKey(std::string_view Value)
		{
			return IsIdentifier(Value, 128);
		}

		FSha256 RequireHash(std::string_view Bytes, std::string_view Name)
		{
			const auto Hash = ParseSha256(Bytes);
			if (!Hash)
				throw FContentValidationError(EContentError::InvalidHash, std::string(Name) + " must be 32 bytes");
			return *Hash;
		}

		FClientCompatibility ConvertCompatibility(const rowing::v1::ClientCompatibilityV1 &Wire)
		{
			return {Wire.minimum_build(), Wire.maximum_build(), Wire.content_schema(), Wire.route_schema()};
		}

		void ValidateCompatibility(const FClientCompatibility &Compatibility, std::uint32_t ClientBuild)
		{
			if (Compatibility.MinimumBuild == 0 || Compatibility.MaximumBuild < Compatibility.MinimumBuild ||
				Compatibility.ContentSchema != ContentManifestSchemaV1 || Compatibility.RouteSchema != RouteDefinitionSchemaV1)
				throw FContentValidationError(EContentError::BadSchema, "invalid compatibility range or schema");
			if (!Compatibility.Supports(ClientBuild))
				throw FContentValidationError(EContentError::Incompatible, "manifest is incompatible with this client build");
		}

		FRouteDefinition ConvertRoute(const rowing::v1::RouteDefinitionV1 &Wire, std::uint32_t ClientBuild)
		{
			FRouteDefinition Route;
			Route.SchemaVersion = Wire.schema_version();
			Route.RouteId = Wire.route_id();
			Route.SemanticVersion = Wire.semantic_version();
			Route.ContentSetId = Wire.content_set_id();
			Route.LengthMm = Wire.length_mm();
			Route.bClosed = Wire.is_closed();
			Route.Compatibility = ConvertCompatibility(Wire.compatibility());
			Route.DisplayNameKey = Wire.display_name_key();
			Route.DescriptionKey = Wire.description_key();
			Route.MetadataSha256 = RequireHash(Wire.metadata_sha256(), "route metadata hash");
			if (Route.SchemaVersion != RouteDefinitionSchemaV1 || !IsIdentifier(Route.RouteId, 128) ||
				!IsIdentifier(Route.SemanticVersion, 64) || !IsIdentifier(Route.ContentSetId, 128) ||
				Route.LengthMm == 0 || Route.LengthMm > 1'000'000'000ULL ||
				!IsLocalizationKey(Route.DisplayNameKey) || !IsLocalizationKey(Route.DescriptionKey))
				throw FContentValidationError(EContentError::InvalidRoute, "route definition fields are invalid");
			ValidateCompatibility(Route.Compatibility, ClientBuild);
			if (Wire.checkpoints_size() > 1024)
				throw FContentValidationError(EContentError::Oversized, "too many route checkpoints");
			std::uint64_t PreviousDistance = 0;
			for (const auto &Checkpoint : Wire.checkpoints())
			{
				if (!IsIdentifier(Checkpoint.checkpoint_id(), 128) || Checkpoint.distance_mm() <= PreviousDistance || Checkpoint.distance_mm() >= Route.LengthMm)
					throw FContentValidationError(EContentError::InvalidRoute, "route checkpoints must be named, ordered, and inside the route");
				Route.Checkpoints.push_back({Checkpoint.checkpoint_id(), Checkpoint.distance_mm()});
				PreviousDistance = Checkpoint.distance_mm();
			}
			rowing::v1::RouteDefinitionV1 Hashable = Wire;
			Hashable.clear_metadata_sha256();
			const std::string HashableBytes = DeterministicSerialize(Hashable);
			if (Sha256(std::span(reinterpret_cast<const std::uint8_t *>(HashableBytes.data()), HashableBytes.size())) != Route.MetadataSha256)
				throw FContentValidationError(EContentError::InvalidHash, "route metadata hash does not match its canonical definition");
			return Route;
		}

		bool IsValidHttpsUrl(std::string_view Url, const FRouteDefinition &Route)
		{
			if (!Url.starts_with("https://") || Url.size() > 2048 || Url.find('\\') != std::string_view::npos ||
				Url.find('#') != std::string_view::npos || Url.find('?') != std::string_view::npos || Url.find("..") != std::string_view::npos)
				return false;
			const std::string_view Rest = Url.substr(8);
			const std::size_t Slash = Rest.find('/');
			if (Slash == 0 || Slash == std::string_view::npos || Rest.substr(0, Slash).find('@') != std::string_view::npos)
				return false;
			return Url.find(Route.ContentSetId) != std::string_view::npos && Url.find(Route.SemanticVersion) != std::string_view::npos;
		}
	} // namespace

	bool FClientCompatibility::Supports(std::uint32_t ClientBuild) const noexcept
	{
		return ClientBuild >= MinimumBuild && ClientBuild <= MaximumBuild;
	}

	FContentValidationError::FContentValidationError(EContentError InCode, std::string Message)
		: std::runtime_error(std::move(Message)), Code(InCode)
	{
	}

	EContentError FContentValidationError::GetCode() const noexcept
	{
		return Code;
	}

	std::string CanonicalizeManifestPayload(std::string_view UnsignedPayload)
	{
		if (UnsignedPayload.empty() || UnsignedPayload.size() > MaximumManifestBytes)
			throw FContentValidationError(EContentError::Oversized, "unsigned manifest payload has an invalid size");
		rowing::v1::ContentManifestUnsignedV1 Wire;
		if (!Wire.ParseFromArray(UnsignedPayload.data(), static_cast<int>(UnsignedPayload.size())) || !Wire.IsInitialized())
			throw FContentValidationError(EContentError::Malformed, "unsigned manifest payload is malformed");
		return DeterministicSerialize(Wire);
	}

	FContentManifest ParseAndVerifyManifest(std::string_view EnvelopeBytes,
											const std::vector<FTrustedContentKey> &TrustedKeys,
											const IContentSignatureVerifier &Verifier,
											const FManifestPolicy &Policy)
	{
		if (EnvelopeBytes.empty() || EnvelopeBytes.size() > MaximumManifestBytes)
			throw FContentValidationError(EContentError::Oversized, "manifest envelope has an invalid size");
		rowing::v1::ContentManifestEnvelopeV1 Envelope;
		if (!Envelope.ParseFromArray(EnvelopeBytes.data(), static_cast<int>(EnvelopeBytes.size())) || !Envelope.IsInitialized())
			throw FContentValidationError(EContentError::Malformed, "manifest envelope is malformed");
		if (Envelope.envelope_version() != ContentManifestEnvelopeV1 || Envelope.signature().size() != 64 || !IsIdentifier(Envelope.key_id(), 64))
			throw FContentValidationError(EContentError::BadSchema, "manifest envelope version, key, or signature length is invalid");
		const std::string CanonicalPayload = CanonicalizeManifestPayload(Envelope.unsigned_payload());
		if (CanonicalPayload != Envelope.unsigned_payload())
			throw FContentValidationError(EContentError::NonCanonical, "manifest payload is not canonical");
		const auto Key = std::find_if(TrustedKeys.begin(), TrustedKeys.end(), [&](const FTrustedContentKey &Candidate)
									  { return Candidate.KeyId == Envelope.key_id(); });
		if (Key == TrustedKeys.end())
			throw FContentValidationError(EContentError::UnknownKey, "manifest key is not trusted");
		const auto PayloadSpan = std::span(reinterpret_cast<const std::uint8_t *>(CanonicalPayload.data()), CanonicalPayload.size());
		const auto SignatureSpan = std::span(reinterpret_cast<const std::uint8_t *>(Envelope.signature().data()), Envelope.signature().size());
		if (!Verifier.Verify(Key->PublicKey, PayloadSpan, SignatureSpan))
			throw FContentValidationError(EContentError::BadSignature, "manifest signature verification failed");

		rowing::v1::ContentManifestUnsignedV1 Wire;
		if (!Wire.ParseFromString(CanonicalPayload))
			throw FContentValidationError(EContentError::Malformed, "verified manifest could not be parsed");
		if (Wire.schema_version() != ContentManifestSchemaV1 || Wire.catalog_revision() == 0)
			throw FContentValidationError(EContentError::BadSchema, "manifest schema or revision is invalid");
		if (Wire.catalog_revision() < Policy.MinimumCatalogRevision)
			throw FContentValidationError(EContentError::RevisionRollback, "catalog revision is older than the accepted revision");
		if (Wire.issued_at_unix_seconds() <= 0 || Wire.expires_at_unix_seconds() <= Wire.issued_at_unix_seconds() ||
			Wire.expires_at_unix_seconds() - Wire.issued_at_unix_seconds() > 7 * 24 * 60 * 60)
			throw FContentValidationError(EContentError::Expired, "manifest validity window is invalid");
		if (!Policy.bAllowExpired && Policy.NowUnixSeconds >= Wire.expires_at_unix_seconds())
			throw FContentValidationError(EContentError::Expired, "manifest has expired");
		if (Wire.withdrawn() && !Policy.bAllowWithdrawn)
			throw FContentValidationError(EContentError::Withdrawn, "route is withdrawn");

		FContentManifest Manifest;
		Manifest.CatalogRevision = Wire.catalog_revision();
		Manifest.IssuedAtUnixSeconds = Wire.issued_at_unix_seconds();
		Manifest.ExpiresAtUnixSeconds = Wire.expires_at_unix_seconds();
		Manifest.Compatibility = ConvertCompatibility(Wire.compatibility());
		ValidateCompatibility(Manifest.Compatibility, Policy.ClientBuild);
		Manifest.Route = ConvertRoute(Wire.route(), Policy.ClientBuild);
		Manifest.PackageUrl = Wire.package_url();
		if (!IsValidHttpsUrl(Manifest.PackageUrl, Manifest.Route))
			throw FContentValidationError(EContentError::InvalidUrl, "package URL is not an immutable HTTPS route URL");
		Manifest.PackageSha256 = RequireHash(Wire.package_sha256(), "package hash");
		Manifest.InventorySha256 = RequireHash(Wire.inventory_sha256(), "inventory hash");
		const std::string CanonicalRoute = DeterministicSerialize(Wire.route());
		Manifest.RouteDefinitionSha256 = Sha256(std::span(reinterpret_cast<const std::uint8_t *>(CanonicalRoute.data()), CanonicalRoute.size()));
		Manifest.CompressedSizeBytes = Wire.compressed_size_bytes();
		Manifest.UncompressedSizeBytes = Wire.uncompressed_size_bytes();
		if (Manifest.CompressedSizeBytes == 0 || Manifest.CompressedSizeBytes > MaximumCompressedPackageBytes ||
			Manifest.UncompressedSizeBytes < Manifest.CompressedSizeBytes || Manifest.UncompressedSizeBytes > MaximumCompressedPackageBytes * 4ULL)
			throw FContentValidationError(EContentError::InvalidSize, "package sizes exceed content policy");
		Manifest.bWithdrawn = Wire.withdrawn();
		Manifest.WithdrawalReasonKey = Wire.withdrawal_reason_key();
		if (Manifest.bWithdrawn && !IsLocalizationKey(Manifest.WithdrawalReasonKey))
			throw FContentValidationError(EContentError::Malformed, "withdrawal requires a safe reason key");
		Manifest.KeyId = Envelope.key_id();
		Manifest.CanonicalUnsignedPayload = CanonicalPayload;
		Manifest.ManifestSha256 = Sha256(std::span(reinterpret_cast<const std::uint8_t *>(EnvelopeBytes.data()), EnvelopeBytes.size()));
		return Manifest;
	}

	FRouteDefinition BuiltInStandardRouteDefinition()
	{
		FRouteDefinition Route;
		Route.SchemaVersion = RouteDefinitionSchemaV1;
		Route.RouteId = "route.standard.2k";
		Route.SemanticVersion = "1.0.0";
		Route.ContentSetId = "builtin-standard";
		Route.LengthMm = 2'000'000;
		Route.bClosed = true;
		Route.Checkpoints = {{"km1", 1'000'000}};
		Route.Compatibility = {1, std::numeric_limits<std::uint32_t>::max(), ContentManifestSchemaV1, RouteDefinitionSchemaV1};
		Route.DisplayNameKey = "route.standard.name";
		Route.DescriptionKey = "route.standard.description";
		constexpr std::string_view CanonicalIdentity = "route.standard.2k|1.0.0|builtin-standard|2000000|closed|km1:1000000";
		Route.MetadataSha256 = Sha256(std::span(reinterpret_cast<const std::uint8_t *>(CanonicalIdentity.data()), CanonicalIdentity.size()));
		return Route;
	}

	FRouteDefinition ParseAndValidateRouteDefinition(std::string_view RouteBytes, std::uint32_t ClientBuild)
	{
		if (RouteBytes.empty() || RouteBytes.size() > MaximumManifestBytes)
			throw FContentValidationError(EContentError::Oversized, "route definition has an invalid size");
		rowing::v1::RouteDefinitionV1 Wire;
		if (!Wire.ParseFromArray(RouteBytes.data(), static_cast<int>(RouteBytes.size())) || !Wire.IsInitialized())
			throw FContentValidationError(EContentError::Malformed, "route definition is malformed");
		if (DeterministicSerialize(Wire) != RouteBytes)
			throw FContentValidationError(EContentError::NonCanonical, "route definition is not canonical");
		return ConvertRoute(Wire, ClientBuild);
	}

	FSha256 Sha256(std::span<const std::uint8_t> Bytes)
	{
		FSha256Builder Builder;
		Builder.Update(Bytes);
		return Builder.Finish();
	}

	FSha256 Sha256File(const std::filesystem::path &Path, std::uint64_t MaximumBytes)
	{
		std::ifstream Input(Path, std::ios::binary);
		if (!Input)
			throw FContentValidationError(EContentError::IoFailure, "cannot open content file: " + Path.string());
		FSha256Builder Builder;
		std::array<std::uint8_t, 1024 * 1024> Buffer{};
		std::uint64_t Total = 0;
		while (Input)
		{
			Input.read(reinterpret_cast<char *>(Buffer.data()), static_cast<std::streamsize>(Buffer.size()));
			const auto Count = static_cast<std::size_t>(Input.gcount());
			Total += Count;
			if (Total > MaximumBytes)
				throw FContentValidationError(EContentError::Oversized, "content file exceeds declared bound: " + Path.string());
			Builder.Update(std::span(Buffer.data(), Count));
		}
		if (!Input.eof())
			throw FContentValidationError(EContentError::IoFailure, "cannot read content file: " + Path.string());
		return Builder.Finish();
	}

	std::string Sha256Hex(const FSha256 &Hash)
	{
		constexpr char Hex[] = "0123456789abcdef";
		std::string Result;
		Result.reserve(64);
		for (const std::uint8_t Byte : Hash)
		{
			Result.push_back(Hex[Byte >> 4U]);
			Result.push_back(Hex[Byte & 0x0FU]);
		}
		return Result;
	}

	std::optional<FSha256> ParseSha256(std::string_view Bytes)
	{
		if (Bytes.size() != 32)
			return std::nullopt;
		FSha256 Hash{};
		std::memcpy(Hash.data(), Bytes.data(), Hash.size());
		return Hash;
	}

	const char *ContentErrorName(EContentError Error) noexcept
	{
		switch (Error)
		{
		case EContentError::None:
			return "none";
		case EContentError::Malformed:
			return "malformed";
		case EContentError::Oversized:
			return "oversized";
		case EContentError::NonCanonical:
			return "non_canonical";
		case EContentError::UnknownKey:
			return "unknown_key";
		case EContentError::BadSignature:
			return "bad_signature";
		case EContentError::BadSchema:
			return "bad_schema";
		case EContentError::Incompatible:
			return "incompatible";
		case EContentError::Expired:
			return "expired";
		case EContentError::RevisionRollback:
			return "revision_rollback";
		case EContentError::Withdrawn:
			return "withdrawn";
		case EContentError::InvalidRoute:
			return "invalid_route";
		case EContentError::InvalidUrl:
			return "invalid_url";
		case EContentError::InvalidHash:
			return "invalid_hash";
		case EContentError::InvalidSize:
			return "invalid_size";
		case EContentError::InvalidPath:
			return "invalid_path";
		case EContentError::DisallowedAssetClass:
			return "disallowed_asset_class";
		case EContentError::MissingLicenseNotice:
			return "missing_license_notice";
		case EContentError::InvalidLicenseProvenance:
			return "invalid_license_provenance";
		case EContentError::InventoryMismatch:
			return "inventory_mismatch";
		case EContentError::PackageMismatch:
			return "package_mismatch";
		case EContentError::StorageInsufficient:
			return "storage_insufficient";
		case EContentError::WorkoutActive:
			return "workout_active";
		case EContentError::IoFailure:
			return "io_failure";
		}
		return "unknown";
	}
} // namespace ContentRuntime
