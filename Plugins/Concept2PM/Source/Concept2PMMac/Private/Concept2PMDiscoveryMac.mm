#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include "Concept2PMDiscoveryProfiles.h"
#include "Concept2PMMac/Concept2PMDiscoveryFactory.h"
#include "Concept2PMMac/Concept2PMRunDiagnostics.h"
#include "PM5CapabilityProfiles.generated.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{
	NSArray<CBUUID *> *IdentityCharacteristicUuids()
	{
		return @[
			[CBUUID UUIDWithString:@"CE060011-43E5-11E4-916C-0800200C9A66"],
			[CBUUID UUIDWithString:@"CE060013-43E5-11E4-916C-0800200C9A66"],
			[CBUUID UUIDWithString:@"CE060014-43E5-11E4-916C-0800200C9A66"],
			[CBUUID UUIDWithString:@"CE060015-43E5-11E4-916C-0800200C9A66"],
			[CBUUID UUIDWithString:@"CE060016-43E5-11E4-916C-0800200C9A66"],
		];
	}

	bool IsUuid(CBUUID *Uuid, NSString *Expected)
	{
		return
			[Uuid.UUIDString caseInsensitiveCompare:Expected] == NSOrderedSame;
	}

	ERowingMachineKind DecodeMachineKind(std::uint8_t Value)
	{
		switch (Value)
		{
		case 0:
		case 1:
		case 2:
		case 3:
		case 5:
		case 7:
		case 8:
		case 16:
		case 17:
		case 18:
		case 19:
		case 20:
		case 32:
		case 64:
		case 224:
			return ERowingMachineKind::IndoorRower;
		case 128:
		case 143:
		case 225:
			return ERowingMachineKind::SkiErg;
		case 192:
		case 193:
		case 194:
		case 207:
		case 226:
			return ERowingMachineKind::BikeErg;
		default:
			return ERowingMachineKind::Unknown;
		}
	}

	std::string ReadBoundedUtf8(NSData *Data)
	{
		if (Data == nil || Data.length > RowingDeviceInfoTextMaxUtf8Bytes)
			return {};
		NSString *Value = [[NSString alloc] initWithData:Data
												encoding:NSUTF8StringEncoding];
		if (Value == nil)
			return {};
		const char *Utf8 = Value.UTF8String;
		return Utf8 == nullptr ? std::string{} : std::string(Utf8);
	}

	std::uint64_t MonotonicNowNs()
	{
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
				.count());
	}

	NSString *CharacteristicUuid(std::uint16_t ShortId)
	{
		return [NSString stringWithFormat:@"CE06%04X-43E5-11E4-916C-0800200C9A66",
										  static_cast<unsigned int>(ShortId)];
	}

	std::uint16_t ShortIdFromUuid(CBUUID *Uuid)
	{
		NSString *Value = Uuid.UUIDString.uppercaseString;
		if (Value.length != 36 ||
			[Value compare:@"CE06"
				   options:NSLiteralSearch
					 range:NSMakeRange(0, 4)] != NSOrderedSame)
			return 0;
		unsigned int ShortId = 0;
		NSScanner *Scanner = [NSScanner scannerWithString:
											[Value substringWithRange:NSMakeRange(4, 4)]];
		if (![Scanner scanHexInt:&ShortId] || !Scanner.isAtEnd || ShortId > 0xFFFFU)
			return 0;
		return static_cast<std::uint16_t>(ShortId);
	}

	std::uint8_t ToProfileProperties(CBCharacteristicProperties Properties)
	{
		std::uint8_t Result =
			Concept2PM::ToPM5CharacteristicProperties(
				Concept2PM::EPM5CharacteristicProperty::None);
		const auto AddIfPresent = [&Result, Properties](
									  CBCharacteristicProperties Native,
									  Concept2PM::EPM5CharacteristicProperty Profile)
		{
			if ((Properties & Native) != 0)
				Result |= Concept2PM::ToPM5CharacteristicProperties(Profile);
		};
		AddIfPresent(CBCharacteristicPropertyRead,
					 Concept2PM::EPM5CharacteristicProperty::Read);
		AddIfPresent(CBCharacteristicPropertyWrite,
					 Concept2PM::EPM5CharacteristicProperty::Write);
		AddIfPresent(CBCharacteristicPropertyWriteWithoutResponse,
					 Concept2PM::EPM5CharacteristicProperty::WriteWithoutResponse);
		AddIfPresent(CBCharacteristicPropertyNotify,
					 Concept2PM::EPM5CharacteristicProperty::Notify);
		AddIfPresent(CBCharacteristicPropertyIndicate,
					 Concept2PM::EPM5CharacteristicProperty::Indicate);
		return Result;
	}

	std::vector<Concept2PM::FPM5CapabilityProfile> AdapterPrivateProfiles()
	{
		Concept2PM::FPM5CapabilityProfile Profile;
		Profile.Version = 0;
		Profile.MonitorModel = "PM5";
		Profile.HardwareRevision = "634";
		Profile.FirmwareRevision = "8200-000372-178.069";
		Profile.MachineKind = ERowingMachineKind::IndoorRower;
		Profile.SupportState = ERowingMachineSupportState::Warn;
		Profile.DiagnosticOnly = true;
		Profile.Characteristics = {
			{Concept2PM::GeneralStatus,
			 true,
			 true,
			 Concept2PM::ToPM5CharacteristicProperties(
				 Concept2PM::EPM5CharacteristicProperty::Notify),
			 {19},
			 ToRowingMetricSet(ERowingMetric::None)},
			{Concept2PM::AdditionalStatus1,
			 true,
			 true,
			 Concept2PM::ToPM5CharacteristicProperties(
				 Concept2PM::EPM5CharacteristicProperty::Notify),
			 {17},
			 ToRowingMetricSet(ERowingMetric::None)},
			{Concept2PM::StrokeData,
			 false,
			 true,
			 Concept2PM::ToPM5CharacteristicProperties(
				 Concept2PM::EPM5CharacteristicProperty::Notify),
			 {20},
			 ToRowingMetricSet(ERowingMetric::None)},
			{Concept2PM::AdditionalStrokeData,
			 false,
			 true,
			 Concept2PM::ToPM5CharacteristicProperties(
				 Concept2PM::EPM5CharacteristicProperty::Notify),
			 {18},
			 ToRowingMetricSet(ERowingMetric::None)}};
		return {std::move(Profile)};
	}

	constexpr std::size_t MaxQueuedEvents = 512;
	constexpr std::uint64_t HandshakeTimeoutNs = 10'000'000'000ULL;
	constexpr std::uint64_t StaleTelemetryNs = 500'000'000ULL;
	constexpr std::uint64_t ReconnectTelemetryNs = 1'500'000'000ULL;
	constexpr std::uint32_t MaxReconnectAttempts = 3;
	NSString *const RememberedPeripheralDefaultsKey =
		@"dev.virtualrowing.pm5-diagnostic.remembered-peripheral.v2";
	NSString *const LegacyRememberedPeripheralDefaultsKey =
		@"dev.virtualrowing.pm5-diagnostic.remembered-peripheral.v1";
	constexpr NSInteger RememberedPeripheralSchemaVersion = 2;

	struct FRememberedPeripheral
	{
		NSUUID *Identifier = nil;
		Concept2PM::FPM5Identity Identity;
	};

	void SaveRememberedPeripheral(CBPeripheral *Peripheral,
								  const Concept2PM::FPM5Identity &Identity)
	{
		if (Peripheral == nil || Peripheral.identifier.UUIDString == nil)
			return;
		NSString *const Model = [NSString stringWithUTF8String:Identity.MonitorModel.c_str()];
		NSString *const Hardware = [NSString stringWithUTF8String:Identity.HardwareRevision.c_str()];
		NSString *const Firmware = [NSString stringWithUTF8String:Identity.FirmwareRevision.c_str()];
		if (Model == nil || Hardware == nil || Firmware == nil ||
			Model.length == 0 || Hardware.length == 0 || Firmware.length == 0 ||
			Identity.MachineKind != ERowingMachineKind::IndoorRower)
			return;
		NSDictionary *const Record = @{
			@"schema_version" : @(RememberedPeripheralSchemaVersion),
			@"peripheral_uuid" : Peripheral.identifier.UUIDString,
			@"model" : Model,
			@"hardware_revision" : Hardware,
			@"firmware_revision" : Firmware,
			@"machine_kind" : @(static_cast<std::uint8_t>(Identity.MachineKind))
		};
		NSUserDefaults *const Defaults = [NSUserDefaults standardUserDefaults];
		[Defaults setObject:Record forKey:RememberedPeripheralDefaultsKey];
		[Defaults removeObjectForKey:LegacyRememberedPeripheralDefaultsKey];
	}

	std::optional<FRememberedPeripheral> LoadRememberedPeripheral()
	{
		NSUserDefaults *const Defaults = [NSUserDefaults standardUserDefaults];
		id const Stored = [Defaults objectForKey:RememberedPeripheralDefaultsKey];
		if (![Stored isKindOfClass:[NSDictionary class]])
		{
			[Defaults removeObjectForKey:RememberedPeripheralDefaultsKey];
			[Defaults removeObjectForKey:LegacyRememberedPeripheralDefaultsKey];
			return std::nullopt;
		}
		NSDictionary *const Record = (NSDictionary *)Stored;
		NSNumber *const Version = Record[@"schema_version"];
		NSString *const IdentifierString = Record[@"peripheral_uuid"];
		NSString *const Model = Record[@"model"];
		NSString *const Hardware = Record[@"hardware_revision"];
		NSString *const Firmware = Record[@"firmware_revision"];
		NSNumber *const Kind = Record[@"machine_kind"];
		NSUUID *const Identifier = [IdentifierString isKindOfClass:[NSString class]]
									   ? [[NSUUID alloc] initWithUUIDString:IdentifierString]
									   : nil;
		const bool Valid = [Version isKindOfClass:[NSNumber class]] &&
						   Version.integerValue == RememberedPeripheralSchemaVersion &&
						   Identifier != nil && [Model isKindOfClass:[NSString class]] && Model.length > 0 &&
						   [Hardware isKindOfClass:[NSString class]] && Hardware.length > 0 &&
						   [Firmware isKindOfClass:[NSString class]] && Firmware.length > 0 &&
						   [Kind isKindOfClass:[NSNumber class]] &&
						   Kind.unsignedCharValue == static_cast<std::uint8_t>(ERowingMachineKind::IndoorRower);
		if (!Valid)
		{
			[Defaults removeObjectForKey:RememberedPeripheralDefaultsKey];
			[Defaults removeObjectForKey:LegacyRememberedPeripheralDefaultsKey];
			return std::nullopt;
		}
		FRememberedPeripheral Result;
		Result.Identifier = Identifier;
		Result.Identity = {Model.UTF8String, Hardware.UTF8String, Firmware.UTF8String, static_cast<ERowingMachineKind>(Kind.unsignedCharValue)};
		[Defaults removeObjectForKey:LegacyRememberedPeripheralDefaultsKey];
		return Result;
	}

	void EnqueueEvent(std::deque<FRowingMachineEvent> &Events,
					  std::uint64_t &Sequence,
					  std::uint64_t &LastTimestampNs,
					  FRowingMachineEventPayload Payload)
	{
		const std::uint64_t Now = MonotonicNowNs();
		LastTimestampNs = std::max(LastTimestampNs + 1ULL, Now);
		Events.push_back({++Sequence, LastTimestampNs, std::move(Payload)});
	}
} // namespace

class FMacMachine;

class FMacDiscovery final : public IConcept2PMDiscovery
{
  public:
	explicit FMacDiscovery(
		std::vector<Concept2PM::FPM5CapabilityProfile> InProfiles,
		FPM5HardwareProbeConfiguration InProbeConfiguration);
	~FMacDiscovery() override;

	FRowingCommandResult StartScan() override;
	FRowingCommandResult StopScan() override;
	bool TryPollDiscoveryEvent(FRowingMachineEvent &OutEvent) override;
	std::unique_ptr<IRowingMachine> TryTakeRelaunchMachine() override;
	void ForgetRememberedMachine() override;
	std::unique_ptr<IRowingMachine>
	CreateMachine(const FRowingMachineId &MachineId) override;

	void OnDiscovered(CBPeripheral *Peripheral, NSInteger Rssi);
	void OnCentralState(CBManagerState State);
	void OnConnected(CBPeripheral *Peripheral);
	void OnConnectionFailed(CBPeripheral *Peripheral, NSError *Error);
	void OnDisconnected(CBPeripheral *Peripheral, NSError *Error);
	void ClearActive(FMacMachine *Machine);
	void RememberWhenReady(CBPeripheral *Peripheral,
						   const Concept2PM::FPM5Identity &Identity);

  private:
	struct FDiscoveredPeripheral
	{
		FRowingMachineId Id;
		std::size_t RetainedIndex = 0;
	};

	void Emit(FRowingMachineEventPayload Payload);
	void Transition(ERowingConnectionState NewState,
					ERowingConnectionReason Reason);
	void EmitScanFault(ERowingFaultCode Code, const char *Diagnostic);
	void ScheduleScanTimeout(ERowingFaultCode Code,
							 const char *Diagnostic,
							 bool OnlyIfNoCandidate);
	void TryReconnectRememberedPeripheral(CBCentralManager *Central);

	std::vector<Concept2PM::FPM5CapabilityProfile> Profiles;
	FPM5HardwareProbeConfiguration ProbeConfiguration;
	id CentralDelegate = nil;
	dispatch_queue_t Queue = nil;
	std::mutex EventMutex;
	std::deque<FRowingMachineEvent> Events;
	std::vector<FDiscoveredPeripheral> Discovered;
	std::vector<FRowingMachineId> SeenThisScan;
	std::shared_ptr<int> LifetimeToken = std::make_shared<int>(0);
	FMacMachine *ActiveMachine = nullptr;
	std::unique_ptr<FMacMachine> RelaunchMachine;
	std::uint64_t EventSequence = 0;
	std::uint64_t LastEventTimestampNs = 0;
	bool QueueOverflowed = false;
	std::uint64_t ScanGeneration = 0;
	std::atomic_bool ScanRequested = false;
	bool Scanning = false;
	bool ReadinessTimeoutPending = false;
	bool RelaunchAttempted = false;
	ERowingConnectionState State = ERowingConnectionState::Idle;
};

class FMacMachine final : public IRowingMachine,
						  public IConcept2PMRunDiagnostics
{
  public:
	FMacMachine(FMacDiscovery &InDiscovery,
				CBCentralManager *InCentral,
				dispatch_queue_t InQueue,
				CBPeripheral *InPeripheral,
				std::vector<Concept2PM::FPM5CapabilityProfile> InProfiles,
				FPM5HardwareProbeConfiguration InProbeConfiguration);
	~FMacMachine() override;

	FRowingCommandResult Connect() override;
	FRowingCommandResult Disconnect() override;
	void StartRelaunchReconnect();
	void ExpectRelaunchIdentity(Concept2PM::FPM5Identity Identity);
	ERowingConnectionState GetConnectionState() const override;
	FRowingMachineDiagnostics GetDiagnostics() const override;
	FPM5RunDiagnostics GetPM5RunDiagnostics() const override;
	bool TryPollPM5ProbePacket(FPM5ProbePacketEvidence &OutEvidence) override;
	void FinalizePM5ProbeCapture() override;
	bool TryPollEvent(FRowingMachineEvent &OutEvent) override;
	void RecordCallbackFailure(EPM5CallbackStage Stage,
							   std::uint16_t Characteristic,
							   NSError *Error);

	void OnConnected();
	void OnConnectionFailed(NSError *Error);
	void OnDisconnected(NSError *Error);
	void OnServicesDiscovered(NSError *Error);
	void OnCharacteristicsDiscovered(CBService *Service, NSError *Error);
	void OnCharacteristicValue(CBCharacteristic *Characteristic,
							   NSError *Error);
	void OnNotificationState(CBCharacteristic *Characteristic,
							 NSError *Error);
	void OnStatusRateWritten(CBCharacteristic *Characteristic, NSError *Error);
	bool OwnsPeripheral(CBPeripheral *Peripheral) const;

  private:
	void Transition(ERowingConnectionState NewState,
					ERowingConnectionReason Reason);
	void Emit(FRowingMachineEventPayload Payload);
	void EmitFault(ERowingFaultCode Code,
				   ERowingFaultSeverity Severity,
				   ERowingOperation Operation,
				   const char *Diagnostic,
				   std::optional<std::uint64_t> ExpectedValue = std::nullopt,
				   std::optional<std::uint64_t> ActualValue = std::nullopt);
	void FinishIdentity();
	void FinishTelemetryIfReady();
	void ReceiveTelemetryPacket(std::uint16_t ShortId,
								const std::vector<std::uint8_t> &Bytes,
								std::uint64_t ReceivedMonotonicNs,
								ERowingConnectionState ConnectionStateAtReceive);
	void PublishAvailableMetrics(
		const Concept2PM::FPM5CharacteristicProfile &Characteristic);
	void EnqueueNotification(CBCharacteristic *Characteristic);
	void ScheduleDecoder();
	void DrainNotifications();
	void FailAcquisitionOverflow();
	void CaptureProbePacket(
		std::uint16_t ShortId,
		const std::vector<std::uint8_t> &Bytes,
		std::uint64_t ReceivedMonotonicNs,
		ERowingConnectionState PacketConnectionState,
		Concept2PM::EPacketError ParserResult,
		const std::vector<std::size_t> &ApprovedPacketLengths,
		std::uint64_t CharacteristicSequence);
	void ArmHandshakeTimeout();
	void ArmLivenessTimeout();
	void BeginReconnect();
	void ScheduleReconnectAttempt();
	void StartReconnectAttempt();
	void ResetHandshakeState();
	void FailHandshakeTimeout();
	bool IsSupportedTelemetryCharacteristic(std::uint16_t ShortId) const;
	bool ConfirmReconnectContinuation(
		const Concept2PM::FGeneralStatusFact &General);
	const Concept2PM::FPM5CharacteristicProfile *
	FindProfileCharacteristic(std::uint16_t ShortId) const;

	FMacDiscovery &Discovery;
	CBCentralManager *Central = nil;
	dispatch_queue_t Queue = nil;
	dispatch_queue_t DecoderQueue = nil;
	id PeripheralDelegate = nil;
	std::vector<Concept2PM::FPM5CapabilityProfile> Profiles;
	FPM5HardwareProbeConfiguration ProbeConfiguration;
	mutable std::mutex Mutex;
	std::deque<FRowingMachineEvent> Events;
	ERowingConnectionState State = ERowingConnectionState::Idle;
	std::uint64_t EventSequence = 0;
	std::uint64_t LastEventTimestampNs = 0;
	bool QueueOverflowed = false;
	std::uint32_t EventQueueHighWaterMark = 0;
	std::uint64_t EventQueueOverflowCount = 0;
	std::uint64_t LastPublishedSampleSequence = 0;
	std::uint64_t LastEmittedMetricSampleSequence = 0;
	std::string Model;
	std::string Hardware;
	std::string Firmware;
	std::string Manufacturer;
	std::optional<ERowingMachineKind> MachineKind;
	bool IdentityFinished = false;
	const Concept2PM::FPM5CapabilityProfile *ActiveProfile = nullptr;
	std::set<std::uint16_t> RequiredNotifyCharacteristics;
	std::set<std::uint16_t> OptionalNotifyCharacteristics;
	std::set<std::uint16_t> EnabledNotifyCharacteristics;
	std::set<std::uint16_t> SeenRequiredCharacteristics;
	Concept2PM::FTelemetryMerger TelemetryMerger;
	std::optional<std::uint64_t> LastGeneralStatusNs;
	struct FReconnectBaseline
	{
		std::uint64_t ElapsedMs = 0;
		std::uint64_t DistanceMm = 0;
		ERowingWorkoutState WorkoutState = ERowingWorkoutState::Unknown;
	};
	std::optional<FReconnectBaseline> ReconnectBaseline;
	bool ReconnectContinuationConfirmed = false;
	bool StatusRateWriteRequired = false;
	bool StatusRateConfigured = false;
	std::uint64_t StatusRateWriteAttemptCount = 0;
	std::uint64_t StatusRateWriteSuccessCount = 0;
	std::uint64_t StatusRateWriteFailureCount = 0;
	std::uint32_t LastRequestedStatusPeriodMs = 0;
	std::map<std::uint16_t, Concept2PM::FPM5CharacteristicDiagnostics>
		CharacteristicDiagnostics;
	std::map<std::pair<EPM5CallbackStage, std::uint16_t>,
			 FPM5CallbackErrorDiagnostic>
		CallbackErrorDiagnostics;
	std::uint64_t HandshakeGeneration = 0;
	std::uint64_t LivenessGeneration = 0;
	bool ReconnectInProgress = false;
	bool ReconnectConnectInFlight = false;
	bool ReconnectAttemptScheduled = false;
	std::uint32_t ReconnectAttempts = 0;
	std::uint64_t ReconnectStartedNs = 0;
	std::optional<Concept2PM::FPM5Identity> ExpectedReconnectIdentity;
	std::optional<FRowingMachineInfo> LastMachineInfo;
	std::shared_ptr<int> LifetimeToken = std::make_shared<int>(0);
	struct FPendingNotification
	{
		std::uint16_t ShortId = 0;
		std::vector<std::uint8_t> Bytes;
		std::uint64_t ReceivedMonotonicNs = 0;
		ERowingConnectionState ConnectionStateAtReceive =
			ERowingConnectionState::Idle;
	};
	mutable std::mutex AcquisitionMutex;
	std::deque<FPendingNotification> AcquisitionQueue;
	bool DecoderScheduled = false;
	bool AcquisitionOverflowed = false;
	std::uint32_t AcquisitionQueueHighWaterMark = 0;
	std::uint64_t AcquisitionQueueOverflowCount = 0;
	std::deque<FPM5ProbePacketEvidence> ProbeEvidenceQueue;
	FPM5ProbeCaptureDiagnostics ProbeCaptureDiagnostics;
	std::uint64_t ProbePacketSequence = 0;
};

@interface VIRPM5CentralDelegate : NSObject <CBCentralManagerDelegate>
@property(nonatomic, strong) CBCentralManager *Central;
@property(nonatomic, strong) NSMutableArray<CBPeripheral *> *KnownPeripherals;
@property(nonatomic, assign) FMacDiscovery *Owner;
@end

@interface VIRPM5PeripheralDelegate : NSObject <CBPeripheralDelegate>
@property(nonatomic, strong) CBPeripheral *Peripheral;
@property(nonatomic, assign) FMacMachine *Owner;
@end

@implementation VIRPM5CentralDelegate
- (void)centralManagerDidUpdateState:(CBCentralManager *)central
{
	if (self.Owner != nullptr)
		self.Owner->OnCentralState(central.state);
}

- (void)centralManager:(CBCentralManager *)central
	didDiscoverPeripheral:(CBPeripheral *)peripheral
		advertisementData:(NSDictionary<NSString *, id> *)advertisementData
					 RSSI:(NSNumber *)RSSI
{
	(void)central;
	(void)advertisementData;
	if (self.Owner != nullptr)
		self.Owner->OnDiscovered(peripheral, RSSI.integerValue);
}

- (void)centralManager:(CBCentralManager *)central
	didConnectPeripheral:(CBPeripheral *)peripheral
{
	(void)central;
	if (self.Owner != nullptr)
		self.Owner->OnConnected(peripheral);
}

- (void)centralManager:(CBCentralManager *)central
	didFailToConnectPeripheral:(CBPeripheral *)peripheral
						 error:(NSError *)error
{
	(void)central;
	if (self.Owner != nullptr)
		self.Owner->OnConnectionFailed(peripheral, error);
}

- (void)centralManager:(CBCentralManager *)central
	didDisconnectPeripheral:(CBPeripheral *)peripheral
					  error:(NSError *)error
{
	(void)central;
	if (self.Owner != nullptr)
		self.Owner->OnDisconnected(peripheral, error);
}
@end

@implementation VIRPM5PeripheralDelegate
- (void)peripheral:(CBPeripheral *)peripheral
	didDiscoverServices:(NSError *)error
{
	(void)peripheral;
	if (self.Owner != nullptr)
		self.Owner->OnServicesDiscovered(error);
}

- (void)peripheral:(CBPeripheral *)peripheral
	didDiscoverCharacteristicsForService:(CBService *)service
								   error:(NSError *)error
{
	(void)peripheral;
	if (self.Owner != nullptr)
		self.Owner->OnCharacteristicsDiscovered(service, error);
}

- (void)peripheral:(CBPeripheral *)peripheral
	didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
							  error:(NSError *)error
{
	(void)peripheral;
	if (self.Owner != nullptr)
		self.Owner->OnCharacteristicValue(characteristic, error);
}

- (void)peripheral:(CBPeripheral *)peripheral
	didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)characteristic
										  error:(NSError *)error
{
	(void)peripheral;
	if (self.Owner != nullptr)
		self.Owner->OnNotificationState(characteristic, error);
}

- (void)peripheral:(CBPeripheral *)peripheral
	didWriteValueForCharacteristic:(CBCharacteristic *)characteristic
							 error:(NSError *)error
{
	(void)peripheral;
	if (self.Owner != nullptr)
		self.Owner->OnStatusRateWritten(characteristic, error);
}
@end

FMacMachine::FMacMachine(
	FMacDiscovery &InDiscovery,
	CBCentralManager *InCentral,
	dispatch_queue_t InQueue,
	CBPeripheral *InPeripheral,
	std::vector<Concept2PM::FPM5CapabilityProfile> InProfiles,
	FPM5HardwareProbeConfiguration InProbeConfiguration)
	: Discovery(InDiscovery), Central(InCentral), Queue(InQueue),
	  Profiles(std::move(InProfiles)),
	  ProbeConfiguration(std::move(InProbeConfiguration))
{
	ProbeCaptureDiagnostics.Enabled = ProbeConfiguration.CaptureRawTelemetry;
	ProbeCaptureDiagnostics.Active = ProbeConfiguration.CaptureRawTelemetry;
	if (ProbeConfiguration.CaptureRawTelemetry)
	{
		ProbeCaptureDiagnostics.StartedMonotonicNs = MonotonicNowNs();
		ProbeCaptureDiagnostics.MaxCaptureDurationMs =
			ProbeConfiguration.MaxCaptureDurationMs;
		ProbeCaptureDiagnostics.MaxCapturedPacketCount =
			ProbeConfiguration.MaxCapturedPacketCount;
		ProbeCaptureDiagnostics.MaxCapturedPayloadBytes =
			ProbeConfiguration.MaxCapturedPayloadBytes;
		ProbeCaptureDiagnostics.EvidenceQueueCapacity =
			ProbeConfiguration.EvidenceQueueCapacity;
	}
	DecoderQueue = dispatch_queue_create("com.virtualindoorrowing.pm5.decoder",
										 DISPATCH_QUEUE_SERIAL);
	@autoreleasepool
	{
		VIRPM5PeripheralDelegate *Delegate = [VIRPM5PeripheralDelegate new];
		Delegate.Owner = this;
		Delegate.Peripheral = InPeripheral;
		PeripheralDelegate = Delegate;
	}
}

FMacMachine::~FMacMachine()
{
	LifetimeToken.reset();
	if (DecoderQueue != nil)
		dispatch_sync(DecoderQueue, ^{
					  });
	dispatch_sync(Queue, ^{
	  @autoreleasepool
	  {
		  Discovery.ClearActive(this);
		  VIRPM5PeripheralDelegate *Delegate =
			  static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
		  Delegate.Owner = nullptr;
		  Delegate.Peripheral.delegate = nil;
		  if (Central != nil && Delegate.Peripheral != nil)
			  [Central cancelPeripheralConnection:Delegate.Peripheral];
	  }
	});
}

FRowingCommandResult FMacMachine::Connect()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (State != ERowingConnectionState::Idle)
		return {ERowingCommandResultCode::InvalidCurrentState};
	Transition(ERowingConnectionState::Connecting,
			   ERowingConnectionReason::UserRequested);
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	dispatch_async(Queue, ^{
	  [Central connectPeripheral:Delegate.Peripheral options:nil];
	});
	return {ERowingCommandResultCode::Accepted};
}

FRowingCommandResult FMacMachine::Disconnect()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (State == ERowingConnectionState::Idle)
		return {ERowingCommandResultCode::InvalidCurrentState};
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	dispatch_async(Queue, ^{
	  [Central cancelPeripheralConnection:Delegate.Peripheral];
	});
	Transition(ERowingConnectionState::Idle,
			   ERowingConnectionReason::UserRequested);
	return {ERowingCommandResultCode::Accepted};
}

void FMacMachine::StartRelaunchReconnect()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (State != ERowingConnectionState::Idle)
		return;
	ReconnectInProgress = true;
	ReconnectConnectInFlight = true;
	ReconnectAttempts = 1;
	ReconnectStartedNs = MonotonicNowNs();
	Transition(ERowingConnectionState::Reconnecting,
			   ERowingConnectionReason::ReconnectStarted);
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	dispatch_async(Queue, ^{
	  [Central connectPeripheral:Delegate.Peripheral options:nil];
	});
}

void FMacMachine::ExpectRelaunchIdentity(Concept2PM::FPM5Identity Identity)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	ExpectedReconnectIdentity = std::move(Identity);
}

ERowingConnectionState FMacMachine::GetConnectionState() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	return State;
}

FRowingMachineDiagnostics FMacMachine::GetDiagnostics() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	std::lock_guard<std::mutex> AcquisitionLock(AcquisitionMutex);
	FRowingMachineDiagnostics Diagnostics;
	Diagnostics.AcquisitionQueue = {
		static_cast<std::uint32_t>(AcquisitionQueue.size()),
		static_cast<std::uint32_t>(MaxQueuedEvents),
		AcquisitionQueueHighWaterMark,
		AcquisitionQueueOverflowCount};
	Diagnostics.EventQueue = {
		static_cast<std::uint32_t>(Events.size()),
		static_cast<std::uint32_t>(MaxQueuedEvents),
		EventQueueHighWaterMark,
		EventQueueOverflowCount};
	return Diagnostics;
}

FPM5RunDiagnostics FMacMachine::GetPM5RunDiagnostics() const
{
	std::lock_guard<std::mutex> Lock(Mutex);
	FPM5RunDiagnostics Diagnostics;
	Diagnostics.RequestedStatusPeriodMs = ActiveProfile != nullptr
											  ? ActiveProfile->RequestedStatusPeriodMs
											  : LastRequestedStatusPeriodMs;
	Diagnostics.StatusRateWriteAttemptCount = StatusRateWriteAttemptCount;
	Diagnostics.StatusRateWriteSuccessCount = StatusRateWriteSuccessCount;
	Diagnostics.StatusRateWriteFailureCount = StatusRateWriteFailureCount;
	Diagnostics.ProbeCapture = ProbeCaptureDiagnostics;
	Diagnostics.ProbeCapture.EvidenceQueueCurrentDepth =
		static_cast<std::uint32_t>(ProbeEvidenceQueue.size());
	Diagnostics.Characteristics.reserve(CharacteristicDiagnostics.size());
	for (const auto &[ShortId, Stats] : CharacteristicDiagnostics)
	{
		(void)ShortId;
		Diagnostics.Characteristics.push_back(Stats);
	}
	Diagnostics.CallbackErrors.reserve(CallbackErrorDiagnostics.size());
	for (const auto &[Key, Stats] : CallbackErrorDiagnostics)
	{
		(void)Key;
		Diagnostics.CallbackErrors.push_back(Stats);
	}
	return Diagnostics;
}

bool FMacMachine::TryPollPM5ProbePacket(FPM5ProbePacketEvidence &OutEvidence)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (ProbeEvidenceQueue.empty())
		return false;
	OutEvidence = std::move(ProbeEvidenceQueue.front());
	ProbeEvidenceQueue.pop_front();
	return true;
}

void FMacMachine::FinalizePM5ProbeCapture()
{
	// The CoreBluetooth callback queue produces acquisition work. Crossing it
	// first and the decoder queue second ensures every packet observed before
	// this call has reached the probe evidence queue before the TUI finalizes.
	dispatch_sync(Queue, ^{
				  });
	dispatch_sync(DecoderQueue, ^{
				  });
	std::lock_guard<std::mutex> Lock(Mutex);
	if (ProbeCaptureDiagnostics.Active)
	{
		ProbeCaptureDiagnostics.Active = false;
		ProbeCaptureDiagnostics.StopReason = EPM5ProbeCaptureStopReason::RunStopped;
	}
}

void FMacMachine::RecordCallbackFailure(EPM5CallbackStage Stage,
										std::uint16_t Characteristic,
										NSError *Error)
{
	auto &Stats = CallbackErrorDiagnostics[{Stage, Characteristic}];
	Stats.Stage = Stage;
	Stats.Characteristic = Characteristic;
	++Stats.Count;
	if (Error == nil)
		return;
	Stats.HasError = true;
	Stats.LastErrorMonotonicNs = MonotonicNowNs();
	if ([Error.domain isEqualToString:@"CBErrorDomain"])
		Stats.LastErrorDomain = EPM5ErrorDomain::CoreBluetooth;
	else if ([Error.domain isEqualToString:@"CBATTErrorDomain"])
		Stats.LastErrorDomain = EPM5ErrorDomain::CoreBluetoothATT;
	else if ([Error.domain isEqualToString:NSCocoaErrorDomain] ||
			 [Error.domain isEqualToString:NSPOSIXErrorDomain])
		Stats.LastErrorDomain = EPM5ErrorDomain::Foundation;
	else
		Stats.LastErrorDomain = EPM5ErrorDomain::Other;
	Stats.LastErrorCode = static_cast<std::int32_t>(Error.code);
}

bool FMacMachine::TryPollEvent(FRowingMachineEvent &OutEvent)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (Events.empty())
		return false;
	OutEvent = std::move(Events.front());
	Events.pop_front();
	return true;
}

void FMacMachine::OnConnected()
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (State != ERowingConnectionState::Connecting &&
		State != ERowingConnectionState::Reconnecting)
		return;
	const bool WasReconnecting = State == ERowingConnectionState::Reconnecting;
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	Delegate.Peripheral.delegate = Delegate;
	ReconnectConnectInFlight = false;
	ResetHandshakeState();
	if (WasReconnecting)
		TelemetryMerger.MarkReconnected();
	Transition(ERowingConnectionState::Discovering,
			   ERowingConnectionReason::OperationStarted);
	ArmHandshakeTimeout();
	[Delegate.Peripheral discoverServices:@[
		[CBUUID UUIDWithString:@"CE060010-43E5-11E4-916C-0800200C9A66"]
	]];
}

void FMacMachine::OnConnectionFailed(NSError *Error)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	RecordCallbackFailure(EPM5CallbackStage::Connection, 0, Error);
	if (State == ERowingConnectionState::Reconnecting)
	{
		ReconnectConnectInFlight = false;
		ScheduleReconnectAttempt();
		return;
	}
	EmitFault(ERowingFaultCode::ConnectionTimeout,
			  ERowingFaultSeverity::Recoverable,
			  ERowingOperation::Connect,
			  "connection failed");
	Transition(ERowingConnectionState::Failed,
			   ERowingConnectionReason::OperationFailed);
}

void FMacMachine::OnDisconnected(NSError *Error)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (Error != nil)
		RecordCallbackFailure(EPM5CallbackStage::Disconnection, 0, Error);
	if (State == ERowingConnectionState::Idle)
		return;
	if (State == ERowingConnectionState::Reconnecting)
	{
		ReconnectConnectInFlight = false;
		ScheduleReconnectAttempt();
		return;
	}
	if (State == ERowingConnectionState::Ready ||
		State == ERowingConnectionState::Stale ||
		State == ERowingConnectionState::DiagnosticOnly)
	{
		EmitFault(ERowingFaultCode::Disconnected,
				  ERowingFaultSeverity::Recoverable,
				  ERowingOperation::Reconnect,
				  "peripheral disconnected; reconnecting the selected device");
		BeginReconnect();
		return;
	}
	EmitFault(ERowingFaultCode::Disconnected,
			  ERowingFaultSeverity::Recoverable,
			  ERowingOperation::Connect,
			  "peripheral disconnected");
	Transition(ERowingConnectionState::Failed,
			   ERowingConnectionReason::LinkLost);
}

void FMacMachine::OnServicesDiscovered(NSError *Error)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	if (Error != nil)
	{
		RecordCallbackFailure(EPM5CallbackStage::ServiceDiscovery, 0, Error);
		EmitFault(ERowingFaultCode::MissingService,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::Discover,
				  "service discovery failed");
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::OperationFailed);
		return;
	}
	if (IdentityFinished && ActiveProfile != nullptr &&
		State == ERowingConnectionState::Subscribing)
	{
		for (CBService *Service in Delegate.Peripheral.services)
		{
			if (IsUuid(Service.UUID,
					   @"CE060030-43E5-11E4-916C-0800200C9A66"))
			{
				NSMutableArray<CBUUID *> *TelemetryUuids = [NSMutableArray new];
				for (const Concept2PM::FPM5CharacteristicProfile &Characteristic :
					 ActiveProfile->Characteristics)
					[TelemetryUuids addObject:[CBUUID UUIDWithString:CharacteristicUuid(Characteristic.ShortId)]];
				if (TelemetryUuids.count == 0)
				{
					EmitFault(ERowingFaultCode::MissingCharacteristic,
							  ERowingFaultSeverity::Terminal,
							  ERowingOperation::Subscribe,
							  "profile has no required telemetry notifications");
					Transition(ERowingConnectionState::Unsupported,
							   ERowingConnectionReason::OperationFailed);
					return;
				}
				[Delegate.Peripheral discoverCharacteristics:TelemetryUuids
												  forService:Service];
				return;
			}
		}
		EmitFault(ERowingFaultCode::MissingService,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::Discover,
				  "rowing telemetry service missing");
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::OperationFailed);
		return;
	}
	for (CBService *Service in Delegate.Peripheral.services)
	{
		if (IsUuid(Service.UUID, @"CE060010-43E5-11E4-916C-0800200C9A66"))
		{
			Transition(ERowingConnectionState::ReadingIdentity,
					   ERowingConnectionReason::OperationStarted);
			[Delegate.Peripheral
				discoverCharacteristics:IdentityCharacteristicUuids()
							 forService:Service];
			return;
		}
	}
	EmitFault(ERowingFaultCode::MissingService,
			  ERowingFaultSeverity::Terminal,
			  ERowingOperation::Discover,
			  "device-information service missing");
	Transition(ERowingConnectionState::Unsupported,
			   ERowingConnectionReason::OperationFailed);
}

void FMacMachine::OnCharacteristicsDiscovered(CBService *Service,
											  NSError *Error)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	const bool IsTelemetryService = IsUuid(
		Service.UUID, @"CE060030-43E5-11E4-916C-0800200C9A66");
	if (Error != nil)
	{
		RecordCallbackFailure(
			IsTelemetryService ? EPM5CallbackStage::TelemetryCharacteristicDiscovery
							   : EPM5CallbackStage::IdentityCharacteristicDiscovery,
			0,
			Error);
		EmitFault(ERowingFaultCode::MissingCharacteristic,
				  ERowingFaultSeverity::Terminal,
				  IsTelemetryService ? ERowingOperation::Subscribe
									 : ERowingOperation::ReadIdentity,
				  IsTelemetryService ? "telemetry characteristic discovery failed"
									 : "identity characteristic discovery failed");
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::OperationFailed);
		return;
	}
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	if (IsTelemetryService && ActiveProfile != nullptr)
	{
		std::set<std::uint16_t> FoundRequired;
		for (CBCharacteristic *Characteristic in Service.characteristics)
		{
			const Concept2PM::FPM5CharacteristicProfile *ProfileCharacteristic = nullptr;
			for (const Concept2PM::FPM5CharacteristicProfile &Candidate :
				 ActiveProfile->Characteristics)
			{
				if (IsUuid(Characteristic.UUID, CharacteristicUuid(Candidate.ShortId)))
				{
					ProfileCharacteristic = &Candidate;
					break;
				}
			}
			if (ProfileCharacteristic == nullptr)
				continue;
			const std::uint8_t ObservedProperties =
				ToProfileProperties(Characteristic.properties);
			auto &CharacteristicStats =
				CharacteristicDiagnostics[ProfileCharacteristic->ShortId];
			CharacteristicStats.Characteristic = ProfileCharacteristic->ShortId;
			CharacteristicStats.PropertiesObserved = true;
			CharacteristicStats.ObservedProperties = ObservedProperties;
			if ((ObservedProperties & ProfileCharacteristic->RequiredProperties) !=
				ProfileCharacteristic->RequiredProperties)
			{
				if (!ProfileCharacteristic->Required)
					continue;
				EmitFault(ERowingFaultCode::InvalidProperty,
						  ERowingFaultSeverity::Terminal,
						  ERowingOperation::Subscribe,
						  "profile-required characteristic properties are unavailable");
				Transition(ERowingConnectionState::Unsupported,
						   ERowingConnectionReason::OperationFailed);
				return;
			}
			if (ProfileCharacteristic->ShortId == 0x0034U)
			{
				if (ActiveProfile->RequestedStatusPeriodMs != 100U)
					continue;
				const std::uint8_t StatusRate = 3U;
				NSData *Value = [NSData dataWithBytes:&StatusRate length:sizeof(StatusRate)];
				StatusRateWriteRequired = true;
				StatusRateConfigured = false;
				++StatusRateWriteAttemptCount;
				[Delegate.Peripheral writeValue:Value
							  forCharacteristic:Characteristic
										   type:CBCharacteristicWriteWithResponse];
				continue;
			}
			if (!ProfileCharacteristic->RequiresNotify)
				continue;
			if (ProfileCharacteristic->Required)
				FoundRequired.insert(ProfileCharacteristic->ShortId);
			++CharacteristicDiagnostics[ProfileCharacteristic->ShortId]
				  .NotificationEnableAttemptCount;
			[Delegate.Peripheral setNotifyValue:YES forCharacteristic:Characteristic];
		}
		if (FoundRequired != RequiredNotifyCharacteristics)
		{
			EmitFault(ERowingFaultCode::MissingCharacteristic,
					  ERowingFaultSeverity::Terminal,
					  ERowingOperation::Subscribe,
					  "required telemetry characteristic missing");
			Transition(ERowingConnectionState::Unsupported,
					   ERowingConnectionReason::OperationFailed);
		}
		return;
	}
	for (CBCharacteristic *Characteristic in Service.characteristics)
	{
		if ((Characteristic.properties & CBCharacteristicPropertyRead) == 0)
		{
			EmitFault(ERowingFaultCode::InvalidProperty,
					  ERowingFaultSeverity::Terminal,
					  ERowingOperation::ReadIdentity,
					  "identity characteristic is not readable");
			Transition(ERowingConnectionState::Unsupported,
					   ERowingConnectionReason::OperationFailed);
			return;
		}
		[Delegate.Peripheral readValueForCharacteristic:Characteristic];
	}
}

void FMacMachine::OnCharacteristicValue(CBCharacteristic *Characteristic,
										NSError *Error)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (IdentityFinished && ActiveProfile != nullptr)
	{
		if (Error != nil)
		{
			RecordCallbackFailure(EPM5CallbackStage::TelemetryValueUpdate,
								  ShortIdFromUuid(Characteristic.UUID),
								  Error);
			EmitFault(ERowingFaultCode::InvalidValue,
					  ERowingFaultSeverity::Warning,
					  ERowingOperation::ReceiveTelemetry,
					  "telemetry notification read failed");
			return;
		}
		EnqueueNotification(Characteristic);
		return;
	}
	if (IdentityFinished)
		return;
	if (Error != nil)
	{
		RecordCallbackFailure(EPM5CallbackStage::IdentityRead,
							  ShortIdFromUuid(Characteristic.UUID),
							  Error);
		EmitFault(ERowingFaultCode::InvalidValue,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::ReadIdentity,
				  "identity read failed");
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::OperationFailed);
		IdentityFinished = true;
		return;
	}
	if (IsUuid(Characteristic.UUID, @"CE060011-43E5-11E4-916C-0800200C9A66"))
		Model = ReadBoundedUtf8(Characteristic.value);
	else if (IsUuid(Characteristic.UUID,
					@"CE060013-43E5-11E4-916C-0800200C9A66"))
		Hardware = ReadBoundedUtf8(Characteristic.value);
	else if (IsUuid(Characteristic.UUID,
					@"CE060014-43E5-11E4-916C-0800200C9A66"))
		Firmware = ReadBoundedUtf8(Characteristic.value);
	else if (IsUuid(Characteristic.UUID,
					@"CE060015-43E5-11E4-916C-0800200C9A66"))
		Manufacturer = ReadBoundedUtf8(Characteristic.value);
	else if (IsUuid(Characteristic.UUID,
					@"CE060016-43E5-11E4-916C-0800200C9A66") &&
			 Characteristic.value.length == 1)
		MachineKind = DecodeMachineKind(
			static_cast<const std::uint8_t *>(Characteristic.value.bytes)[0]);

	if (!Model.empty() && !Hardware.empty() && !Firmware.empty() &&
		!Manufacturer.empty() && MachineKind)
		FinishIdentity();
}

void FMacMachine::OnNotificationState(CBCharacteristic *Characteristic,
									  NSError *Error)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (ActiveProfile == nullptr)
		return;
	std::optional<std::uint16_t> ShortId;
	const auto ProfileCharacteristic = std::find_if(
		ActiveProfile->Characteristics.begin(), ActiveProfile->Characteristics.end(), [Characteristic](const Concept2PM::FPM5CharacteristicProfile &Candidate)
		{ return Candidate.RequiresNotify &&
				 IsUuid(Characteristic.UUID, CharacteristicUuid(Candidate.ShortId)); });
	if (ProfileCharacteristic != ActiveProfile->Characteristics.end())
		ShortId = ProfileCharacteristic->ShortId;
	if (!ShortId || !ProfileCharacteristic->RequiresNotify)
		return;
	if (Error != nil || !Characteristic.isNotifying)
	{
		++CharacteristicDiagnostics[*ShortId].NotificationEnableFailureCount;
		RecordCallbackFailure(EPM5CallbackStage::NotificationSubscription,
							  ShortIdFromUuid(Characteristic.UUID),
							  Error);
		if (!ProfileCharacteristic->Required)
			return;
		EmitFault(ERowingFaultCode::InvalidProperty,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::Subscribe,
				  "PM5 refused a required telemetry notification");
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::OperationFailed);
		return;
	}
	++CharacteristicDiagnostics[*ShortId].NotificationEnableSuccessCount;
	EnabledNotifyCharacteristics.insert(*ShortId);
	FinishTelemetryIfReady();
}

void FMacMachine::OnStatusRateWritten(CBCharacteristic *Characteristic,
									  NSError *Error)
{
	std::lock_guard<std::mutex> Lock(Mutex);
	if (!StatusRateWriteRequired)
		return;
	if (Error != nil ||
		!IsUuid(Characteristic.UUID,
				@"CE060034-43E5-11E4-916C-0800200C9A66"))
	{
		RecordCallbackFailure(EPM5CallbackStage::StatusRateWrite,
							  ShortIdFromUuid(Characteristic.UUID),
							  Error);
		++StatusRateWriteFailureCount;
		EmitFault(ERowingFaultCode::InvalidValue,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::Subscribe,
				  "PM5 status-rate configuration was not accepted");
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::OperationFailed);
		return;
	}
	++StatusRateWriteSuccessCount;
	StatusRateConfigured = true;
	FinishTelemetryIfReady();
}

void FMacMachine::FinishTelemetryIfReady()
{
	if (ActiveProfile == nullptr ||
		!std::includes(EnabledNotifyCharacteristics.begin(),
					   EnabledNotifyCharacteristics.end(),
					   RequiredNotifyCharacteristics.begin(),
					   RequiredNotifyCharacteristics.end()) ||
		!std::includes(SeenRequiredCharacteristics.begin(),
					   SeenRequiredCharacteristics.end(),
					   RequiredNotifyCharacteristics.begin(),
					   RequiredNotifyCharacteristics.end()) ||
		(StatusRateWriteRequired && !StatusRateConfigured) ||
		State != ERowingConnectionState::Subscribing)
		return;
	if (ActiveProfile->DiagnosticOnly)
	{
		Transition(ERowingConnectionState::DiagnosticOnly,
				   ERowingConnectionReason::DiagnosticObservationConfirmed);
		ReconnectInProgress = false;
		ReconnectAttempts = 0;
	}
	else if (ActiveProfile->SupportState == ERowingMachineSupportState::Allowed)
	{
		Transition(ERowingConnectionState::Ready,
				   ERowingConnectionReason::ReadinessConfirmed);
		VIRPM5PeripheralDelegate *Delegate =
			static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
		Discovery.RememberWhenReady(Delegate.Peripheral,
									{Model, Hardware, Firmware, *MachineKind});
		if (ReconnectInProgress && LastMachineInfo)
		{
			const std::uint64_t GapNs = MonotonicNowNs() - ReconnectStartedNs;
			Emit(FRowingConnectionRestored{*LastMachineInfo, GapNs / 1'000'000ULL});
			ReconnectInProgress = false;
			ReconnectAttempts = 0;
		}
	}
	else
	{
		EmitFault(ERowingFaultCode::UnsupportedIdentity,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::Subscribe,
				  "profile cannot authorize telemetry readiness");
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::CapabilityRejected);
	}
}

void FMacMachine::EnqueueNotification(CBCharacteristic *Characteristic)
{
	if (ActiveProfile == nullptr || Characteristic.value == nil ||
		Characteristic.value.length == 0)
		return;
	const auto It = std::find_if(
		ActiveProfile->Characteristics.begin(),
		ActiveProfile->Characteristics.end(),
		[Characteristic](const Concept2PM::FPM5CharacteristicProfile &Profile)
		{
			return IsUuid(Characteristic.UUID, CharacteristicUuid(Profile.ShortId));
		});
	if (It == ActiveProfile->Characteristics.end() || !It->RequiresNotify)
		return;
	const std::uint16_t ShortId = It->ShortId;
	const auto *Data = static_cast<const std::uint8_t *>(Characteristic.value.bytes);
	FPendingNotification Notification;
	Notification.ShortId = ShortId;
	Notification.Bytes.assign(Data, Data + Characteristic.value.length);
	Notification.ReceivedMonotonicNs = MonotonicNowNs();
	Notification.ConnectionStateAtReceive = State;
	{
		std::lock_guard<std::mutex> AcquisitionLock(AcquisitionMutex);
		if (AcquisitionOverflowed)
			return;
		if (AcquisitionQueue.size() >= MaxQueuedEvents)
		{
			AcquisitionOverflowed = true;
			++AcquisitionQueueOverflowCount;
			FailAcquisitionOverflow();
			return;
		}
		AcquisitionQueue.push_back(std::move(Notification));
		AcquisitionQueueHighWaterMark = std::max(
			AcquisitionQueueHighWaterMark,
			static_cast<std::uint32_t>(AcquisitionQueue.size()));
	}
	ScheduleDecoder();
}

void FMacMachine::ScheduleDecoder()
{
	if (DecoderScheduled)
		return;
	DecoderScheduled = true;
	const std::weak_ptr<int> WeakLifetime = LifetimeToken;
	dispatch_async(DecoderQueue, ^{
	  if (WeakLifetime.expired())
		  return;
	  DrainNotifications();
	});
}

void FMacMachine::DrainNotifications()
{
	for (;;)
	{
		FPendingNotification Notification;
		bool WasEmpty = false;
		{
			std::lock_guard<std::mutex> AcquisitionLock(AcquisitionMutex);
			if (AcquisitionQueue.empty())
				WasEmpty = true;
			else
			{
				Notification = std::move(AcquisitionQueue.front());
				AcquisitionQueue.pop_front();
			}
		}
		if (WasEmpty)
		{
			std::lock_guard<std::mutex> MachineLock(Mutex);
			std::lock_guard<std::mutex> AcquisitionLock(AcquisitionMutex);
			if (AcquisitionQueue.empty())
			{
				DecoderScheduled = false;
				return;
			}
			continue;
		}
		std::lock_guard<std::mutex> MachineLock(Mutex);
		ReceiveTelemetryPacket(Notification.ShortId,
							   Notification.Bytes,
							   Notification.ReceivedMonotonicNs,
							   Notification.ConnectionStateAtReceive);
	}
}

void FMacMachine::FailAcquisitionOverflow()
{
	Transition(ERowingConnectionState::Failed,
			   ERowingConnectionReason::OperationFailed);
	FRowingFault Fault;
	Fault.Code = ERowingFaultCode::QueueOverflow;
	Fault.Severity = ERowingFaultSeverity::Terminal;
	Fault.Operation = ERowingOperation::ReceiveTelemetry;
	Fault.ConnectionState = ERowingConnectionState::Failed;
	Fault.DiagnosticText = "notification acquisition queue capacity reached; session stopped";
	Fault.ExpectedValue = MaxQueuedEvents;
	Fault.ActualValue = MaxQueuedEvents + 1;
	Emit(std::move(Fault));
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	[Central cancelPeripheralConnection:Delegate.Peripheral];
}

void FMacMachine::CaptureProbePacket(
	std::uint16_t ShortId,
	const std::vector<std::uint8_t> &Bytes,
	std::uint64_t ReceivedMonotonicNs,
	ERowingConnectionState PacketConnectionState,
	Concept2PM::EPacketError ParserResult,
	const std::vector<std::size_t> &ApprovedPacketLengths,
	std::uint64_t CharacteristicSequence)
{
	if (!ProbeCaptureDiagnostics.Enabled)
		return;

	const std::uint64_t PacketSequence = ++ProbePacketSequence;
	++ProbeCaptureDiagnostics.ObservedPacketCount;
	if (ProbeCaptureDiagnostics.Active)
	{
		const std::uint64_t ElapsedMs =
			ReceivedMonotonicNs >= ProbeCaptureDiagnostics.StartedMonotonicNs
				? (ReceivedMonotonicNs -
				   ProbeCaptureDiagnostics.StartedMonotonicNs) /
					  1'000'000ULL
				: 0;
		if (ElapsedMs > ProbeConfiguration.MaxCaptureDurationMs)
		{
			ProbeCaptureDiagnostics.Active = false;
			ProbeCaptureDiagnostics.StopReason =
				EPM5ProbeCaptureStopReason::DurationLimit;
		}
		else if (ProbeCaptureDiagnostics.CapturedPacketCount >=
				 ProbeConfiguration.MaxCapturedPacketCount)
		{
			ProbeCaptureDiagnostics.Active = false;
			ProbeCaptureDiagnostics.StopReason =
				EPM5ProbeCaptureStopReason::PacketLimit;
		}
	}
	if (!ProbeCaptureDiagnostics.Active)
	{
		++ProbeCaptureDiagnostics.LimitDroppedPacketCount;
		return;
	}
	if (ProbeEvidenceQueue.size() >= ProbeConfiguration.EvidenceQueueCapacity)
	{
		++ProbeCaptureDiagnostics.EvidenceQueueOverflowCount;
		return;
	}

	FPM5ProbePacketEvidence Evidence;
	Evidence.PacketSequence = PacketSequence;
	Evidence.CharacteristicSequence = CharacteristicSequence;
	Evidence.Characteristic = ShortId;
	Evidence.ReceivedMonotonicNs = ReceivedMonotonicNs;
	Evidence.ConnectionState = PacketConnectionState;
	Evidence.ParserResult = ParserResult;
	Evidence.ApprovedPacketLengths = ApprovedPacketLengths;
	Evidence.OriginalPayloadLength = Bytes.size();
	const std::size_t CapturedLength =
		std::min(Bytes.size(), ProbeConfiguration.MaxCapturedPayloadBytes);
	Evidence.PayloadBytes.assign(Bytes.begin(), Bytes.begin() + CapturedLength);
	Evidence.PayloadTruncated = CapturedLength != Bytes.size();
	if (Evidence.PayloadTruncated)
		++ProbeCaptureDiagnostics.TruncatedPayloadCount;
	++ProbeCaptureDiagnostics.CapturedPacketCount;
	ProbeCaptureDiagnostics.CapturedPayloadByteCount += CapturedLength;
	ProbeEvidenceQueue.push_back(std::move(Evidence));
	ProbeCaptureDiagnostics.EvidenceQueueHighWaterMark = std::max(
		ProbeCaptureDiagnostics.EvidenceQueueHighWaterMark,
		static_cast<std::uint32_t>(ProbeEvidenceQueue.size()));
}

void FMacMachine::ReceiveTelemetryPacket(
	std::uint16_t ShortId,
	const std::vector<std::uint8_t> &Bytes,
	std::uint64_t ReceivedMonotonicNs,
	ERowingConnectionState ConnectionStateAtReceive)
{
	if (ActiveProfile == nullptr ||
		(State != ERowingConnectionState::Subscribing &&
		 State != ERowingConnectionState::Ready &&
		 State != ERowingConnectionState::Stale &&
		 State != ERowingConnectionState::DiagnosticOnly))
		return;
	const auto *ProfileCharacteristic = FindProfileCharacteristic(ShortId);
	if (ProfileCharacteristic == nullptr)
		return;
	auto &CharacteristicStats = CharacteristicDiagnostics[ShortId];
	CharacteristicStats.Characteristic = ShortId;
	CharacteristicStats.RecordNotification(ReceivedMonotonicNs, Bytes.size());
	const Concept2PM::FDecodedPacket Packet = Concept2PM::DecodePacket(
		ShortId, Bytes, ProfileCharacteristic->AllowedPacketLengths);
	CaptureProbePacket(ShortId,
					   Bytes,
					   ReceivedMonotonicNs,
					   ConnectionStateAtReceive,
					   Packet.Error.Code,
					   ProfileCharacteristic->AllowedPacketLengths,
					   CharacteristicStats.NotificationCount);
	if (!Packet.IsValid())
	{
		CharacteristicStats.RecordParserError(
			Packet.Error.Code, Bytes.size(), ReceivedMonotonicNs);
		const ERowingFaultCode FaultCode =
			Packet.Error.Code == Concept2PM::EPacketError::LengthNotApproved
				? ERowingFaultCode::InvalidPacketLength
				: (Packet.Error.Code == Concept2PM::EPacketError::UnknownCharacteristic
					   ? ERowingFaultCode::InvalidProperty
					   : ERowingFaultCode::InvalidValue);
		std::string Detail = "notification characteristic=";
		char CharacteristicText[7]{};
		std::snprintf(CharacteristicText, sizeof(CharacteristicText), "0x%04x", static_cast<unsigned int>(ShortId));
		Detail += CharacteristicText;
		Detail += " packet_length=" + std::to_string(Bytes.size()) + " allowed_lengths=";
		for (std::size_t Index = 0;
			 Index < ProfileCharacteristic->AllowedPacketLengths.size();
			 ++Index)
		{
			if (Index != 0)
				Detail += ',';
			Detail += std::to_string(
				ProfileCharacteristic->AllowedPacketLengths[Index]);
		}
		const ERowingFaultSeverity Severity = ProfileCharacteristic->Required
												  ? ERowingFaultSeverity::Terminal
												  : ERowingFaultSeverity::Warning;
		EmitFault(FaultCode,
				  Severity,
				  ERowingOperation::ReceiveTelemetry,
				  Detail.c_str(),
				  Packet.Error.ExpectedLength,
				  Packet.Error.ActualLength);
		if (ProfileCharacteristic->Required)
			Transition(ERowingConnectionState::Unsupported,
					   ERowingConnectionReason::OperationFailed);
		return;
	}
	PublishAvailableMetrics(*ProfileCharacteristic);
	SeenRequiredCharacteristics.insert(ShortId);
	const auto NowNs = ReceivedMonotonicNs;
	if (Packet.GeneralStatus)
	{
		if (!ConfirmReconnectContinuation(*Packet.GeneralStatus))
			return;
		ReconnectBaseline = FReconnectBaseline{
			Packet.GeneralStatus->ElapsedMs,
			Packet.GeneralStatus->DistanceMm,
			Packet.GeneralStatus->WorkoutState};
		LastGeneralStatusNs = NowNs;
		ArmLivenessTimeout();
		if (State == ERowingConnectionState::Stale &&
			ActiveProfile->SupportState == ERowingMachineSupportState::Allowed)
			Transition(ERowingConnectionState::Ready,
					   ERowingConnectionReason::TelemetryResumed);
	}
	auto Sample = TelemetryMerger.Push(Packet, NowNs);
	auto Correction = TelemetryMerger.TakePendingCorrection();
	FinishTelemetryIfReady();
	if (State == ERowingConnectionState::DiagnosticOnly ||
		State == ERowingConnectionState::Ready)
	{
		if (Packet.StrokeData)
		{
			const auto &Fact = *Packet.StrokeData;
			FRowingStrokeMetrics Stroke;
			Stroke.Source = ERowingStrokeMetricsSource::KinematicsAndForce;
			Stroke.SourceElapsedMs = Fact.ElapsedMs;
			Stroke.StrokeCount = Fact.StrokeCount;
			Stroke.CumulativeDistanceMm = Fact.CumulativeDistanceMm;
			Stroke.DriveLengthMm = Fact.DriveLengthMm;
			Stroke.DriveTimeMs = Fact.DriveTimeMs;
			Stroke.RecoveryTimeMs = Fact.RecoveryTimeMs;
			Stroke.StrokeDistanceMm = Fact.StrokeDistanceMm;
			Stroke.PeakDriveForceDeciLb = Fact.PeakDriveForceDeciLb;
			Stroke.AverageDriveForceDeciLb = Fact.AverageDriveForceDeciLb;
			Stroke.WorkPerStrokeDeciJoules = Fact.WorkPerStrokeDeciJoules;
			Emit(std::move(Stroke));
		}
		if (Packet.AdditionalStrokeData)
		{
			const auto &Fact = *Packet.AdditionalStrokeData;
			FRowingStrokeMetrics Stroke;
			Stroke.Source = ERowingStrokeMetricsSource::PowerAndProjection;
			Stroke.SourceElapsedMs = Fact.ElapsedMs;
			Stroke.StrokeCount = Fact.StrokeCount;
			Stroke.StrokePowerW = Fact.StrokePowerW;
			Stroke.CaloriesPerHour = Fact.CaloriesPerHour;
			Stroke.ProjectedWorkTimeMs = Fact.ProjectedWorkTimeMs;
			Stroke.ProjectedWorkDistanceMm = Fact.ProjectedWorkDistanceMm;
			Stroke.ProjectedWorkOtherRaw = Fact.ProjectedWorkOtherRaw;
			Emit(std::move(Stroke));
		}
	}
	if (Sample && State == ERowingConnectionState::DiagnosticOnly)
	{
		LastPublishedSampleSequence = Sample->Sequence;
		Emit(FRowingDiagnosticSample{std::move(*Sample)});
	}
	else if (Sample && State == ERowingConnectionState::Ready)
	{
		LastPublishedSampleSequence = Sample->Sequence;
		LastEmittedMetricSampleSequence = Sample->Sequence;
		Emit(std::move(*Sample));
	}
	if (Correction && State == ERowingConnectionState::Ready &&
		ActiveProfile->SupportState == ERowingMachineSupportState::Allowed &&
		!ActiveProfile->DiagnosticOnly &&
		Correction->TargetSampleSequence == LastEmittedMetricSampleSequence)
		Emit(std::move(*Correction));
}

void FMacMachine::PublishAvailableMetrics(
	const Concept2PM::FPM5CharacteristicProfile &Characteristic)
{
	if (ActiveProfile == nullptr || ActiveProfile->DiagnosticOnly || !LastMachineInfo)
		return;
	const FRowingMetricSet AvailableMetrics =
		LastMachineInfo->SupportedMetrics | Characteristic.ImplementedMetrics;
	if (AvailableMetrics == LastMachineInfo->SupportedMetrics)
		return;
	LastMachineInfo->SupportedMetrics = AvailableMetrics;
	Emit(*LastMachineInfo);
}

bool FMacMachine::OwnsPeripheral(CBPeripheral *Peripheral) const
{
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	return Delegate != nil && Delegate.Peripheral == Peripheral;
}

const Concept2PM::FPM5CharacteristicProfile *
FMacMachine::FindProfileCharacteristic(std::uint16_t ShortId) const
{
	if (ActiveProfile == nullptr)
		return nullptr;
	const auto It = std::find_if(
		ActiveProfile->Characteristics.begin(), ActiveProfile->Characteristics.end(), [ShortId](const Concept2PM::FPM5CharacteristicProfile &Entry)
		{ return Entry.ShortId == ShortId; });
	return It == ActiveProfile->Characteristics.end() ? nullptr : &*It;
}

bool FMacMachine::ConfirmReconnectContinuation(
	const Concept2PM::FGeneralStatusFact &General)
{
	if (!ReconnectInProgress)
		return true;

	// A relaunch reconnect has no in-memory source baseline. Identity and the
	// complete readiness handshake still fail closed, but no gap measurement or
	// source value is invented for the time before this process started.
	if (!ReconnectBaseline)
	{
		ReconnectContinuationConfirmed = true;
		return true;
	}

	Concept2PM::FGeneralStatusFact Previous;
	Previous.ElapsedMs = ReconnectBaseline->ElapsedMs;
	Previous.DistanceMm = ReconnectBaseline->DistanceMm;
	Previous.WorkoutState = ReconnectBaseline->WorkoutState;
	if (!Concept2PM::IsReconnectContinuationCompatible(Previous, General))
	{
		EmitFault(ERowingFaultCode::InvalidValue,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::Reconnect,
				  "reconnected PM5 source state is incompatible with continuation");
		ReconnectInProgress = false;
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::CapabilityRejected);
		return false;
	}

	ReconnectContinuationConfirmed = true;
	return true;
}

void FMacMachine::ResetHandshakeState()
{
	++HandshakeGeneration;
	++LivenessGeneration;
	IdentityFinished = false;
	ActiveProfile = nullptr;
	Model.clear();
	Hardware.clear();
	Firmware.clear();
	Manufacturer.clear();
	MachineKind.reset();
	RequiredNotifyCharacteristics.clear();
	EnabledNotifyCharacteristics.clear();
	SeenRequiredCharacteristics.clear();
	StatusRateWriteRequired = false;
	StatusRateConfigured = false;
	LastGeneralStatusNs.reset();
	ReconnectContinuationConfirmed = false;
	std::lock_guard<std::mutex> AcquisitionLock(AcquisitionMutex);
	AcquisitionQueue.clear();
}

void FMacMachine::ArmHandshakeTimeout()
{
	const std::uint64_t Generation = ++HandshakeGeneration;
	const std::weak_ptr<int> WeakLifetime = LifetimeToken;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, HandshakeTimeoutNs), Queue, ^{
	  if (WeakLifetime.expired())
		  return;
	  std::lock_guard<std::mutex> Lock(Mutex);
	  if (Generation != HandshakeGeneration)
		  return;
	  if (State == ERowingConnectionState::Discovering ||
		  State == ERowingConnectionState::ReadingIdentity ||
		  State == ERowingConnectionState::Subscribing)
		  FailHandshakeTimeout();
	});
}

void FMacMachine::FailHandshakeTimeout()
{
	EmitFault(ERowingFaultCode::ConnectionTimeout,
			  ERowingFaultSeverity::Terminal,
			  ERowingOperation::Discover,
			  "identity or required telemetry discovery did not complete before timeout");
	Transition(ERowingConnectionState::Unsupported,
			   ERowingConnectionReason::OperationFailed);
}

void FMacMachine::ArmLivenessTimeout()
{
	const std::uint64_t Generation = ++LivenessGeneration;
	const std::weak_ptr<int> WeakLifetime = LifetimeToken;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, StaleTelemetryNs), Queue, ^{
	  if (WeakLifetime.expired())
		  return;
	  std::lock_guard<std::mutex> Lock(Mutex);
	  if (Generation != LivenessGeneration || !LastGeneralStatusNs ||
		  State != ERowingConnectionState::Ready)
		  return;
	  const std::uint64_t AgeNs = MonotonicNowNs() - *LastGeneralStatusNs;
	  if (AgeNs < StaleTelemetryNs)
		  return;
	  Emit(FRowingTelemetryStale{LastPublishedSampleSequence, AgeNs / 1'000'000ULL});
	  EmitFault(ERowingFaultCode::StaleTelemetry,
				ERowingFaultSeverity::Warning,
				ERowingOperation::ReceiveTelemetry,
				"required general-status telemetry is stale");
	  Transition(ERowingConnectionState::Stale,
				 ERowingConnectionReason::TelemetryTimedOut);
	  dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
								   ReconnectTelemetryNs - StaleTelemetryNs),
					 Queue,
					 ^{
					   if (WeakLifetime.expired())
						   return;
					   std::lock_guard<std::mutex> ReconnectLock(Mutex);
					   if (Generation != LivenessGeneration || !LastGeneralStatusNs ||
						   State != ERowingConnectionState::Stale)
						   return;
					   if (MonotonicNowNs() - *LastGeneralStatusNs >= ReconnectTelemetryNs)
						   EmitFault(ERowingFaultCode::StaleTelemetry,
									 ERowingFaultSeverity::Warning,
									 ERowingOperation::ReceiveTelemetry,
									 "required general-status telemetry remains stale; device action is blocked");
					 });
	});
}

void FMacMachine::BeginReconnect()
{
	if (State != ERowingConnectionState::Ready &&
		State != ERowingConnectionState::Stale &&
		State != ERowingConnectionState::DiagnosticOnly)
		return;
	if (!ReconnectInProgress)
	{
		ReconnectInProgress = true;
		ReconnectAttempts = 0;
		ReconnectStartedNs = MonotonicNowNs();
	}
	ResetHandshakeState();
	Transition(ERowingConnectionState::Reconnecting,
			   ERowingConnectionReason::ReconnectStarted);
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	[Central cancelPeripheralConnection:Delegate.Peripheral];
	ScheduleReconnectAttempt();
}

void FMacMachine::ScheduleReconnectAttempt()
{
	if (State != ERowingConnectionState::Reconnecting ||
		ReconnectConnectInFlight || ReconnectAttemptScheduled)
		return;
	ReconnectAttemptScheduled = true;
	const std::weak_ptr<int> WeakLifetime = LifetimeToken;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), Queue, ^{
	  if (WeakLifetime.expired())
		  return;
	  std::lock_guard<std::mutex> Lock(Mutex);
	  ReconnectAttemptScheduled = false;
	  StartReconnectAttempt();
	});
}

void FMacMachine::StartReconnectAttempt()
{
	if (State != ERowingConnectionState::Reconnecting || ReconnectConnectInFlight)
		return;
	if (ReconnectAttempts >= MaxReconnectAttempts)
	{
		ReconnectInProgress = false;
		EmitFault(ERowingFaultCode::ConnectionTimeout,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::Reconnect,
				  "selected peripheral did not reconnect within bounded attempts");
		Transition(ERowingConnectionState::Failed,
				   ERowingConnectionReason::OperationFailed);
		return;
	}
	++ReconnectAttempts;
	ReconnectConnectInFlight = true;
	VIRPM5PeripheralDelegate *Delegate =
		static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
	[Central connectPeripheral:Delegate.Peripheral options:nil];
	const std::uint64_t Attempt = ReconnectAttempts;
	const std::weak_ptr<int> WeakLifetime = LifetimeToken;
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), Queue, ^{
	  if (WeakLifetime.expired())
		  return;
	  std::lock_guard<std::mutex> Lock(Mutex);
	  if (State != ERowingConnectionState::Reconnecting ||
		  !ReconnectConnectInFlight || ReconnectAttempts != Attempt)
		  return;
	  ReconnectConnectInFlight = false;
	  ScheduleReconnectAttempt();
	});
}

void FMacMachine::Transition(ERowingConnectionState NewState,
							 ERowingConnectionReason Reason)
{
	if (QueueOverflowed || State == NewState)
		return;
	const ERowingConnectionState Previous = State;
	State = NewState;
	Emit(FRowingConnectionStateChanged{Previous, NewState, Reason});
}

void FMacMachine::Emit(FRowingMachineEventPayload Payload)
{
	// Reserve space for an overflow fault and terminal transition. This never
	// evicts already queued facts merely to report the overflow.
	if (QueueOverflowed || Events.size() >= MaxQueuedEvents - 2)
	{
		if (QueueOverflowed)
			return;
		QueueOverflowed = true;
		++EventQueueOverflowCount;
		const ERowingConnectionState Previous = State;
		State = ERowingConnectionState::Failed;
		EnqueueEvent(Events, EventSequence, LastEventTimestampNs, FRowingConnectionStateChanged{Previous, ERowingConnectionState::Failed, ERowingConnectionReason::OperationFailed});
		FRowingFault Fault;
		Fault.Code = ERowingFaultCode::QueueOverflow;
		Fault.Severity = ERowingFaultSeverity::Terminal;
		Fault.Operation = ERowingOperation::ReceiveTelemetry;
		Fault.ConnectionState = ERowingConnectionState::Failed;
		Fault.DiagnosticText = "adapter event queue capacity reached; session stopped";
		Fault.ExpectedValue = MaxQueuedEvents;
		Fault.ActualValue = Events.size() + 1;
		EnqueueEvent(Events, EventSequence, LastEventTimestampNs, std::move(Fault));
		VIRPM5PeripheralDelegate *Delegate =
			static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
		dispatch_async(Queue, ^{
		  [Central cancelPeripheralConnection:Delegate.Peripheral];
		});
		EventQueueHighWaterMark = static_cast<std::uint32_t>(Events.size());
		return;
	}
	EnqueueEvent(Events, EventSequence, LastEventTimestampNs, std::move(Payload));
	EventQueueHighWaterMark = std::max(
		EventQueueHighWaterMark, static_cast<std::uint32_t>(Events.size()));
}

void FMacMachine::EmitFault(ERowingFaultCode Code,
							ERowingFaultSeverity Severity,
							ERowingOperation Operation,
							const char *Diagnostic,
							std::optional<std::uint64_t> ExpectedValue,
							std::optional<std::uint64_t> ActualValue)
{
	FRowingFault Fault;
	Fault.Code = Code;
	Fault.Severity = Severity;
	Fault.Operation = Operation;
	Fault.ConnectionState = State;
	Fault.DiagnosticText = Diagnostic;
	Fault.ExpectedValue = ExpectedValue;
	Fault.ActualValue = ActualValue;
	Emit(std::move(Fault));
}

void FMacMachine::FinishIdentity()
{
	IdentityFinished = true;
	FRowingMachineInfo Info;
	Info.Manufacturer = Manufacturer;
	Info.Model = Model;
	Info.HardwareVersion = Hardware;
	Info.FirmwareVersion = Firmware;
	Info.MachineKind = *MachineKind;
	const Concept2PM::FCapabilityEvaluation Evaluation =
		Concept2PM::EvaluateCapability(
			{Model, Hardware, Firmware, *MachineKind}, Profiles);
	// Capability evaluation describes decoder potential. Public machine info
	// advertises only metrics backed by a valid notification in this connection.
	Info.SupportedMetrics = ToRowingMetricSet(ERowingMetric::None);
	Info.CapabilityProfileVersion = Evaluation.ProfileVersion;
	Info.SupportState = Evaluation.SupportState;
	Emit(Info);
	LastMachineInfo = Info;
	if (*MachineKind != ERowingMachineKind::IndoorRower)
		EmitFault(ERowingFaultCode::WrongMachineType,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::ReadIdentity,
				  "connected machine is not an indoor rower");
	else if (ExpectedReconnectIdentity &&
			 (ExpectedReconnectIdentity->MonitorModel != Model ||
			  ExpectedReconnectIdentity->HardwareRevision != Hardware ||
			  ExpectedReconnectIdentity->FirmwareRevision != Firmware ||
			  ExpectedReconnectIdentity->MachineKind != *MachineKind))
	{
		EmitFault(ERowingFaultCode::UnsupportedIdentity,
				  ERowingFaultSeverity::Terminal,
				  ERowingOperation::Reconnect,
				  "reconnected peripheral identity changed");
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::CapabilityRejected);
	}
	else if (Evaluation.Profile != nullptr &&
			 ((Evaluation.Profile->DiagnosticOnly &&
			   Evaluation.SupportState == ERowingMachineSupportState::Warn) ||
			  (!Evaluation.Profile->DiagnosticOnly &&
			   Evaluation.SupportState == ERowingMachineSupportState::Allowed)))
	{
		ActiveProfile = Evaluation.Profile;
		LastRequestedStatusPeriodMs = ActiveProfile->RequestedStatusPeriodMs;
		for (const Concept2PM::FPM5CharacteristicProfile &Characteristic :
			 ActiveProfile->Characteristics)
		{
			auto &Stats = CharacteristicDiagnostics[Characteristic.ShortId];
			Stats.Characteristic = Characteristic.ShortId;
			Stats.ApprovedPacketLengths = Characteristic.AllowedPacketLengths;
		}
		RequiredNotifyCharacteristics.clear();
		for (const Concept2PM::FPM5CharacteristicProfile &Characteristic :
			 ActiveProfile->Characteristics)
		{
			if (Characteristic.Required)
			{
				if (!Characteristic.RequiresNotify ||
					(Characteristic.ShortId != Concept2PM::GeneralStatus &&
					 Characteristic.ShortId != Concept2PM::AdditionalStatus1))
				{
					EmitFault(ERowingFaultCode::MissingCharacteristic,
							  ERowingFaultSeverity::Terminal,
							  ERowingOperation::Subscribe,
							  "profile required telemetry characteristic is unsupported by this adapter");
					Transition(ERowingConnectionState::Unsupported,
							   ERowingConnectionReason::OperationFailed);
					return;
				}
				RequiredNotifyCharacteristics.insert(Characteristic.ShortId);
			}
		}
		if (RequiredNotifyCharacteristics.count(Concept2PM::GeneralStatus) == 0 ||
			RequiredNotifyCharacteristics.count(Concept2PM::AdditionalStatus1) == 0)
		{
			EmitFault(ERowingFaultCode::MissingCharacteristic,
					  ERowingFaultSeverity::Terminal,
					  ERowingOperation::Subscribe,
					  "profile must require general status and additional status 1");
			Transition(ERowingConnectionState::Unsupported,
					   ERowingConnectionReason::OperationFailed);
			return;
		}
		if (ActiveProfile->DiagnosticOnly)
			EmitFault(ERowingFaultCode::UnsupportedIdentity,
					  ERowingFaultSeverity::Warning,
					  ERowingOperation::ReadIdentity,
					  "unverified local diagnostic profile; workout use disabled");
		ExpectedReconnectIdentity =
			Concept2PM::FPM5Identity{Model, Hardware, Firmware, *MachineKind};
		VIRPM5PeripheralDelegate *Delegate =
			static_cast<VIRPM5PeripheralDelegate *>(PeripheralDelegate);
		Transition(ERowingConnectionState::Subscribing,
				   ERowingConnectionReason::OperationStarted);
		[Delegate.Peripheral discoverServices:@[
			[CBUUID UUIDWithString:@"CE060030-43E5-11E4-916C-0800200C9A66"]
		]];
	}
	else
		EmitFault(ERowingFaultCode::UnsupportedIdentity,
				  ERowingFaultSeverity::Recoverable,
				  ERowingOperation::ReadIdentity,
				  "capability profile is not approved");
	if (ActiveProfile == nullptr)
		Transition(ERowingConnectionState::Unsupported,
				   ERowingConnectionReason::CapabilityRejected);
}

FMacDiscovery::FMacDiscovery(
	std::vector<Concept2PM::FPM5CapabilityProfile> InProfiles,
	FPM5HardwareProbeConfiguration InProbeConfiguration)
	: Profiles(std::move(InProfiles)),
	  ProbeConfiguration(std::move(InProbeConfiguration))
{
	@autoreleasepool
	{
		VIRPM5CentralDelegate *Delegate = [VIRPM5CentralDelegate new];
		Delegate.Owner = this;
		Delegate.KnownPeripherals = [NSMutableArray new];
		Queue = dispatch_queue_create("com.virtualindoorrowing.pm5.central",
									  DISPATCH_QUEUE_SERIAL);
		Delegate.Central = [[CBCentralManager alloc] initWithDelegate:Delegate
																queue:Queue];
		CentralDelegate = Delegate;
	}
}

FMacDiscovery::~FMacDiscovery()
{
	@autoreleasepool
	{
		RelaunchMachine.reset();
		VIRPM5CentralDelegate *Delegate =
			static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
		ScanRequested.store(false);
		dispatch_sync(Queue, ^{
		  [Delegate.Central stopScan];
		  Scanning = false;
		  Delegate.Owner = nullptr;
		  Delegate.Central.delegate = nil;
		  LifetimeToken.reset();
		});
	}
}

void FMacDiscovery::TryReconnectRememberedPeripheral(CBCentralManager *Central)
{
	if (RelaunchAttempted || ActiveMachine != nullptr || Central == nil)
		return;
	RelaunchAttempted = true;
	const std::optional<FRememberedPeripheral> Remembered = LoadRememberedPeripheral();
	if (!Remembered)
		return;
	NSArray<CBPeripheral *> *const Peripherals =
		[Central retrievePeripheralsWithIdentifiers:@[ Remembered->Identifier ]];
	if (Peripherals.count != 1)
	{
		EmitScanFault(ERowingFaultCode::ConnectionTimeout,
					  "Remembered PM5 is unavailable; use scan to select a device");
		return;
	}
	RelaunchMachine = std::make_unique<FMacMachine>(
		*this,
		Central,
		Queue,
		Peripherals.firstObject,
		Profiles,
		ProbeConfiguration);
	RelaunchMachine->ExpectRelaunchIdentity(Remembered->Identity);
	ActiveMachine = RelaunchMachine.get();
	RelaunchMachine->StartRelaunchReconnect();
}

void FMacDiscovery::RememberWhenReady(
	CBPeripheral *Peripheral,
	const Concept2PM::FPM5Identity &Identity)
{
	SaveRememberedPeripheral(Peripheral, Identity);
}

FRowingCommandResult FMacDiscovery::StartScan()
{
	ScanRequested.store(true);
	dispatch_async(Queue, ^{
	  VIRPM5CentralDelegate *Delegate =
		  static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
	  if (Scanning)
	  {
		  [Delegate.Central stopScan];
		  Scanning = false;
	  }
	  SeenThisScan.clear();
	  ReadinessTimeoutPending = false;
	  if (State == ERowingConnectionState::PermissionDenied ||
		  State == ERowingConnectionState::Failed)
		  Transition(ERowingConnectionState::Idle,
					 ERowingConnectionReason::UserRequested);
	  OnCentralState(Delegate.Central.state);
	});
	return {ERowingCommandResultCode::Accepted};
}

FRowingCommandResult FMacDiscovery::StopScan()
{
	ScanRequested.store(false);
	dispatch_async(Queue, ^{
	  VIRPM5CentralDelegate *Delegate =
		  static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
	  [Delegate.Central stopScan];
	  Scanning = false;
	  ReadinessTimeoutPending = false;
	  Transition(ERowingConnectionState::Idle,
				 ERowingConnectionReason::UserRequested);
	});
	return {ERowingCommandResultCode::Accepted};
}

bool FMacDiscovery::TryPollDiscoveryEvent(FRowingMachineEvent &OutEvent)
{
	std::lock_guard<std::mutex> Lock(EventMutex);
	if (Events.empty())
		return false;
	OutEvent = std::move(Events.front());
	Events.pop_front();
	return true;
}

std::unique_ptr<IRowingMachine> FMacDiscovery::TryTakeRelaunchMachine()
{
	__block FMacMachine *TransferredMachine = nullptr;
	dispatch_sync(Queue, ^{
	  TransferredMachine = RelaunchMachine.release();
	});
	return std::unique_ptr<IRowingMachine>(TransferredMachine);
}

void FMacDiscovery::ForgetRememberedMachine()
{
	NSUserDefaults *const Defaults = [NSUserDefaults standardUserDefaults];
	[Defaults removeObjectForKey:RememberedPeripheralDefaultsKey];
	[Defaults removeObjectForKey:LegacyRememberedPeripheralDefaultsKey];
	__block FMacMachine *CancelledRelaunch = nullptr;
	dispatch_sync(Queue, ^{
	  if (RelaunchMachine)
	  {
		  CancelledRelaunch = RelaunchMachine.release();
		  if (ActiveMachine == CancelledRelaunch)
			  ActiveMachine = nullptr;
	  }
	});
	// Destruction waits for CoreBluetooth cancellation and clears ActiveMachine.
	// Do it outside Queue because FMacMachine's destructor synchronizes on it.
	delete CancelledRelaunch;
}

std::unique_ptr<IRowingMachine>
FMacDiscovery::CreateMachine(const FRowingMachineId &MachineId)
{
	__block FMacMachine *CreatedMachine = nullptr;
	dispatch_sync(Queue, ^{
	  if (ActiveMachine != nullptr)
		  return;
	  const auto It =
		  std::find_if(Discovered.begin(),
					   Discovered.end(),
					   [&MachineId](const FDiscoveredPeripheral &Candidate)
					   { return Candidate.Id == MachineId; });
	  if (It == Discovered.end())
		  return;
	  VIRPM5CentralDelegate *Delegate =
		  static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
	  ScanRequested.store(false);
	  ++ScanGeneration;
	  ReadinessTimeoutPending = false;
	  if (Scanning)
	  {
		  [Delegate.Central stopScan];
		  Scanning = false;
	  }
	  Transition(ERowingConnectionState::Idle,
				 ERowingConnectionReason::UserRequested);
	  CBPeripheral *Peripheral = Delegate.KnownPeripherals[It->RetainedIndex];
	  CreatedMachine = new FMacMachine(
		  *this,
		  Delegate.Central,
		  Queue,
		  Peripheral,
		  Profiles,
		  ProbeConfiguration);
	  ActiveMachine = CreatedMachine;
	});
	return std::unique_ptr<IRowingMachine>(CreatedMachine);
}

void FMacDiscovery::OnDiscovered(CBPeripheral *Peripheral, NSInteger Rssi)
{
	if (!Scanning || !ScanRequested.load() || Peripheral == nil)
		return;
	const FRowingMachineId Id = FRowingMachineId::FromPrivateAdapterValue(
		std::string(Peripheral.identifier.UUIDString.UTF8String));
	if (std::find(SeenThisScan.begin(), SeenThisScan.end(), Id) !=
		SeenThisScan.end())
		return;
	SeenThisScan.push_back(Id);
	VIRPM5CentralDelegate *Delegate =
		static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
	auto Existing = std::find_if(
		Discovered.begin(),
		Discovered.end(),
		[&Id](const FDiscoveredPeripheral &Candidate)
		{ return Candidate.Id == Id; });
	if (Existing == Discovered.end())
	{
		[Delegate.KnownPeripherals addObject:Peripheral];
		Discovered.push_back(
			{Id, static_cast<std::size_t>(Delegate.KnownPeripherals.count - 1)});
	}
	else
		Delegate.KnownPeripherals[Existing->RetainedIndex] = Peripheral;
	FRowingMachineDescriptor Descriptor;
	Descriptor.Id = Id;
	Descriptor.DisplayLabel = "Concept2 PM candidate";
	Descriptor.SignalStrengthDbm = static_cast<std::int32_t>(Rssi);
	Emit(std::move(Descriptor));
}

void FMacDiscovery::OnCentralState(CBManagerState State)
{
	if (State == CBManagerStatePoweredOn && !ScanRequested.load())
	{
		VIRPM5CentralDelegate *Delegate =
			static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
		TryReconnectRememberedPeripheral(Delegate.Central);
		return;
	}
	if (!ScanRequested.load())
		return;
	if (State == CBManagerStateUnknown || State == CBManagerStateResetting)
	{
		if (!ReadinessTimeoutPending)
		{
			ReadinessTimeoutPending = true;
			ScheduleScanTimeout(
				ERowingFaultCode::Permission,
				"Bluetooth did not become ready; check permission and power, then scan again",
				false);
		}
		return;
	}

	VIRPM5CentralDelegate *Delegate =
		static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
	if (State == CBManagerStatePoweredOn)
	{
		if (Scanning)
			return;
		ReadinessTimeoutPending = false;
		[Delegate.Central
			scanForPeripheralsWithServices:@[
				[CBUUID
					UUIDWithString:@"CE060000-43E5-11E4-916C-0800200C9A66"]
			]
								   options:@{
									   CBCentralManagerScanOptionAllowDuplicatesKey :
										   @NO
								   }];
		Scanning = true;
		Transition(ERowingConnectionState::Scanning,
				   ERowingConnectionReason::UserRequested);
		ScheduleScanTimeout(
			ERowingFaultCode::ScanTimeout,
			"No Concept2 PM found; check that the PM5 is on its Connect screen, then scan again",
			true);
		return;
	}

	ScanRequested.store(false);
	ReadinessTimeoutPending = false;
	++ScanGeneration;
	if (Scanning)
	{
		[Delegate.Central stopScan];
		Scanning = false;
	}
	if (State == CBManagerStateUnauthorized)
	{
		Transition(ERowingConnectionState::PermissionDenied,
				   ERowingConnectionReason::OperationFailed);
		EmitScanFault(
			ERowingFaultCode::Permission,
			"Bluetooth permission is denied; allow access in System Settings");
	}
	else if (State == CBManagerStatePoweredOff)
	{
		Transition(ERowingConnectionState::Failed,
				   ERowingConnectionReason::OperationFailed);
		EmitScanFault(ERowingFaultCode::Permission,
					  "Bluetooth is powered off; turn it on and scan again");
	}
	else
	{
		Transition(ERowingConnectionState::Failed,
				   ERowingConnectionReason::OperationFailed);
		EmitScanFault(ERowingFaultCode::Permission,
					  "Bluetooth is unavailable on this Mac");
	}
}

void FMacDiscovery::EmitScanFault(ERowingFaultCode Code,
								  const char *Diagnostic)
{
	FRowingFault Fault;
	Fault.Code = Code;
	Fault.Severity = ERowingFaultSeverity::Recoverable;
	Fault.Operation = ERowingOperation::Scan;
	Fault.ConnectionState = State;
	Fault.DiagnosticText = Diagnostic;
	Emit(std::move(Fault));
}

void FMacDiscovery::ScheduleScanTimeout(ERowingFaultCode Code,
										const char *Diagnostic,
										bool OnlyIfNoCandidate)
{
	const std::uint64_t Generation = ++ScanGeneration;
	const std::weak_ptr<int> WeakLifetime = LifetimeToken;
	dispatch_after(
		dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC),
		Queue,
		^{
		  if (WeakLifetime.expired())
			  return;
		  if (!ScanRequested.load() || ScanGeneration != Generation)
			  return;
		  ScanRequested.store(false);
		  if (Scanning)
		  {
			  VIRPM5CentralDelegate *Delegate =
				  static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
			  [Delegate.Central stopScan];
			  Scanning = false;
		  }
		  ReadinessTimeoutPending = false;
		  if (OnlyIfNoCandidate && !SeenThisScan.empty())
		  {
			  Transition(ERowingConnectionState::Idle,
						 ERowingConnectionReason::UserRequested);
			  return;
		  }
		  Transition(ERowingConnectionState::Idle,
					 ERowingConnectionReason::OperationFailed);
		  EmitScanFault(Code, Diagnostic);
		});
}

void FMacDiscovery::OnConnected(CBPeripheral *Peripheral)
{
	if (ActiveMachine != nullptr && ActiveMachine->OwnsPeripheral(Peripheral))
		ActiveMachine->OnConnected();
}

void FMacDiscovery::OnConnectionFailed(CBPeripheral *Peripheral, NSError *Error)
{
	if (ActiveMachine != nullptr && ActiveMachine->OwnsPeripheral(Peripheral))
		ActiveMachine->OnConnectionFailed(Error);
}

void FMacDiscovery::OnDisconnected(CBPeripheral *Peripheral, NSError *Error)
{
	if (ActiveMachine != nullptr && ActiveMachine->OwnsPeripheral(Peripheral))
		ActiveMachine->OnDisconnected(Error);
}

void FMacDiscovery::ClearActive(FMacMachine *Machine)
{
	if (ActiveMachine == Machine)
		ActiveMachine = nullptr;
}

void FMacDiscovery::Transition(ERowingConnectionState NewState,
							   ERowingConnectionReason Reason)
{
	if (QueueOverflowed || State == NewState)
		return;
	const ERowingConnectionState Previous = State;
	State = NewState;
	Emit(FRowingConnectionStateChanged{Previous, NewState, Reason});
}

void FMacDiscovery::Emit(FRowingMachineEventPayload Payload)
{
	std::lock_guard<std::mutex> Lock(EventMutex);
	if (QueueOverflowed || Events.size() >= MaxQueuedEvents - 2)
	{
		if (QueueOverflowed)
			return;
		QueueOverflowed = true;
		ScanRequested.store(false);
		if (Scanning)
		{
			VIRPM5CentralDelegate *Delegate =
				static_cast<VIRPM5CentralDelegate *>(CentralDelegate);
			[Delegate.Central stopScan];
			Scanning = false;
		}
		const ERowingConnectionState Previous = State;
		State = ERowingConnectionState::Failed;
		EnqueueEvent(
			Events,
			EventSequence,
			LastEventTimestampNs,
			FRowingConnectionStateChanged{
				Previous,
				ERowingConnectionState::Failed,
				ERowingConnectionReason::OperationFailed});
		FRowingFault Fault;
		Fault.Code = ERowingFaultCode::QueueOverflow;
		Fault.Severity = ERowingFaultSeverity::Terminal;
		Fault.Operation = ERowingOperation::Scan;
		Fault.ConnectionState = ERowingConnectionState::Failed;
		Fault.DiagnosticText = "discovery event queue capacity reached; scan stopped";
		EnqueueEvent(Events, EventSequence, LastEventTimestampNs, std::move(Fault));
		return;
	}
	EnqueueEvent(Events, EventSequence, LastEventTimestampNs, std::move(Payload));
}

std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiscovery(
	std::vector<Concept2PM::FPM5CapabilityProfile> Profiles,
	FPM5HardwareProbeConfiguration ProbeConfiguration)
{
	return std::make_unique<FMacDiscovery>(std::move(Profiles),
										   std::move(ProbeConfiguration));
}

std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiscovery()
{
	return CreateConcept2PMDiscovery(
		Concept2PM::GetGeneratedPM5CapabilityProfiles());
}

std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMDiagnosticDiscovery()
{
	return CreateConcept2PMDiscovery(AdapterPrivateProfiles());
}

std::unique_ptr<IConcept2PMDiscovery> CreateConcept2PMHardwareProbeDiscovery(
	FPM5HardwareProbeConfiguration Configuration)
{
	Configuration.CaptureRawTelemetry = true;
	return CreateConcept2PMDiscovery(
		Concept2PM::GetGeneratedPM5CapabilityProfiles(),
		std::move(Configuration));
}
