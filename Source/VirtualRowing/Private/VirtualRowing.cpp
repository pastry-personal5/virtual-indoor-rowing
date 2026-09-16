#include "Modules/ModuleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if PLATFORM_MAC
#include "ToolchainBluetoothProbe.h"
#endif

class FVirtualRowingModule final : public IModuleInterface
{
  public:
	virtual void StartupModule() override
	{
#if PLATFORM_MAC
		if (FParse::Param(FCommandLine::Get(), TEXT("ToolchainBluetoothProbe")))
		{
			FToolchainBluetoothProbe::Run();
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
