#include "Modules/ModuleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"

#include "NativeLinkageProbe.h"
#include "VirDebugLog.h"

#if PLATFORM_MAC
#include "ToolchainBluetoothProbe.h"
#endif

class FVirtualRowingModule final : public IModuleInterface
{
  public:
	virtual void StartupModule() override
	{
		// TEMPORARY Milestone 3 probe-invocation diagnostic (2026-09-17): see VirDebugLog.h.
		VirDebugLog(TEXT("StartupModule entered"));
		VirDebugLog(FString::Printf(TEXT("CommandLine=[%s]"), FCommandLine::Get()));
		FCoreDelegates::GetOnPostEngineInit().AddLambda([]()
												   { VirDebugLog(TEXT("OnPostEngineInit fired")); });
		if (FParse::Param(FCommandLine::Get(), TEXT("NativeLinkageProbe")))
		{
			VirDebugLog(FString::Printf(TEXT("VirNativeLinkageProbe()=%s"), VirNativeLinkageProbe() ? TEXT("true") : TEXT("false")));
		}
#if PLATFORM_MAC
		const bool bProbeRequested = FParse::Param(FCommandLine::Get(), TEXT("ToolchainBluetoothProbe"));
		VirDebugLog(FString::Printf(TEXT("FParse::Param(ToolchainBluetoothProbe)=%s"), bProbeRequested ? TEXT("true") : TEXT("false")));
		if (bProbeRequested)
		{
			VirDebugLog(TEXT("Calling FToolchainBluetoothProbe::Run()"));
			FToolchainBluetoothProbe::Run();
			VirDebugLog(TEXT("FToolchainBluetoothProbe::Run() returned; calling RequestExit(true)"));
			// Force=true: the probe result is already flushed to disk by Run() above, and this
			// diagnostic mode must not fall through to normal engine startup (map load, game
			// window). A soft RequestExit(false) only sets a flag the main loop checks later,
			// which lets a game window appear first and never reliably reaches that check.
			FPlatformMisc::RequestExit(true);
		}
#endif
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FVirtualRowingModule, VirtualRowing, "VirtualRowing");
