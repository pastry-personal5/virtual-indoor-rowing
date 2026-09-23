#include "ContentSubsystem.h"

#include "WorkoutSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IPlatformFilePak.h"
#include "IO/IoStatus.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include "LocalData/ContentRepository.h"
#include "ContentRuntime/ContentInventory.h"
#include "ContentRuntimeMac/CryptoKitContentVerifier.h"
#include "WorkoutRuntime/WorkoutSnapshot.h"

#include <chrono>
#include <filesystem>
#include <memory>

DEFINE_LOG_CATEGORY_STATIC(LogContentSubsystem, Log, All);

namespace
{
	constexpr uint32 ContentClientBuild = 1;
	constexpr const TCHAR *AppDataFolderName = TEXT("dev.virtualrowing.app");

	int64 UnixNowSeconds()
	{
		return static_cast<int64>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
	}

	ContentRuntime::FEd25519PublicKey PublicKey(std::initializer_list<uint8> Bytes)
	{
		ContentRuntime::FEd25519PublicKey Key{};
		check(Bytes.size() == Key.size());
		int32 Index = 0;
		for (const uint8 Byte : Bytes)
			Key[Index++] = Byte;
		return Key;
	}

	const std::vector<ContentRuntime::FTrustedContentKey> &TrustedKeys()
	{
		// These are verification keys only. The restricted release machine owns
		// the corresponding offline signing keys; no private key enters the app,
		// repository, CI, or ordinary developer setup.
		static const std::vector<ContentRuntime::FTrustedContentKey> Keys = {
			{"content-current", PublicKey({0x66, 0xc3, 0x6d, 0x6a, 0x30, 0x36, 0xdb, 0x50, 0x3c, 0xab, 0x31, 0x0d, 0x11, 0xc0, 0x6b, 0xc4, 0x2a, 0xc2, 0xbf, 0x72, 0x59, 0x49, 0xcc, 0x50, 0x28, 0xb8, 0x0a, 0xbc, 0xe1, 0xfa, 0x24, 0x68})},
			{"content-next", PublicKey({0x6b, 0x96, 0xb0, 0x47, 0xed, 0xdc, 0x30, 0x3d, 0xa2, 0x8c, 0x5b, 0x4c, 0xfe, 0x46, 0x85, 0x66, 0x1c, 0xd8, 0x6c, 0x23, 0x63, 0x9c, 0x00, 0x0c, 0xbb, 0x07, 0x03, 0xcf, 0xe7, 0x10, 0xbb, 0x57})},
		};
		return Keys;
	}

	std::string LoadRouteDefinitionBytes(const FString &Path)
	{
		TArray64<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.IsEmpty())
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::IoFailure, "cannot read extracted route definition");
		return {reinterpret_cast<const char *>(Bytes.GetData()), static_cast<size_t>(Bytes.Num())};
	}
} // namespace

struct UContentSubsystem::FImpl
{
	ContentRuntime::FRouteDefinition StandardRoute = ContentRuntime::BuiltInStandardRouteDefinition();
	ContentRuntime::FRouteDefinition SelectedRoute = StandardRoute;
	ContentRuntime::FRouteDefinition HanRoute;
	bool bHanAvailable = false;
	FString HanAvailabilityReason = TEXT("content.han.not_installed");
	FString ActivePackageNotice;
	int64 ActivePackageIssuedAtUnixSeconds = 0;
	std::unique_ptr<LocalData::FContentRepository> Repository;
	FString MountedInstallPath;
	FString CatalogUrl;
	FString ContentRoot;
	FString OperationStatus = TEXT("content.idle");
	std::optional<ContentRuntime::FContentManifest> CatalogManifest;

	// Development diagnostics only: nothing here feeds a decision.
	TArray<FString> Events;
	double StartSeconds = FPlatformTime::Seconds();
	int32 CatalogHttpCode = 0;
	int32 CatalogBytes = 0;
	int32 PackageHttpCode = 0;
	uint64 DownloadReceivedBytes = 0;
	uint64 DownloadExpectedBytes = 0;
	FString ActiveVersion;
	uint64 ActiveCatalogRevision = 0;
	FString ActiveInstallLeaf;

	void Note(const FString &Message)
	{
		Events.Add(FString::Printf(TEXT("+%.1fs %s"), FPlatformTime::Seconds() - StartSeconds, *Message));
		while (Events.Num() > 12)
			Events.RemoveAt(0);
	}

	// Retained (active or last-known-good) content is never overwritten by a download.
	bool IsVersionRetained(const ContentRuntime::FContentManifest &Manifest) const
	{
		for (const auto &Retained : Repository->ListRetained())
		{
			if (Retained.ContentSetId == Manifest.Route.ContentSetId && Retained.SemanticVersion == Manifest.Route.SemanticVersion &&
				(Retained.State == ContentRuntime::EInstalledContentState::Active || Retained.State == ContentRuntime::EInstalledContentState::LastKnownGood))
				return true;
		}
		return false;
	}
};

void UContentSubsystem::Initialize(FSubsystemCollectionBase &Collection)
{
	Super::Initialize(Collection);
	Impl = MakeShared<FImpl>();
	if (FParse::Param(FCommandLine::Get(), TEXT("ContentSafeMode")))
	{
		Impl->HanAvailabilityReason = TEXT("content.han.safe_mode");
		return;
	}
	try
	{
		const FString AppSupport = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT("Library/Application Support"), AppDataFolderName);
		std::filesystem::create_directories(TCHAR_TO_UTF8(*AppSupport));
		Impl->Repository = std::make_unique<LocalData::FContentRepository>(std::filesystem::path(TCHAR_TO_UTF8(*FPaths::Combine(AppSupport, TEXT("rowing.sqlite3")))));
		Impl->ContentRoot = FPaths::Combine(AppSupport, TEXT("Content"));
		std::filesystem::create_directories(TCHAR_TO_UTF8(*Impl->ContentRoot));
		FParse::Value(FCommandLine::Get(), TEXT("ContentCatalogUrl="), Impl->CatalogUrl);
		// A verified set is deliberately activated only on a subsequent launch.
		// This is before menu presentation and therefore cannot change an active row.
		if (const auto Verified = Impl->Repository->FindByState(ContentRuntime::EInstalledContentState::Verified))
			Impl->Repository->ActivateVerified(Verified->ContentSetId, false);
		const int64 Now = UnixNowSeconds();
		const auto Active = Impl->Repository->FindByState(ContentRuntime::EInstalledContentState::Active);
		const auto LastKnownGood = Impl->Repository->FindByState(ContentRuntime::EInstalledContentState::LastKnownGood);
		FString LastFailure;
		auto TryCandidate = [this, Now, &LastFailure](const std::optional<ContentRuntime::FInstalledContentRecord> &Candidate)
		{
			if (!Candidate || Candidate->ExpiresAtUnixSeconds <= Now || Candidate->State == ContentRuntime::EInstalledContentState::Withdrawn)
				return false;
			const FString InstallPath = UTF8_TO_TCHAR(Candidate->InstallPath.c_str());
			FString Failure;
			try
			{
				// Parse before mounting so an invalid route never changes the IoStore
				// mount set. The archive validator has already bound this file to the
				// signed manifest before it could reach the install directory.
				Impl->HanRoute = ContentRuntime::ParseAndValidateRouteDefinition(
					LoadRouteDefinitionBytes(FPaths::Combine(InstallPath, TEXT("route.pb"))), ContentClientBuild);
				Impl->SelectedRoute = Impl->HanRoute;
				if (TryMountInstalledContent(InstallPath, Failure))
				{
					FFileHelper::LoadFileToString(Impl->ActivePackageNotice, *FPaths::Combine(InstallPath, TEXT("licenses/NOTICE.txt")));
					Impl->ActivePackageIssuedAtUnixSeconds = Candidate->IssuedAtUnixSeconds;
					Impl->ActiveVersion = UTF8_TO_TCHAR(Candidate->SemanticVersion.c_str());
					Impl->ActiveCatalogRevision = Candidate->CatalogRevision;
					Impl->ActiveInstallLeaf = FPaths::GetCleanFilename(InstallPath);
					Impl->Note(FString::Printf(TEXT("boot: mounted v%s (catalog r%llu)"), *Impl->ActiveVersion, Impl->ActiveCatalogRevision));
					return true;
				}
			}
			catch (const ContentRuntime::FContentValidationError &Error)
			{
				Failure = UTF8_TO_TCHAR(ContentRuntime::ContentErrorName(Error.GetCode()));
				Impl->Note(FString::Printf(TEXT("boot: route.pb rejected: %s"), UTF8_TO_TCHAR(Error.what())));
			}
			if (Failure.IsEmpty())
				Failure = TEXT("content.mount_failed");
			Impl->Note(FString::Printf(TEXT("boot: candidate v%s failed: %s"), UTF8_TO_TCHAR(Candidate->SemanticVersion.c_str()), *Failure));
			Impl->Repository->MarkFailed(Candidate->ContentSetId, TCHAR_TO_UTF8(*Failure), false);
			LastFailure = Failure;
			Impl->SelectedRoute = Impl->StandardRoute;
			return false;
		};
		if (TryCandidate(Active) || TryCandidate(LastKnownGood))
		{
			Impl->bHanAvailable = true;
			Impl->HanAvailabilityReason.Empty();
		}
		else
		{
			const auto Selection = ContentRuntime::SelectBootContent(Active, LastKnownGood, Now);
			Impl->HanAvailabilityReason = LastFailure.IsEmpty()
											  ? UTF8_TO_TCHAR(Selection.HanUnavailableReason.c_str())
											  : LastFailure;
			// A previous launch already marked this package failed. Keep that
			// reason visible after a restart instead of reporting "not installed".
			if (Impl->HanAvailabilityReason == TEXT("content.han.not_installed"))
			{
				for (const auto &Record : Impl->Repository->ListRetained())
				{
					if (Record.State == ContentRuntime::EInstalledContentState::Failed && !Record.FailureCategory.empty())
					{
						Impl->HanAvailabilityReason = UTF8_TO_TCHAR(Record.FailureCategory.c_str());
						break;
					}
				}
			}
		}
	}
	catch (const std::exception &Error)
	{
		UE_LOG(LogContentSubsystem, Warning, TEXT("Content boot failed; using Standard route: %s"), UTF8_TO_TCHAR(Error.what()));
		Impl->SelectedRoute = Impl->StandardRoute;
		Impl->HanAvailabilityReason = TEXT("content.han.validation_failed");
		// Shipping compiles UE_LOG out; keep the cause visible through the status line.
		Impl->OperationStatus = FString::Printf(TEXT("content.init_failed: %s"), UTF8_TO_TCHAR(Error.what()));
		Impl->Note(Impl->OperationStatus);
	}
	// Network/catalog work is strictly background boot work. Absence or failure
	// cannot delay the local menu or alter Standard's availability.
	BeginCatalogRefresh();
}

void UContentSubsystem::Deinitialize()
{
	Impl.Reset();
	Super::Deinitialize();
}

const ContentRuntime::FRouteDefinition &UContentSubsystem::GetSelectedRoute() const
{
	static const ContentRuntime::FRouteDefinition StandardRoute = ContentRuntime::BuiltInStandardRouteDefinition();
	return Impl ? Impl->SelectedRoute : StandardRoute;
}

FString UContentSubsystem::GetHanAvailabilityReason() const
{
	return Impl ? Impl->HanAvailabilityReason : TEXT("content.han.not_installed");
}

bool UContentSubsystem::IsHanAvailable() const
{
	return Impl && Impl->bHanAvailable;
}

bool UContentSubsystem::IsHanContentMounted() const
{
	return Impl && !Impl->MountedInstallPath.IsEmpty();
}

FString UContentSubsystem::GetActivePackageNotice() const
{
	return Impl ? Impl->ActivePackageNotice : FString();
}

int64 UContentSubsystem::GetActivePackageIssuedAtUnixSeconds() const
{
	return Impl ? Impl->ActivePackageIssuedAtUnixSeconds : 0;
}

bool UContentSubsystem::IsHanUpdateAvailable() const
{
	return Impl && Impl->CatalogManifest && !Impl->CatalogManifest->bWithdrawn && IsHanContentMounted() && Impl->OperationStatus == TEXT("content.catalog_ready") && Impl->CatalogManifest->IssuedAtUnixSeconds > Impl->ActivePackageIssuedAtUnixSeconds &&
		   // A rebuild under the mounted version cannot be installed (retained versions are never overwritten).
		   FString(UTF8_TO_TCHAR(Impl->CatalogManifest->Route.SemanticVersion.c_str())) != Impl->ActiveVersion;
}

FString UContentSubsystem::GetContentLicensesCreditsText() const
{
	if (!Impl || Impl->ActivePackageNotice.IsEmpty())
		return TEXT("Standard route: VIR-authored content. No third-party package notice is active.");
	return Impl->ActivePackageNotice;
}

bool UContentSubsystem::SelectRouteById(const FString &RouteId)
{
	if (!Impl || !CanSelectRoute(RouteId, IsHanAvailable(), IsWorkoutActive()))
		return false;
	if (RouteId == TEXT("route.standard.2k"))
	{
		Impl->SelectedRoute = Impl->StandardRoute;
		return true;
	}
	if (RouteId == TEXT("route.han-river.5k"))
	{
		// A verified Han route is already selected at boot. The explicit branch
		// keeps the UI contract ID-based while preventing a Standard selection
		// from making a still-mounted Han package appear unavailable.
		Impl->SelectedRoute = Impl->HanRoute;
		return true;
	}
	return false;
}

bool UContentSubsystem::CanSelectRoute(const FString &RouteId, bool bHanAvailable, bool bWorkoutActive)
{
	if (bWorkoutActive)
		return false;
	if (RouteId == TEXT("route.standard.2k"))
		return true;
	return RouteId == TEXT("route.han-river.5k") && bHanAvailable;
}

bool UContentSubsystem::CanOperateContent() const
{
	return !IsWorkoutActive();
}

namespace
{
	// Plain-language reading of the stable status categories for the diagnostics view.
	FString DescribeContentStatus(const FString &Status)
	{
		if (Status == TEXT("content.idle"))
			return TEXT("Nothing has been requested yet.");
		if (Status == TEXT("content.catalog_url_missing"))
			return TEXT("No -ContentCatalogUrl launch argument was given.");
		if (Status == TEXT("content.catalog_refreshing"))
			return TEXT("Fetching the signed catalog.");
		if (Status == TEXT("content.catalog_ready"))
			return TEXT("Catalog verified. The button appears only if it is newer than what is mounted.");
		if (Status.StartsWith(TEXT("content.catalog_network_failed")))
			return TEXT("The catalog could not be fetched: check the URL and that the origin is serving it.");
		if (Status == TEXT("content.catalog_withdrawn"))
			return TEXT("The catalog withdrew this content.");
		if (Status == TEXT("content.download_running"))
			return TEXT("Downloading the package.");
		if (Status == TEXT("content.download_partial"))
			return TEXT("The download stopped short; click Download to resume.");
		if (Status == TEXT("content.download_network_failed"))
			return TEXT("The package request failed: the URL in the catalog is probably not being served.");
		if (Status == TEXT("content.download_verified_restart_required"))
			return TEXT("Installed and verified. Relaunch the app to activate it.");
		if (Status == TEXT("content.version_already_installed"))
			return TEXT("This version is already retained; publish under a new version.");
		if (Status == TEXT("content.storage_insufficient"))
			return TEXT("Not enough free disk space for the package.");
		if (Status == TEXT("io_failure"))
			return TEXT("A file or network read/write failed. The event log names which.");
		if (Status == TEXT("malformed"))
			return TEXT("Received or stored data has an invalid structure. The event log names which.");
		return FString();
	}

	FString ShortUrl(const FString &Url)
	{
		FString Rest = Url;
		int32 SchemeEnd = INDEX_NONE;
		if (Rest.FindChar(TEXT(':'), SchemeEnd) && Rest.Mid(SchemeEnd, 3) == TEXT("://"))
			Rest = Rest.Mid(SchemeEnd + 3);
		FString Host;
		FString Path;
		if (!Rest.Split(TEXT("/"), &Host, &Path))
			return Rest;
		FString Leaf = FPaths::GetCleanFilename(Path);
		if (Leaf.StartsWith(TEXT("catalog-")) && Leaf.Len() > 20)
			Leaf = Leaf.Left(16) + TEXT("...") + FPaths::GetExtension(Leaf, true);
		return Host + TEXT("/.../") + Leaf;
	}

	FString UtcText(int64 UnixSeconds)
	{
		return FDateTime::FromUnixTimestamp(UnixSeconds).ToString(TEXT("%Y-%m-%d %H:%M:%S"));
	}
} // namespace

FString UContentSubsystem::GetDiagnosticsText() const
{
	if (!Impl)
		return TEXT("Content subsystem not initialized.");
	TArray<FString> Lines;
	const FString Hint = DescribeContentStatus(Impl->OperationStatus);
	if (!Hint.IsEmpty())
		Lines.Add(Hint);
	Lines.Add(FString::Printf(TEXT("catalog url: %s"), Impl->CatalogUrl.IsEmpty() ? TEXT("(none)") : *ShortUrl(Impl->CatalogUrl)));
	if (Impl->CatalogHttpCode != 0)
		Lines.Add(FString::Printf(TEXT("catalog fetch: HTTP %d, %d bytes"), Impl->CatalogHttpCode, Impl->CatalogBytes));
	if (Impl->Repository)
	{
		try
		{
			Lines.Add(FString::Printf(TEXT("accepted catalog revision: %llu"), static_cast<uint64>(Impl->Repository->AcceptedCatalogRevision())));
			for (const auto &Record : Impl->Repository->ListRetained())
			{
				FString Line = FString::Printf(TEXT("installed: v%s %s (catalog r%llu, built %s UTC)"), UTF8_TO_TCHAR(Record.SemanticVersion.c_str()), UTF8_TO_TCHAR(ContentRuntime::InstalledContentStateName(Record.State)), static_cast<uint64>(Record.CatalogRevision), *UtcText(Record.IssuedAtUnixSeconds));
				if (Record.State == ContentRuntime::EInstalledContentState::Failed && !Record.FailureCategory.empty())
					Line += FString::Printf(TEXT(" failure=%s"), UTF8_TO_TCHAR(Record.FailureCategory.c_str()));
				Lines.Add(MoveTemp(Line));
			}
		}
		catch (const std::exception &Error)
		{
			Lines.Add(FString::Printf(TEXT("installed: (unreadable: %s)"), UTF8_TO_TCHAR(Error.what())));
		}
	}
	if (Impl->CatalogManifest)
		Lines.Add(FString::Printf(TEXT("catalog offers: v%s (r%llu, built %s UTC, %llu bytes)"), UTF8_TO_TCHAR(Impl->CatalogManifest->Route.SemanticVersion.c_str()), static_cast<uint64>(Impl->CatalogManifest->CatalogRevision), *UtcText(Impl->CatalogManifest->IssuedAtUnixSeconds), static_cast<uint64>(Impl->CatalogManifest->CompressedSizeBytes)));
	Lines.Add(FString::Printf(TEXT("mounted: %s"), IsHanContentMounted() ? *FString::Printf(TEXT("v%s (installed/.../%s)"), *Impl->ActiveVersion, *Impl->ActiveInstallLeaf) : TEXT("no")));
	if (!Impl->HanAvailabilityReason.IsEmpty())
		Lines.Add(FString::Printf(TEXT("han unavailable because: %s"), *Impl->HanAvailabilityReason));
	Lines.Add(FString::Printf(TEXT("update available: %s"), IsHanUpdateAvailable() ? TEXT("yes") : TEXT("no")));
	if (Impl->DownloadExpectedBytes > 0)
		Lines.Add(FString::Printf(TEXT("download: %llu / %llu bytes (HTTP %d)"), Impl->DownloadReceivedBytes, Impl->DownloadExpectedBytes, Impl->PackageHttpCode));
	if (Impl->Events.Num() > 0)
	{
		Lines.Add(TEXT("events:"));
		for (const FString &Event : Impl->Events)
			Lines.Add(TEXT("  ") + Event);
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString UContentSubsystem::GetContentOperationStatus() const
{
	return Impl ? Impl->OperationStatus : TEXT("content.unavailable");
}

bool UContentSubsystem::BeginCatalogRefresh()
{
	if (!Impl || !CanOperateContent())
		return false;
	if (Impl->CatalogUrl.IsEmpty())
	{
		// Never overwrite a boot failure with a generic "no URL" state.
		if (Impl->OperationStatus == TEXT("content.idle"))
			Impl->OperationStatus = TEXT("content.catalog_url_missing");
		return false;
	}
	FHttpRequestPtr Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Impl->CatalogUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/octet-stream"));
	Request->OnProcessRequestComplete().BindUObject(this, &UContentSubsystem::HandleCatalogResponse);
	Impl->OperationStatus = TEXT("content.catalog_refreshing");
	Impl->Note(FString::Printf(TEXT("catalog: GET %s"), *ShortUrl(Impl->CatalogUrl)));
	if (!Request->ProcessRequest())
	{
		Impl->OperationStatus = TEXT("content.catalog_network_failed");
		return false;
	}
	return true;
}

void UContentSubsystem::HandleCatalogResponse(FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
{
	if (!Impl || !bSucceeded || !Response.IsValid() || Response->GetResponseCode() != 200 || !CanOperateContent())
	{
		if (Impl)
		{
			Impl->CatalogHttpCode = Response.IsValid() ? Response->GetResponseCode() : 0;
			Impl->OperationStatus = FString::Printf(TEXT("content.catalog_network_failed (%s, HTTP %d)"), bSucceeded ? TEXT("connected") : TEXT("no response"), Impl->CatalogHttpCode);
			Impl->Note(FString::Printf(TEXT("catalog: request failed (%s, HTTP %d)"), bSucceeded ? TEXT("connected") : TEXT("no response"), Impl->CatalogHttpCode));
		}
		return;
	}
	try
	{
		const TArray<uint8> &Bytes = Response->GetContent();
		Impl->CatalogHttpCode = Response->GetResponseCode();
		Impl->CatalogBytes = Bytes.Num();
		Impl->Note(FString::Printf(TEXT("catalog: HTTP 200, %d bytes"), Bytes.Num()));
		if (Bytes.IsEmpty() || Bytes.Num() > static_cast<int32>(ContentRuntime::MaximumManifestBytes))
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::Oversized, "catalog response exceeds bounds");
		const std::string Envelope(reinterpret_cast<const char *>(Bytes.GetData()), Bytes.Num());
		ContentRuntimeMac::FCryptoKitContentVerifier Verifier;
		const auto Manifest = ContentRuntime::ParseAndVerifyManifest(Envelope, TrustedKeys(), Verifier, {ContentClientBuild, UnixNowSeconds(), Impl->Repository->AcceptedCatalogRevision(), false, true});
		Impl->Repository->AcceptCatalogRevision(Manifest.CatalogRevision, ContentRuntime::Sha256Hex(Manifest.ManifestSha256));
		if (Manifest.bWithdrawn)
		{
			Impl->Repository->ApplyWithdrawal(Manifest.Route.ContentSetId, false);
			Impl->CatalogManifest.reset();
			Impl->bHanAvailable = false;
			Impl->SelectedRoute = Impl->StandardRoute;
			Impl->HanAvailabilityReason = UTF8_TO_TCHAR(Manifest.WithdrawalReasonKey.c_str());
			Impl->OperationStatus = TEXT("content.catalog_withdrawn");
			return;
		}
		Impl->CatalogManifest = Manifest;
		Impl->OperationStatus = TEXT("content.catalog_ready");
		Impl->Note(FString::Printf(TEXT("catalog: verified r%llu offering v%s"), Manifest.CatalogRevision, UTF8_TO_TCHAR(Manifest.Route.SemanticVersion.c_str())));
	}
	catch (const ContentRuntime::FContentValidationError &Error)
	{
		Impl->OperationStatus = UTF8_TO_TCHAR(ContentRuntime::ContentErrorName(Error.GetCode()));
		Impl->Note(FString::Printf(TEXT("catalog: rejected: %s"), UTF8_TO_TCHAR(Error.what())));
	}
	catch (const std::exception &Error)
	{
		Impl->OperationStatus = TEXT("content.catalog_validation_failed");
		Impl->Note(FString::Printf(TEXT("catalog: error: %s"), UTF8_TO_TCHAR(Error.what())));
	}
}

bool UContentSubsystem::BeginHanDownload()
{
	if (!Impl || !CanOperateContent() || !Impl->CatalogManifest || Impl->CatalogManifest->bWithdrawn)
		return false;
	const ContentRuntime::FContentManifest &Manifest = *Impl->CatalogManifest;
	// Refuse a retained version up front rather than after downloading the whole package.
	if (Impl->IsVersionRetained(Manifest))
	{
		Impl->OperationStatus = TEXT("content.version_already_installed");
		Impl->Note(FString::Printf(TEXT("download: v%s is already retained; not downloading"), UTF8_TO_TCHAR(Manifest.Route.SemanticVersion.c_str())));
		return false;
	}
	const FString StagingDirectory = FPaths::Combine(Impl->ContentRoot, TEXT("staging"), UTF8_TO_TCHAR(Manifest.Route.ContentSetId.c_str()));
	if (!ContentRuntime::HasStorageAdmission(std::filesystem::path(TCHAR_TO_UTF8(*StagingDirectory)), Manifest))
	{
		Impl->OperationStatus = TEXT("content.storage_insufficient");
		return false;
	}
	std::filesystem::create_directories(TCHAR_TO_UTF8(*StagingDirectory));
	const FString PackagePath = FPaths::Combine(StagingDirectory, TEXT("package.vircontent"));
	const uint64 ExistingSize = IFileManager::Get().FileExists(*PackagePath) ? static_cast<uint64>(IFileManager::Get().FileSize(*PackagePath)) : 0;
	// A file that already has the full size was either never validated or failed
	// validation. A Range request from its end would get HTTP 416 and could never
	// recover, so restart from zero.
	const bool bRestart = ExistingSize >= Manifest.CompressedSizeBytes;
	if (bRestart)
		IFileManager::Get().Delete(*PackagePath, false, true);
	const uint64 ResumeOffset = bRestart ? 0 : ExistingSize;
	Impl->Repository->SaveDownload({Manifest.Route.ContentSetId, Manifest.PackageUrl, TCHAR_TO_UTF8(*StagingDirectory), Manifest.CompressedSizeBytes, ResumeOffset, ""}, false);
	FHttpRequestPtr Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(UTF8_TO_TCHAR(Manifest.PackageUrl.c_str()));
	Request->SetVerb(TEXT("GET"));
	if (ResumeOffset > 0)
		Request->SetHeader(TEXT("Range"), FString::Printf(TEXT("bytes=%llu-"), ResumeOffset));
	Request->OnProcessRequestComplete().BindUObject(this, &UContentSubsystem::HandleDownloadResponse);
	Impl->OperationStatus = TEXT("content.download_running");
	Impl->DownloadExpectedBytes = Manifest.CompressedSizeBytes;
	Impl->DownloadReceivedBytes = ResumeOffset;
	Impl->Note(FString::Printf(TEXT("download: GET v%s, %llu bytes, resume at %llu"), UTF8_TO_TCHAR(Manifest.Route.SemanticVersion.c_str()), Manifest.CompressedSizeBytes, ResumeOffset));
	if (!Request->ProcessRequest())
	{
		Impl->OperationStatus = TEXT("content.download_network_failed");
		return false;
	}
	return true;
}

void UContentSubsystem::HandleDownloadResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSucceeded)
{
	if (!Impl || !Impl->CatalogManifest || !bSucceeded || !Response.IsValid() || !CanOperateContent())
	{
		if (Impl)
		{
			Impl->PackageHttpCode = Response.IsValid() ? Response->GetResponseCode() : 0;
			Impl->OperationStatus = TEXT("content.download_network_failed");
			Impl->Note(FString::Printf(TEXT("download: request failed (%s, HTTP %d)"), bSucceeded ? TEXT("connected") : TEXT("no response"), Impl->PackageHttpCode));
		}
		return;
	}
	try
	{
		const ContentRuntime::FContentManifest Manifest = *Impl->CatalogManifest;
		const FString StagingDirectory = FPaths::Combine(Impl->ContentRoot, TEXT("staging"), UTF8_TO_TCHAR(Manifest.Route.ContentSetId.c_str()));
		const FString PackagePath = FPaths::Combine(StagingDirectory, TEXT("package.vircontent"));
		const bool bRequestedRange = Request->GetHeader(TEXT("Range")).Len() > 0;
		const int32 ResponseCode = Response->GetResponseCode();
		Impl->PackageHttpCode = ResponseCode;
		Impl->Note(FString::Printf(TEXT("download: HTTP %d, %d bytes"), ResponseCode, Response->GetContent().Num()));
		if (ResponseCode != 206 && ResponseCode != 200)
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::IoFailure, "package request failed");
		const bool bAppend = bRequestedRange && ResponseCode == 206;
		const TArray<uint8> &Bytes = Response->GetContent();
		if (Bytes.IsEmpty() || !FFileHelper::SaveArrayToFile(Bytes, *PackagePath, &IFileManager::Get(), bAppend ? FILEWRITE_Append : 0))
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::IoFailure, "cannot write staged package");
		const uint64 ReceivedSize = static_cast<uint64>(IFileManager::Get().FileSize(*PackagePath));
		Impl->DownloadReceivedBytes = ReceivedSize;
		Impl->Repository->SaveDownload({Manifest.Route.ContentSetId, Manifest.PackageUrl, TCHAR_TO_UTF8(*StagingDirectory), Manifest.CompressedSizeBytes, ReceivedSize, TCHAR_TO_UTF8(*Response->GetHeader(TEXT("ETag")))}, false);
		if (ReceivedSize != Manifest.CompressedSizeBytes)
		{
			Impl->OperationStatus = TEXT("content.download_partial");
			return;
		}
		const std::string Inventory = ContentRuntime::ReadStagedPackageInventory(std::filesystem::path(TCHAR_TO_UTF8(*StagingDirectory)));
		const auto Validated = ContentRuntime::ValidateStagedPackage(std::filesystem::path(TCHAR_TO_UTF8(*StagingDirectory)), Manifest, Inventory);
		Impl->Note(TEXT("download: size, digest, inventory and notice verified"));
		// Refuse before extraction, which would otherwise replace retained files on disk
		// and only then be rejected by the repository, leaving record and files disagreeing.
		if (Impl->IsVersionRetained(Manifest))
		{
			Impl->OperationStatus = TEXT("content.version_already_installed");
			Impl->Note(FString::Printf(TEXT("download: v%s is already retained; not overwriting"), UTF8_TO_TCHAR(Manifest.Route.SemanticVersion.c_str())));
			return;
		}
		const FString InstallPath = FPaths::Combine(Impl->ContentRoot, TEXT("installed"), UTF8_TO_TCHAR(Manifest.Route.ContentSetId.c_str()), UTF8_TO_TCHAR(Manifest.Route.SemanticVersion.c_str()));
		ContentRuntime::ExtractValidatedPackage(Validated, std::filesystem::path(TCHAR_TO_UTF8(*InstallPath)));
		Impl->Note(FString::Printf(TEXT("download: extracted to installed/%s/%s"), UTF8_TO_TCHAR(Manifest.Route.ContentSetId.c_str()), UTF8_TO_TCHAR(Manifest.Route.SemanticVersion.c_str())));
		Impl->Repository->SaveStaged({Manifest.Route.ContentSetId, Manifest.Route.RouteId, Manifest.Route.SemanticVersion, ContentRuntime::Sha256Hex(Manifest.ManifestSha256), ContentRuntime::EInstalledContentState::Staged, Manifest.CatalogRevision, Manifest.IssuedAtUnixSeconds, Manifest.ExpiresAtUnixSeconds, TCHAR_TO_UTF8(*InstallPath), ""}, false);
		Impl->Repository->MarkVerified(Manifest.Route.ContentSetId, false);
		Impl->Repository->RemoveDownload(Manifest.Route.ContentSetId, false);
		Impl->OperationStatus = TEXT("content.download_verified_restart_required");
		Impl->Note(TEXT("download: recorded as verified; relaunch to activate"));
	}
	catch (const ContentRuntime::FContentValidationError &Error)
	{
		Impl->OperationStatus = UTF8_TO_TCHAR(ContentRuntime::ContentErrorName(Error.GetCode()));
		Impl->Note(FString::Printf(TEXT("download: rejected: %s"), UTF8_TO_TCHAR(Error.what())));
	}
	catch (const std::exception &Error)
	{
		Impl->OperationStatus = TEXT("content.download_validation_failed");
		Impl->Note(FString::Printf(TEXT("download: error: %s"), UTF8_TO_TCHAR(Error.what())));
	}
}

bool UContentSubsystem::MountInstalledContentForTesting(const FString &InstallPath)
{
	FString Failure;
	const bool bMounted = TryMountInstalledContent(InstallPath, Failure);
	if (!bMounted && Impl)
		Impl->HanAvailabilityReason = Failure;
	return bMounted;
}

bool UContentSubsystem::TryMountInstalledContent(const FString &InstallPath, FString &OutFailureCategory)
{
	if (!CanOperateContent())
	{
		OutFailureCategory = TEXT("content.workout_active");
		return false;
	}
	const FString PakPath = FPaths::Combine(InstallPath, TEXT("HanRiver.pak"));
	const FString UtocPath = FPaths::Combine(InstallPath, TEXT("HanRiver.utoc"));
	const FString UcasPath = FPaths::Combine(InstallPath, TEXT("HanRiver.ucas"));
	const FString RoutePath = FPaths::Combine(InstallPath, TEXT("route.pb"));
	IPlatformFile &FileSystem = FPlatformFileManager::Get().GetPlatformFile();
	if (!FileSystem.FileExists(*PakPath) || !FileSystem.FileExists(*UtocPath) || !FileSystem.FileExists(*UcasPath) || !FileSystem.FileExists(*RoutePath))
	{
		OutFailureCategory = TEXT("content.mount_missing_iostore");
		if (Impl)
			Impl->Note(FString::Printf(TEXT("mount: missing file(s) pak=%d utoc=%d ucas=%d route=%d"), FileSystem.FileExists(*PakPath), FileSystem.FileExists(*UtocPath), FileSystem.FileExists(*UcasPath), FileSystem.FileExists(*RoutePath)));
		return false;
	}
	IPlatformFile *PlatformFile = FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile"));
	FPakPlatformFile *PakPlatformFile = static_cast<FPakPlatformFile *>(PlatformFile);
	if (!PakPlatformFile)
	{
		OutFailureCategory = TEXT("content.mount_failed");
		if (Impl)
			Impl->Note(TEXT("mount: PakFile platform file is unavailable"));
		return false;
	}
	FPakMountArgs MountArgs;
	MountArgs.PakFilename = *PakPath;
	MountArgs.PakOrder = 100;
	FIoStatus MountStatus;
	if (!PakPlatformFile->Mount(MountArgs, &MountStatus))
	{
		OutFailureCategory = TEXT("content.mount_failed");
		if (Impl)
			Impl->Note(FString::Printf(TEXT("mount: HanRiver.pak refused (%s, system error %u)"), GetIoErrorText(MountStatus.GetErrorCode()), MountStatus.GetSystemErrorCode()));
		return false;
	}
	if (Impl)
		Impl->MountedInstallPath = InstallPath;
	return true;
}

bool UContentSubsystem::IsWorkoutActive() const
{
	const UGameInstance *GameInstance = GetGameInstance();
	const UWorkoutSubsystem *Workout = GameInstance ? GameInstance->GetSubsystem<UWorkoutSubsystem>() : nullptr;
	const FWorkoutSnapshot *Snapshot = Workout ? Workout->GetSnapshot() : nullptr;
	return Snapshot && Snapshot->State == ERowingSessionState::Active;
}
