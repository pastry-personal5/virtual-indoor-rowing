#include "ContentSubsystem.h"

#include "WorkoutSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "IPlatformFilePak.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include "LocalData/ContentRepository.h"
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
			{"content-current", PublicKey({0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a})},
			{"content-next", PublicKey({0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7, 0x4d, 0x1b, 0x7e, 0xbc, 0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c})},
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
	std::unique_ptr<LocalData::FContentRepository> Repository;
	FString MountedInstallPath;
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
					return true;
				}
			}
			catch (const ContentRuntime::FContentValidationError &Error)
			{
				Failure = UTF8_TO_TCHAR(ContentRuntime::ContentErrorName(Error.GetCode()));
			}
			if (Failure.IsEmpty())
				Failure = TEXT("content.mount_failed");
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
		}
	}
	catch (const std::exception &Error)
	{
		UE_LOG(LogContentSubsystem, Warning, TEXT("Content boot failed; using Standard route: %s"), UTF8_TO_TCHAR(Error.what()));
		Impl->SelectedRoute = Impl->StandardRoute;
		Impl->HanAvailabilityReason = TEXT("content.han.validation_failed");
	}
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

FString UContentSubsystem::GetActivePackageNotice() const
{
	return Impl ? Impl->ActivePackageNotice : FString();
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
		return false;
	}
	IPlatformFile *PlatformFile = FPlatformFileManager::Get().FindPlatformFile(TEXT("PakFile"));
	FPakPlatformFile *PakPlatformFile = static_cast<FPakPlatformFile *>(PlatformFile);
	if (!PakPlatformFile || !PakPlatformFile->Mount(*PakPath, 100, nullptr))
	{
		OutFailureCategory = TEXT("content.mount_failed");
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
