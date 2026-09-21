#include "ContentRuntime/ContentInventory.h"
#include "ContentRuntime/ContentManifest.h"
#include "ContentRuntime/ContentState.h"
#include "ContentRuntime/CourseLevel.h"

#include "rowing/v1/content.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	template <typename TMessage>
	std::string Serialize(const TMessage &Message)
	{
		std::string Bytes(Message.ByteSizeLong(), '\0');
		google::protobuf::io::ArrayOutputStream Array(Bytes.data(), static_cast<int>(Bytes.size()));
		google::protobuf::io::CodedOutputStream Coded(&Array);
		Coded.SetSerializationDeterministic(true);
		assert(Message.SerializeToCodedStream(&Coded));
		Bytes.resize(static_cast<std::size_t>(Coded.ByteCount()));
		return Bytes;
	}

	std::string HashBytes(const ContentRuntime::FSha256 &Hash)
	{
		return {reinterpret_cast<const char *>(Hash.data()), Hash.size()};
	}

	std::string HashText(std::string_view Text)
	{
		return HashBytes(ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Text.data()), Text.size())));
	}

	void AppendU16(std::string &Bytes, std::uint16_t Value)
	{
		Bytes.push_back(static_cast<char>(Value));
		Bytes.push_back(static_cast<char>(Value >> 8U));
	}

	void AppendU32(std::string &Bytes, std::uint32_t Value)
	{
		for (unsigned Index = 0; Index < 4; ++Index)
			Bytes.push_back(static_cast<char>(Value >> (Index * 8U)));
	}

	void AppendU64(std::string &Bytes, std::uint64_t Value)
	{
		for (unsigned Index = 0; Index < 8; ++Index)
			Bytes.push_back(static_cast<char>(Value >> (Index * 8U)));
	}

	rowing::v1::RouteDefinitionV1 ValidRoute();

	std::string MakeArchive(std::string_view InventoryBytes,
							std::string_view Notice = "Virtual Indoor Rowing content notice\n",
							bool bDuplicateNotice = false)
	{
		const std::string RouteBytes = Serialize(ValidRoute());
		const std::string WorldData(128, 'w');
		const std::string ImageData(128, 'i');
		std::vector<std::pair<std::string_view, std::string_view>> Entries = {
			{"HanRiver.pak", "pak-metadata-fixture"},
			{"HanRiver.utoc", "utoc-fixture"},
			{"HanRiver.ucas", "ucas-fixture"},
			{"inventory.pb", InventoryBytes},
			{"route.pb", RouteBytes},
			{"licenses/NOTICE.txt", Notice},
			{"Game/HanRiver/HanRiverWorld.json", WorldData},
		};
		if (InventoryBytes.find("ReferenceAdaptation") != std::string_view::npos)
			Entries.push_back({"Game/HanRiver/Textures/ReferenceAdaptation.json", ImageData});
		if (bDuplicateNotice)
			Entries.push_back({"licenses/NOTICE.txt", Notice});
		std::string Bytes = "VIRCNT1\n";
		AppendU32(Bytes, Entries.size());
		for (const auto &[Path, Data] : Entries)
		{
			AppendU16(Bytes, static_cast<std::uint16_t>(Path.size()));
			AppendU64(Bytes, Data.size());
			const auto Hash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Data.data()), Data.size()));
			Bytes.append(reinterpret_cast<const char *>(Hash.data()), Hash.size());
			Bytes.append(Path);
			Bytes.append(Data);
		}
		return Bytes;
	}

	class FDeterministicVerifier final : public ContentRuntime::IContentSignatureVerifier
	{
	  public:
		bool Verify(const ContentRuntime::FEd25519PublicKey &PublicKey,
					std::span<const std::uint8_t> Payload,
					std::span<const std::uint8_t> Signature) const override
		{
			if (Signature.size() != 64)
				return false;
			const auto Hash = ContentRuntime::Sha256(Payload);
			for (std::size_t Index = 0; Index < Signature.size(); ++Index)
			{
				if (Signature[Index] != static_cast<std::uint8_t>(Hash[Index % Hash.size()] ^ PublicKey[0]))
					return false;
			}
			return true;
		}
	};

	ContentRuntime::FEd25519PublicKey Key(std::uint8_t Marker)
	{
		ContentRuntime::FEd25519PublicKey KeyBytes{};
		KeyBytes.fill(Marker);
		return KeyBytes;
	}

	std::string SignatureFor(std::string_view Payload, std::uint8_t KeyMarker)
	{
		const auto Hash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Payload.data()), Payload.size()));
		std::string Signature(64, '\0');
		for (std::size_t Index = 0; Index < Signature.size(); ++Index)
			Signature[Index] = static_cast<char>(Hash[Index % Hash.size()] ^ KeyMarker);
		return Signature;
	}

	rowing::v1::RouteDefinitionV1 ValidRoute()
	{
		rowing::v1::RouteDefinitionV1 Route;
		Route.set_schema_version(1);
		Route.set_route_id("route.han-river.5k");
		Route.set_semantic_version("1.0.0");
		Route.set_content_set_id("han-river-alpha-1");
		Route.set_length_mm(5'000'000);
		Route.set_is_closed(false);
		auto *Compatibility = Route.mutable_compatibility();
		Compatibility->set_minimum_build(1);
		Compatibility->set_maximum_build(999999);
		Compatibility->set_content_schema(1);
		Compatibility->set_route_schema(1);
		Route.set_display_name_key("route.han.name");
		Route.set_description_key("route.han.description");
		for (int Index = 1; Index < 5; ++Index)
		{
			auto *Checkpoint = Route.add_checkpoints();
			Checkpoint->set_checkpoint_id("km" + std::to_string(Index));
			Checkpoint->set_distance_mm(static_cast<std::uint64_t>(Index) * 1'000'000ULL);
		}
		const std::string MetadataBytes = Serialize(Route);
		Route.set_metadata_sha256(HashBytes(ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(MetadataBytes.data()), MetadataBytes.size()))));
		return Route;
	}

	rowing::v1::ContentManifestUnsignedV1 ValidUnsigned(const ContentRuntime::FSha256 &InventoryHash,
														const ContentRuntime::FSha256 &PackageHash,
														std::uint64_t PackageSize)
	{
		rowing::v1::ContentManifestUnsignedV1 Manifest;
		Manifest.set_schema_version(1);
		Manifest.set_catalog_revision(10);
		Manifest.set_issued_at_unix_seconds(1'700'000'000);
		Manifest.set_expires_at_unix_seconds(1'700'604'800);
		for (auto *Compatibility : {Manifest.mutable_compatibility()})
		{
			Compatibility->set_minimum_build(1);
			Compatibility->set_maximum_build(999999);
			Compatibility->set_content_schema(1);
			Compatibility->set_route_schema(1);
		}
		*Manifest.mutable_route() = ValidRoute();
		Manifest.set_package_url("https://content.internal.invalid/han-river-alpha-1/1.0.0/package.vircontent");
		Manifest.set_package_sha256(HashBytes(PackageHash));
		Manifest.set_compressed_size_bytes(PackageSize);
		Manifest.set_uncompressed_size_bytes(PackageSize + 1024);
		Manifest.set_inventory_sha256(HashBytes(InventoryHash));
		return Manifest;
	}

	std::string EnvelopeFor(const rowing::v1::ContentManifestUnsignedV1 &Unsigned,
							std::string KeyId = "content-current",
							std::uint8_t KeyMarker = 0x11,
							std::optional<std::string> PayloadOverride = std::nullopt)
	{
		const std::string Payload = PayloadOverride.value_or(Serialize(Unsigned));
		rowing::v1::ContentManifestEnvelopeV1 Envelope;
		Envelope.set_envelope_version(1);
		Envelope.set_unsigned_payload(Payload);
		Envelope.set_key_id(std::move(KeyId));
		Envelope.set_signature(SignatureFor(Payload, KeyMarker));
		return Serialize(Envelope);
	}

	rowing::v1::ContentInventoryV1 ValidInventory(std::string_view NoticeText = "Virtual Indoor Rowing content notice\n")
	{
		rowing::v1::ContentInventoryV1 Inventory;
		Inventory.set_schema_version(1);
		Inventory.set_content_set_id("han-river-alpha-1");
		auto *World = Inventory.add_entries();
		World->set_relative_path("Game/HanRiver/HanRiverWorld.json");
		World->set_asset_class("World");
		World->set_size_bytes(128);
		World->set_sha256(HashText(std::string(128, 'w')));
		auto *Notice = Inventory.add_entries();
		Notice->set_relative_path("licenses/NOTICE.txt");
		Notice->set_asset_class("LicenseNotice");
		Notice->set_size_bytes(NoticeText.size());
		Notice->set_sha256(HashBytes(ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(NoticeText.data()), NoticeText.size()))));
		return Inventory;
	}

	rowing::v1::ContentInventoryV1 CcBySaInventory(std::string_view Version, std::string_view NoticeText)
	{
		auto Inventory = ValidInventory(NoticeText);
		auto *Image = Inventory.add_entries();
		Image->set_relative_path("Game/HanRiver/Textures/ReferenceAdaptation.json");
		Image->set_asset_class("Texture2D");
		Image->set_size_bytes(128);
		Image->set_sha256(HashText(std::string(128, 'i')));
		auto *License = Image->mutable_license();
		License->set_license_name("CC BY-SA");
		License->set_license_version(Version);
		License->set_license_url("https://creativecommons.org/licenses/by-sa/" + std::string(Version));
		License->set_creator("Han River Artist");
		License->set_canonical_source_url("https://source.invalid/han-river-image");
		License->set_source_sha256(std::string(32, 's'));
		License->set_modification_description("Cropped and retouched for the Han River texture atlas.");
		License->set_share_alike_release("CC BY-SA " + std::string(Version) + " in this package notice.");
		return Inventory;
	}

	template <typename FCallable>
	void ExpectError(ContentRuntime::EContentError Expected, FCallable &&Callable)
	{
		try
		{
			Callable();
			assert(false && "expected content validation failure");
		}
		catch (const ContentRuntime::FContentValidationError &Error)
		{
			assert(Error.GetCode() == Expected);
		}
	}

	void TestManifestTrustAndPolicy()
	{
		const auto InventoryBytes = Serialize(ValidInventory());
		const std::string PackageBytes = MakeArchive(InventoryBytes);
		const auto InventoryHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(InventoryBytes.data()), InventoryBytes.size()));
		const auto PackageHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(PackageBytes.data()), PackageBytes.size()));
		const auto Unsigned = ValidUnsigned(InventoryHash, PackageHash, PackageBytes.size());
		const std::vector<ContentRuntime::FTrustedContentKey> Keys = {{"content-current", Key(0x11)}, {"content-next", Key(0x22)}};
		FDeterministicVerifier Verifier;
		const ContentRuntime::FManifestPolicy Policy{42, 1'700'000'001, 9, false, false};
		const auto Manifest = ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Unsigned), Keys, Verifier, Policy);
		assert(Manifest.Route.RouteId == "route.han-river.5k");
		assert(Manifest.Route.LengthMm == 5'000'000);
		assert(!Manifest.Route.bClosed);
		assert(Manifest.Route.Checkpoints.size() == 4);

		const auto NextManifest = ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Unsigned, "content-next", 0x22), Keys, Verifier, Policy);
		assert(NextManifest.KeyId == "content-next");
		auto BadSignature = EnvelopeFor(Unsigned);
		BadSignature.back() ^= 0x01;
		ExpectError(ContentRuntime::EContentError::BadSignature, [&]
					{ ContentRuntime::ParseAndVerifyManifest(BadSignature, Keys, Verifier, Policy); });
		ExpectError(ContentRuntime::EContentError::UnknownKey, [&]
					{ ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Unsigned, "retired", 0x33), Keys, Verifier, Policy); });
		ExpectError(ContentRuntime::EContentError::Expired, [&]
					{ ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Unsigned), Keys, Verifier, {42, 1'700'604'800, 0, false, false}); });
		ExpectError(ContentRuntime::EContentError::RevisionRollback, [&]
					{ ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Unsigned), Keys, Verifier, {42, 1'700'000'001, 11, false, false}); });

		auto Withdrawn = Unsigned;
		Withdrawn.set_withdrawn(true);
		Withdrawn.set_withdrawal_reason_key("content.han.withdrawn");
		ExpectError(ContentRuntime::EContentError::Withdrawn, [&]
					{ ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Withdrawn), Keys, Verifier, Policy); });

		auto Incompatible = Unsigned;
		Incompatible.mutable_compatibility()->set_minimum_build(1000);
		ExpectError(ContentRuntime::EContentError::Incompatible, [&]
					{ ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Incompatible), Keys, Verifier, Policy); });
	}

	void TestCanonicalAndBoundedParsing()
	{
		const auto InventoryBytes = Serialize(ValidInventory());
		const auto InventoryHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(InventoryBytes.data()), InventoryBytes.size()));
		const std::string PackageBytes = MakeArchive(InventoryBytes);
		const auto PackageHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(PackageBytes.data()), PackageBytes.size()));
		const auto Unsigned = ValidUnsigned(InventoryHash, PackageHash, PackageBytes.size());
		std::string NonCanonical = Serialize(Unsigned);
		assert(NonCanonical.size() >= 2 && static_cast<unsigned char>(NonCanonical[0]) == 0x08 && static_cast<unsigned char>(NonCanonical[1]) == 0x01);
		NonCanonical.replace(1, 1, std::string("\x81\x00", 2));
		FDeterministicVerifier Verifier;
		const std::vector<ContentRuntime::FTrustedContentKey> Keys = {{"content-current", Key(0x11)}};
		ExpectError(ContentRuntime::EContentError::NonCanonical, [&]
					{ ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Unsigned, "content-current", 0x11, NonCanonical), Keys, Verifier, {42, 1'700'000'001, 0, false, false}); });
		const std::string Huge(ContentRuntime::MaximumManifestBytes + 1, 'x');
		ExpectError(ContentRuntime::EContentError::Oversized, [&]
					{ ContentRuntime::ParseAndVerifyManifest(Huge, Keys, Verifier, {}); });
	}

	void TestInventoryAndPackageValidation()
	{
		const auto InventoryWire = ValidInventory();
		const std::string InventoryBytes = Serialize(InventoryWire);
		const std::string PackageBytes = MakeArchive(InventoryBytes);
		const auto InventoryHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(InventoryBytes.data()), InventoryBytes.size()));
		const auto PackageHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(PackageBytes.data()), PackageBytes.size()));
		const auto Unsigned = ValidUnsigned(InventoryHash, PackageHash, PackageBytes.size());
		FDeterministicVerifier Verifier;
		const std::vector<ContentRuntime::FTrustedContentKey> Keys = {{"content-current", Key(0x11)}};
		const auto Manifest = ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(Unsigned), Keys, Verifier, {42, 1'700'000'001, 0, false, false});
		assert(ContentRuntime::ParseAndValidateInventory(InventoryBytes, Manifest).Entries.size() == 2);
		assert(ContentRuntime::IsSafeRelativeContentPath("Game/HanRiver/World"));
		assert(!ContentRuntime::IsSafeRelativeContentPath("../Plugins/Bad.dylib"));
		assert(!ContentRuntime::IsAllowedAssetClass("BlueprintGeneratedClass"));

		auto BadClass = InventoryWire;
		BadClass.mutable_entries(0)->set_asset_class("BlueprintGeneratedClass");
		const std::string BadClassBytes = Serialize(BadClass);
		auto BadClassManifest = Manifest;
		BadClassManifest.InventorySha256 = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(BadClassBytes.data()), BadClassBytes.size()));
		ExpectError(ContentRuntime::EContentError::DisallowedAssetClass, [&]
					{ ContentRuntime::ParseAndValidateInventory(BadClassBytes, BadClassManifest); });

		const auto Directory = std::filesystem::temp_directory_path() / ("vir_content_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(Directory);
		{
			std::ofstream Output(Directory / "package.vircontent", std::ios::binary);
			Output << PackageBytes;
		}
		assert(ContentRuntime::ReadStagedPackageInventory(Directory) == InventoryBytes);
		assert(ContentRuntime::ValidateStagedPackage(Directory, Manifest, InventoryBytes).Inventory.Entries.size() == 2);
		assert(ContentRuntime::HasStorageAdmission(Directory, Manifest) ==
			   (std::filesystem::space(Directory).available >= ContentRuntime::MinimumFreeStorageBytes &&
				std::filesystem::space(Directory).available - ContentRuntime::MinimumFreeStorageBytes >= Manifest.CompressedSizeBytes));
		{
			// Regression: the staging directory does not exist when admission runs.
			const auto Missing = Directory / "not-yet-created" / "staging" / "han-river-alpha-1";
			assert(ContentRuntime::HasStorageAdmission(Missing, Manifest) == ContentRuntime::HasStorageAdmission(Directory, Manifest));
			assert(!std::filesystem::exists(Directory / "not-yet-created"));
		}
		const auto Validated = ContentRuntime::ValidateStagedPackage(Directory, Manifest, InventoryBytes);
		const auto InstallDirectory = Directory / "installed";
		ContentRuntime::ExtractValidatedPackage(Validated, InstallDirectory);
		assert(std::filesystem::is_regular_file(InstallDirectory / "HanRiver.utoc"));
		assert(std::filesystem::is_regular_file(InstallDirectory / "HanRiver.ucas"));
		{
			std::ofstream Output(Directory / "package.vircontent", std::ios::binary | std::ios::trunc);
			std::string Tampered = PackageBytes;
			Tampered.back() ^= 1;
			Output << Tampered;
		}
		ExpectError(ContentRuntime::EContentError::PackageMismatch, [&]
					{ ContentRuntime::ValidateStagedPackage(Directory, Manifest, InventoryBytes); });
		std::filesystem::remove_all(Directory);
	}

	void TestBootFallbackAndWorkoutGuard()
	{
		using namespace ContentRuntime;
		assert(ContentOperationAllowed(false));
		assert(!ContentOperationAllowed(true));
		FInstalledContentRecord Active;
		Active.ContentSetId = "han-river-alpha-1";
		Active.RouteId = "route.han-river.5k";
		Active.State = EInstalledContentState::Active;
		Active.ExpiresAtUnixSeconds = 200;
		assert(SelectBootContent(Active, std::nullopt, 100).RouteId == "route.han-river.5k");
		const auto Expired = SelectBootContent(Active, std::nullopt, 200);
		assert(Expired.RouteId == "route.standard.2k");
		assert(Expired.HanUnavailableReason == "content.han.expired");
		assert(SelectBootContent(std::nullopt, std::nullopt, 100).RouteId == "route.standard.2k");
	}

	void TestCcBySaVersionsAndNoticeFailures()
	{
		for (const std::string Version : {"2.0", "3.0", "4.0"})
		{
			const std::string Notice = "Asset: Game/HanRiver/Textures/ReferenceAdaptation.json\n"
									   "Creator: Han River Artist\nLicense: CC BY-SA " +
									   Version + "\n"
												 "License URL: https://creativecommons.org/licenses/by-sa/" +
									   Version + "\n"
												 "Source: https://source.invalid/han-river-image\n"
												 "Changes: Cropped and retouched for the Han River texture atlas.\n"
												 "Share-alike release: CC BY-SA " +
									   Version + " in this package notice.\n";
			const auto InventoryWire = CcBySaInventory(Version, Notice);
			const std::string InventoryBytes = Serialize(InventoryWire);
			const std::string PackageBytes = MakeArchive(InventoryBytes, Notice);
			const auto InventoryHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(InventoryBytes.data()), InventoryBytes.size()));
			const auto PackageHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(PackageBytes.data()), PackageBytes.size()));
			const auto ManifestWire = ValidUnsigned(InventoryHash, PackageHash, PackageBytes.size());
			FDeterministicVerifier Verifier;
			const std::vector<ContentRuntime::FTrustedContentKey> Keys = {{"content-current", Key(0x11)}};
			const auto Manifest = ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(ManifestWire), Keys, Verifier, {42, 1'700'000'001, 0, false, false});
			const auto Directory = std::filesystem::temp_directory_path() / ("vir_cc_by_sa_" + Version);
			std::filesystem::remove_all(Directory);
			std::filesystem::create_directories(Directory);
			std::ofstream(Directory / "package.vircontent", std::ios::binary) << PackageBytes;
			assert(ContentRuntime::ValidateStagedPackage(Directory, Manifest, InventoryBytes).PackageNotice == Notice);
			std::filesystem::remove_all(Directory);
		}

		const std::string ValidNotice = "Asset: Game/HanRiver/Textures/ReferenceAdaptation.json\n"
										"Creator: Han River Artist\nLicense: CC BY-SA 3.0\n"
										"License URL: https://creativecommons.org/licenses/by-sa/3.0\n"
										"Source: https://source.invalid/han-river-image\n"
										"Changes: Cropped and retouched for the Han River texture atlas.\n"
										"Share-alike release: CC BY-SA 3.0 in this package notice.\n";
		const auto ValidCcInventory = CcBySaInventory("3.0", ValidNotice);
		for (const auto ClearField : {"license_version", "license_url", "creator", "canonical_source_url", "modification_description", "share_alike_release"})
		{
			auto Incomplete = ValidCcInventory;
			Incomplete.mutable_entries(2)->mutable_license()->GetReflection()->ClearField(Incomplete.mutable_entries(2)->mutable_license(),
																						  Incomplete.mutable_entries(2)->mutable_license()->GetDescriptor()->FindFieldByName(ClearField));
			const std::string Bytes = Serialize(Incomplete);
			const auto Hash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
			auto Manifest = ContentRuntime::FContentManifest{};
			Manifest.Route.ContentSetId = "han-river-alpha-1";
			Manifest.UncompressedSizeBytes = 1'000'000;
			Manifest.InventorySha256 = Hash;
			ExpectError(ContentRuntime::EContentError::InvalidLicenseProvenance, [&]
						{ ContentRuntime::ParseAndValidateInventory(Bytes, Manifest); });
		}
		{
			auto Incomplete = ValidCcInventory;
			Incomplete.mutable_entries(2)->mutable_license()->clear_source_sha256();
			const std::string Bytes = Serialize(Incomplete);
			auto Manifest = ContentRuntime::FContentManifest{};
			Manifest.Route.ContentSetId = "han-river-alpha-1";
			Manifest.UncompressedSizeBytes = 1'000'000;
			Manifest.InventorySha256 = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
			ExpectError(ContentRuntime::EContentError::InvalidLicenseProvenance, [&]
						{ ContentRuntime::ParseAndValidateInventory(Bytes, Manifest); });
		}

		{
			const std::string Bytes = Serialize(ValidCcInventory);
			const std::string PackageBytes = MakeArchive(Bytes, ValidNotice, true);
			const auto InventoryHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
			const auto PackageHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(PackageBytes.data()), PackageBytes.size()));
			const auto ManifestWire = ValidUnsigned(InventoryHash, PackageHash, PackageBytes.size());
			FDeterministicVerifier Verifier;
			const std::vector<ContentRuntime::FTrustedContentKey> Keys = {{"content-current", Key(0x11)}};
			const auto Manifest = ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(ManifestWire), Keys, Verifier, {42, 1'700'000'001, 0, false, false});
			const auto Directory = std::filesystem::temp_directory_path() / "vir_duplicate_notice";
			std::filesystem::remove_all(Directory);
			std::filesystem::create_directories(Directory);
			std::ofstream(Directory / "package.vircontent", std::ios::binary) << PackageBytes;
			ExpectError(ContentRuntime::EContentError::InvalidPath, [&]
						{ ContentRuntime::ValidateStagedPackage(Directory, Manifest, Bytes); });
			std::filesystem::remove_all(Directory);
		}

		{
			auto MismatchedInventory = ValidCcInventory;
			MismatchedInventory.mutable_entries(0)->set_size_bytes(129);
			const std::string Bytes = Serialize(MismatchedInventory);
			const auto InventoryHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(Bytes.data()), Bytes.size()));
			const std::string PackageBytes = MakeArchive(Bytes, ValidNotice);
			const auto PackageHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(PackageBytes.data()), PackageBytes.size()));
			const auto ManifestWire = ValidUnsigned(InventoryHash, PackageHash, PackageBytes.size());
			FDeterministicVerifier Verifier;
			const std::vector<ContentRuntime::FTrustedContentKey> Keys = {{"content-current", Key(0x11)}};
			const auto Manifest = ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(ManifestWire), Keys, Verifier, {42, 1'700'000'001, 0, false, false});
			const auto Directory = std::filesystem::temp_directory_path() / "vir_notice_inventory_mismatch";
			std::filesystem::remove_all(Directory);
			std::filesystem::create_directories(Directory);
			std::ofstream(Directory / "package.vircontent", std::ios::binary) << PackageBytes;
			ExpectError(ContentRuntime::EContentError::InventoryMismatch, [&]
						{ ContentRuntime::ValidateStagedPackage(Directory, Manifest, Bytes); });
			std::filesystem::remove_all(Directory);
		}

		auto MissingNotice = ValidInventory();
		MissingNotice.mutable_entries(1)->set_relative_path("docs/NOTICE.txt");
		const std::string MissingNoticeBytes = Serialize(MissingNotice);
		const auto InventoryHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(MissingNoticeBytes.data()), MissingNoticeBytes.size()));
		const auto PackageBytes = MakeArchive(MissingNoticeBytes);
		const auto PackageHash = ContentRuntime::Sha256(std::span(reinterpret_cast<const std::uint8_t *>(PackageBytes.data()), PackageBytes.size()));
		const auto ManifestWire = ValidUnsigned(InventoryHash, PackageHash, PackageBytes.size());
		FDeterministicVerifier Verifier;
		const std::vector<ContentRuntime::FTrustedContentKey> Keys = {{"content-current", Key(0x11)}};
		const auto Manifest = ContentRuntime::ParseAndVerifyManifest(EnvelopeFor(ManifestWire), Keys, Verifier, {42, 1'700'000'001, 0, false, false});
		ExpectError(ContentRuntime::EContentError::MissingLicenseNotice, [&]
					{ ContentRuntime::ParseAndValidateInventory(MissingNoticeBytes, Manifest); });
	}
} // namespace

void TestCourseLevelTableAndAllowlist()
{
	assert(ContentRuntime::CourseLevelAssetPathForRoute("route.han-river.5k") == "/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour" && "Han level is selected by route ID");
	assert(!ContentRuntime::CourseLevelAssetPathForRoute("route.standard.2k").has_value() && "Standard has no downloaded level");
	assert(!ContentRuntime::CourseLevelAssetPathForRoute("/Game/Evil/Map").has_value() && "a package path is never a route ID");
	assert(ContentRuntime::IsCourseLevelActorClassAllowed("/Script/Engine.StaticMeshActor") && "static mesh actors are allowed");
	assert(!ContentRuntime::IsCourseLevelActorClassAllowed("/Game/Evil/BP_Actor.BP_Actor_C") && "Blueprint classes are rejected");
	assert(!ContentRuntime::IsCourseLevelActorClassAllowed("/Script/VirtualRowing.GrayBoxCourseActor") && "project classes are rejected");
	assert(!ContentRuntime::IsCourseLevelActorClassAllowed("/Script/Engine.LevelScriptBlueprint") && "level Blueprints are rejected");
	assert(!ContentRuntime::IsCourseLevelActorClassAllowed("") && "an empty class name is rejected");
	assert(ContentRuntime::CourseLevelActorRequiresComponentCheck("/Script/Engine.Actor") && "a plain Actor is component-checked");
	assert(!ContentRuntime::CourseLevelActorRequiresComponentCheck("/Script/Engine.StaticMeshActor") && "typed actors are not component-checked");
	assert(ContentRuntime::IsCourseLevelComponentClassAllowed("/Script/Engine.HierarchicalInstancedStaticMeshComponent") && "HISM components are allowed");
	assert(ContentRuntime::IsCourseLevelComponentClassAllowed("/Script/Engine.SceneComponent") && "a scene root is allowed");
	assert(!ContentRuntime::IsCourseLevelComponentClassAllowed("/Script/Engine.AudioComponent") && "audio is not allowed on a plain Actor yet");
	assert(!ContentRuntime::IsCourseLevelComponentClassAllowed("/Script/Engine.ChildActorComponent") && "child-actor components are rejected");
	assert(!ContentRuntime::IsCourseLevelComponentClassAllowed("/Script/Engine.SplineMeshComponent") && "spline mesh is not allowed on a plain Actor yet");
	assert(!ContentRuntime::IsCourseLevelComponentClassAllowed("/Game/Evil/BP_Comp.BP_Comp_C") && "Blueprint components are rejected");
	assert(!ContentRuntime::IsCourseLevelComponentClassAllowed("") && "an empty component class name is rejected");
}

void TestPackageSizeCap()
{
	constexpr std::uint64_t GiB = 1024ULL * 1024ULL * 1024ULL;
	static_assert(ContentRuntime::MaximumCompressedPackageBytes == 100ULL * GiB, "Han package cap is 100 GiB");
	// The free-space rule adds the package to a fixed 100 GiB headroom, so it scales with the cap.
	static_assert(ContentRuntime::MinimumFreeStorageBytes == 100ULL * GiB, "free-space headroom is unchanged");
}

int main()
{
	TestPackageSizeCap();
	TestManifestTrustAndPolicy();
	TestCanonicalAndBoundedParsing();
	TestInventoryAndPackageValidation();
	TestBootFallbackAndWorkoutGuard();
	TestCcBySaVersionsAndNoticeFailures();
	TestCourseLevelTableAndAllowlist();
	std::cout << "content runtime tests passed\n";
}
