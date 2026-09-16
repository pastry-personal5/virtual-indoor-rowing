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
			FPlatformMisc::RequestExit(false);
		}
#endif
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FVirtualRowingModule, VirtualRowing, "VirtualRowing");
