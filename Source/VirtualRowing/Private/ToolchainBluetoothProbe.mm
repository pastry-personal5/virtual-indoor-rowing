#include "ToolchainBluetoothProbe.h"

// Carbon's NumberFormatting.h declares a global FVector, which conflicts with
// Unreal's LWC FVector alias. Rename only while importing Apple framework headers.
#define FVector FVector_AppleFramework
#import <CoreBluetooth/CoreBluetooth.h>
#undef FVector

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

@interface FToolchainBluetoothProbeDelegate : NSObject <CBCentralManagerDelegate>
{
  @public
	dispatch_semaphore_t Finished;
	dispatch_queue_t Queue;
	CBCentralManager *Central;
	FString Result;
	bool bDidStartScan;
}
@end

@implementation FToolchainBluetoothProbeDelegate

- (instancetype)init
{
	self = [super init];
	if (self)
	{
		Finished = dispatch_semaphore_create(0);
		Queue = nil;
		Central = nil;
		Result = TEXT("timeout");
		bDidStartScan = false;
	}
	return self;
}

- (void)centralManagerDidUpdateState:(CBCentralManager *)central
{
	switch (central.state)
	{
	case CBManagerStatePoweredOn:
		if (!bDidStartScan)
		{
			bDidStartScan = true;
			Result = TEXT("authorized_powered_on");
			[central scanForPeripheralsWithServices:nil options:nil];
			dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC), Queue, ^{
			  [central stopScan];
			  dispatch_semaphore_signal(Finished);
			});
		}
		break;
	case CBManagerStateUnauthorized:
		Result = [CBManager authorization] == CBManagerAuthorizationRestricted ? TEXT("restricted") : TEXT("denied");
		dispatch_semaphore_signal(Finished);
		break;
	case CBManagerStateUnsupported:
		Result = TEXT("unsupported");
		dispatch_semaphore_signal(Finished);
		break;
	case CBManagerStatePoweredOff:
		Result = TEXT("powered_off");
		dispatch_semaphore_signal(Finished);
		break;
	case CBManagerStateResetting:
		Result = TEXT("corebluetooth_error");
		dispatch_semaphore_signal(Finished);
		break;
	case CBManagerStateUnknown:
		break;
	}
}

@end

namespace
{
	FString JsonEscape(const FString &Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return Escaped;
	}
} // namespace

void FToolchainBluetoothProbe::Run()
{
	const double Started = FPlatformTime::Seconds();
	dispatch_queue_t Queue = dispatch_queue_create("dev.virtualrowing.toolchain-bluetooth-probe", DISPATCH_QUEUE_SERIAL);
	FToolchainBluetoothProbeDelegate *Delegate = [FToolchainBluetoothProbeDelegate new];
	Delegate->Queue = Queue;
	dispatch_async(Queue, ^{
	  Delegate->Central = [[CBCentralManager alloc] initWithDelegate:Delegate queue:Queue options:nil];
	});
	const long WaitResult = dispatch_semaphore_wait(Delegate->Finished, dispatch_time(DISPATCH_TIME_NOW, 16 * NSEC_PER_SEC));
	if (WaitResult != 0)
	{
		Delegate->Result = TEXT("timeout");
	}
	const double DurationSeconds = FPlatformTime::Seconds() - Started;
	const FString FileName = FString::Printf(
		TEXT("toolchain-bluetooth-probe-%s-%s.json"),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString Json = FString::Printf(
		TEXT("{\n  \"schema_version\": 1,\n  \"source_revision\": \"%s\",\n  \"toolchain_fingerprint\": \"%s\",\n  \"timestamp_utc\": \"%s\",\n  \"result_state\": \"%s\",\n  \"duration_ms\": %lld\n}\n"),
		*JsonEscape(VIR_SOURCE_REVISION),
		*JsonEscape(VIR_TOOLCHAIN_FINGERPRINT),
		*FDateTime::UtcNow().ToIso8601(),
		*JsonEscape(Delegate->Result),
		static_cast<long long>(DurationSeconds * 1000.0));
	FString ResultDirectory;
	FParse::Value(FCommandLine::Get(), TEXT("ToolchainBluetoothProbeResultDir="), ResultDirectory);
	if (ResultDirectory.IsEmpty())
	{
		ResultDirectory = FPaths::ProjectLogDir();
	}
	const FString Path = FPaths::Combine(ResultDirectory, FileName);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FFileHelper::SaveStringToFile(Json, *Path);
}
