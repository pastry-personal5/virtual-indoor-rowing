#include "pm5_sim/MockRowingMachine.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <utility>

namespace pm5_sim
{
	namespace
	{
		constexpr std::uint64_t MillisecondsToNanoseconds = 1000000ULL;

		bool SameInfo(const FRowingMachineInfo &Left,
					  const FRowingMachineInfo &Right)
		{
			return Left.Manufacturer == Right.Manufacturer &&
				   Left.Model == Right.Model &&
				   Left.HardwareVersion == Right.HardwareVersion &&
				   Left.FirmwareVersion == Right.FirmwareVersion &&
				   Left.MachineKind == Right.MachineKind &&
				   Left.SupportedMetrics == Right.SupportedMetrics &&
				   Left.CapabilityProfileVersion ==
					   Right.CapabilityProfileVersion &&
				   Left.SupportState == Right.SupportState;
		}

		FRowingFault
		MakeFault(ERowingFaultCode Code,
				  ERowingFaultSeverity Severity,
				  ERowingOperation Operation,
				  ERowingConnectionState State,
				  std::string Diagnostic,
				  std::optional<std::uint64_t> Expected = std::nullopt,
				  std::optional<std::uint64_t> Actual = std::nullopt)
		{
			return FRowingFault{Code,
								Severity,
								Operation,
								State,
								std::move(Diagnostic),
								Expected,
								Actual};
		}

		class FEventBuffer final
		{
		  public:
			explicit FEventBuffer(std::size_t InCapacity)
				: Capacity(std::max<std::size_t>(InCapacity, 3)),
				  OrdinaryCapacity(Capacity - 2)
			{
			}

			bool TryPush(FRowingMachineEventPayload Payload, std::uint64_t AtNs)
			{
				if (Overflowed || Events.size() >= OrdinaryCapacity)
				{
					return false;
				}
				Events.push_back(MakeEvent(std::move(Payload), AtNs));
				UpdateHighWaterMark();
				return true;
			}

			void PushReserved(FRowingMachineEventPayload Payload,
							  std::uint64_t AtNs)
			{
				if (Events.size() < Capacity)
				{
					Events.push_back(MakeEvent(std::move(Payload), AtNs));
					UpdateHighWaterMark();
				}
			}

			void MarkOverflowed() noexcept
			{
				if (!Overflowed)
				{
					++OverflowCount;
				}
				Overflowed = true;
			}

			bool IsOverflowed() const noexcept
			{
				return Overflowed;
			}

			bool TryPop(FRowingMachineEvent &OutEvent)
			{
				if (Events.empty())
				{
					return false;
				}
				OutEvent = std::move(Events.front());
				Events.pop_front();
				return true;
			}

			std::size_t Size() const noexcept
			{
				return Events.size();
			}

			std::size_t GetCapacity() const noexcept
			{
				return Capacity;
			}

			FRowingQueueDiagnostics GetDiagnostics() const noexcept
			{
				const auto ToPublicCount = [](std::size_t Count)
				{
					return static_cast<std::uint32_t>(std::min<std::size_t>(
						Count, std::numeric_limits<std::uint32_t>::max()));
				};
				return {ToPublicCount(Events.size()),
						ToPublicCount(Capacity),
						ToPublicCount(HighWaterMark),
						OverflowCount};
			}

		  private:
			void UpdateHighWaterMark() noexcept
			{
				HighWaterMark = std::max(HighWaterMark, Events.size());
			}

			FRowingMachineEvent MakeEvent(FRowingMachineEventPayload Payload,
										  std::uint64_t AtNs)
			{
				return FRowingMachineEvent{
					NextSequence++, AtNs, std::move(Payload)};
			}

			std::deque<FRowingMachineEvent> Events;
			std::size_t Capacity;
			std::size_t OrdinaryCapacity;
			std::size_t HighWaterMark = 0;
			std::uint64_t NextSequence = 1;
			std::uint64_t OverflowCount = 0;
			bool Overflowed = false;
		};
	} // namespace

	struct FMockRowingMachine::FImpl
	{
		explicit FImpl(FMockMachineScenario InScenario,
					   std::size_t Capacity,
					   std::uint64_t InStaleAfterMs,
					   std::uint64_t InBlockingWarningAfterMs)
			: Scenario(std::move(InScenario)), Events(Capacity),
			  StaleAfterMs(InStaleAfterMs),
			  BlockingWarningAfterMs(
				  std::max(InBlockingWarningAfterMs, InStaleAfterMs))
		{
		}

		void Transition(ERowingConnectionState NewState,
						ERowingConnectionReason Reason)
		{
			if (State == NewState)
			{
				return;
			}
			const auto OldState = State;
			State = NewState;
			if (!Events.TryPush(
					FRowingConnectionStateChanged{OldState, NewState, Reason},
					NowNs))
			{
				Overflow(OldState);
			}
		}

		void Emit(FRowingMachineEventPayload Payload)
		{
			if (!Events.TryPush(std::move(Payload), NowNs))
			{
				Overflow(State);
			}
		}

		void Overflow(ERowingConnectionState Previous)
		{
			if (Events.IsOverflowed())
			{
				return;
			}
			Events.MarkOverflowed();
			State = ERowingConnectionState::Failed;
			Events.PushReserved(
				FRowingConnectionStateChanged{
					Previous,
					ERowingConnectionState::Failed,
					ERowingConnectionReason::OperationFailed},
				NowNs);
			Events.PushReserved(
				MakeFault(
					ERowingFaultCode::QueueOverflow,
					ERowingFaultSeverity::Terminal,
					ERowingOperation::ReceiveTelemetry,
					ERowingConnectionState::Failed,
					"Synthetic event queue capacity exceeded; stream stopped.",
					Events.GetCapacity(),
					Events.Size() + 1),
				NowNs);
		}

		bool FailReadiness(ERowingFaultCode Code,
						   ERowingOperation Operation,
						   const char *Text)
		{
			Emit(MakeFault(
				Code, ERowingFaultSeverity::Terminal, Operation, State, Text));
			if (!Events.IsOverflowed())
			{
				Transition(ERowingConnectionState::Unsupported,
						   ERowingConnectionReason::CapabilityRejected);
			}
			return false;
		}

		bool ValidateProfile(const FRowingMachineInfo &Info,
							 bool IdentityReadable,
							 bool IdentityWellFormed,
							 bool ServicesPresent,
							 bool CharacteristicsPresent,
							 bool PropertiesValid,
							 bool StatusValid,
							 bool Status1Valid)
		{
			if (!ServicesPresent)
			{
				return FailReadiness(ERowingFaultCode::MissingService,
									 ERowingOperation::Discover,
									 "Required synthetic service is absent.");
			}
			if (!CharacteristicsPresent)
			{
				return FailReadiness(
					ERowingFaultCode::MissingCharacteristic,
					ERowingOperation::Discover,
					"Required synthetic characteristic is absent.");
			}
			if (!IdentityReadable)
			{
				return FailReadiness(ERowingFaultCode::UnsupportedIdentity,
									 ERowingOperation::ReadIdentity,
									 "Synthetic identity could not be read.");
			}
			if (!IdentityWellFormed)
			{
				return FailReadiness(
					ERowingFaultCode::UnsupportedIdentity,
					ERowingOperation::ReadIdentity,
					"Synthetic identity fields are malformed.");
			}
			Emit(Info);
			if (Info.MachineKind != ERowingMachineKind::IndoorRower)
			{
				return FailReadiness(
					ERowingFaultCode::WrongMachineType,
					ERowingOperation::ReadIdentity,
					"Synthetic machine kind is not an indoor rower.");
			}
			if (Info.SupportState != ERowingMachineSupportState::Allowed)
			{
				return FailReadiness(
					ERowingFaultCode::UnsupportedIdentity,
					ERowingOperation::ReadIdentity,
					"Synthetic identity is outside the allowed profile.");
			}
			if (!PropertiesValid)
			{
				return FailReadiness(
					ERowingFaultCode::InvalidProperty,
					ERowingOperation::Subscribe,
					"Synthetic required property is unavailable.");
			}
			if (!StatusValid || !Status1Valid)
			{
				return FailReadiness(ERowingFaultCode::InvalidValue,
									 ERowingOperation::Subscribe,
									 "Synthetic readiness status is invalid.");
			}
			return !Events.IsOverflowed();
		}

		FMockMachineScenario Scenario;
		FEventBuffer Events;
		ERowingConnectionState State = ERowingConnectionState::Idle;
		std::uint64_t NowNs = 0;
		std::uint64_t StaleAfterMs;
		std::uint64_t BlockingWarningAfterMs;
		std::uint64_t LastStatusNs = 0;
		std::uint64_t NextSampleSequence = 1;
		std::optional<FRowingMetricSample> LastSample;
		std::optional<FMockReconnectIdentity> PendingReconnect;
		std::uint64_t PendingReconnectGapMs = 0;
		std::uint64_t LinkLossNs = 0;
		bool HasStatusTimestamp = false;
		bool StaleEventSent = false;
		bool BlockingFaultSent = false;
		bool ReconnectedSamplePending = false;
		bool IsShutdown = false;
	};

	FMockRowingMachine::FMockRowingMachine(FMockMachineScenario Scenario,
										   std::size_t EventQueueCapacity,
										   std::uint64_t StaleAfterMs,
										   std::uint64_t BlockingWarningAfterMs)
		: Impl(std::make_unique<FImpl>(std::move(Scenario),
									   EventQueueCapacity,
									   StaleAfterMs,
									   BlockingWarningAfterMs))
	{
	}

	FMockRowingMachine::~FMockRowingMachine() = default;

	FRowingCommandResult FMockRowingMachine::Connect()
	{
		if (Impl->IsShutdown)
		{
			return {ERowingCommandResultCode::Shutdown};
		}
		if (Impl->Events.IsOverflowed())
		{
			return {ERowingCommandResultCode::Shutdown};
		}
		if (Impl->State == ERowingConnectionState::Reconnecting)
		{
			Impl->Transition(ERowingConnectionState::ReadingIdentity,
							 ERowingConnectionReason::ReconnectStarted);
			if (Impl->Events.IsOverflowed())
			{
				return {ERowingCommandResultCode::Accepted};
			}

			FMockReconnectIdentity Identity;
			if (Impl->Scenario.ReconnectIdentity)
			{
				Identity = *Impl->Scenario.ReconnectIdentity;
			}
			else
			{
				Identity.Id = Impl->Scenario.Descriptor.Id;
				Identity.Info = Impl->Scenario.Info;
			}

			if (!Identity.IdentityReadable)
			{
				Impl->FailReadiness(
					ERowingFaultCode::UnsupportedIdentity,
					ERowingOperation::Reconnect,
					"Reconnect identity could not be confirmed.");
				return {ERowingCommandResultCode::Accepted};
			}
			if (!Identity.IdentityWellFormed)
			{
				Impl->FailReadiness(
					ERowingFaultCode::UnsupportedIdentity,
					ERowingOperation::ReadIdentity,
					"Reconnect identity fields are malformed.");
				return {ERowingCommandResultCode::Accepted};
			}
			if (!(Identity.Id == Impl->Scenario.Descriptor.Id) ||
				!SameInfo(Identity.Info, Impl->Scenario.Info))
			{
				const ERowingFaultCode Fault =
					Identity.Info.MachineKind == ERowingMachineKind::IndoorRower
						? ERowingFaultCode::UnsupportedIdentity
						: ERowingFaultCode::WrongMachineType;
				Impl->FailReadiness(Fault,
									ERowingOperation::Reconnect,
									"Reconnect identity does not match the "
									"selected synthetic machine.");
				return {ERowingCommandResultCode::Accepted};
			}
			if (!Impl->ValidateProfile(Identity.Info,
									   Identity.IdentityReadable,
									   Identity.IdentityWellFormed,
									   Identity.RequiredServicesPresent,
									   Identity.RequiredCharacteristicsPresent,
									   Identity.RequiredPropertiesValid,
									   Identity.InitialStatusValid,
									   Identity.AdditionalStatus1Valid))
			{
				return {ERowingCommandResultCode::Accepted};
			}

			Impl->Transition(ERowingConnectionState::Subscribing,
							 ERowingConnectionReason::OperationStarted);
			Impl->PendingReconnect = Identity;
			Impl->PendingReconnectGapMs =
				Identity.GapDurationMs != 0
					? Identity.GapDurationMs
					: (Impl->NowNs >= Impl->LinkLossNs
						   ? (Impl->NowNs - Impl->LinkLossNs) /
								 MillisecondsToNanoseconds
						   : 0);
			if (Identity.FirstSample)
			{
				PublishTelemetry(std::move(*Identity.FirstSample));
			}
			return {ERowingCommandResultCode::Accepted};
		}

		if (Impl->State != ERowingConnectionState::Idle &&
			Impl->State != ERowingConnectionState::Failed &&
			Impl->State != ERowingConnectionState::Unsupported)
		{
			return {ERowingCommandResultCode::InvalidCurrentState};
		}

		Impl->Transition(ERowingConnectionState::Connecting,
						 ERowingConnectionReason::OperationStarted);
		if (!Impl->Scenario.ConnectionSucceeds)
		{
			Impl->Emit(MakeFault(
				ERowingFaultCode::ConnectionTimeout,
				ERowingFaultSeverity::Recoverable,
				ERowingOperation::Connect,
				ERowingConnectionState::Connecting,
				"Synthetic peripheral connection failed."));
			Impl->Transition(ERowingConnectionState::Failed,
							 ERowingConnectionReason::OperationFailed);
			return {ERowingCommandResultCode::Accepted};
		}
		Impl->Transition(ERowingConnectionState::Discovering,
						 ERowingConnectionReason::OperationStarted);
		if (!Impl->Scenario.RequiredServicesPresent)
		{
			Impl->FailReadiness(ERowingFaultCode::MissingService,
								ERowingOperation::Discover,
								"Required synthetic service is absent.");
			return {ERowingCommandResultCode::Accepted};
		}
		if (!Impl->Scenario.RequiredCharacteristicsPresent)
		{
			Impl->FailReadiness(ERowingFaultCode::MissingCharacteristic,
								ERowingOperation::Discover,
								"Required synthetic characteristic is absent.");
			return {ERowingCommandResultCode::Accepted};
		}
		Impl->Transition(ERowingConnectionState::ReadingIdentity,
						 ERowingConnectionReason::OperationStarted);
		if (!Impl->Scenario.IdentityReadable)
		{
			Impl->FailReadiness(ERowingFaultCode::UnsupportedIdentity,
								ERowingOperation::ReadIdentity,
								"Synthetic identity could not be read.");
			return {ERowingCommandResultCode::Accepted};
		}
		if (!Impl->Scenario.IdentityWellFormed)
		{
			Impl->FailReadiness(
				ERowingFaultCode::UnsupportedIdentity,
				ERowingOperation::ReadIdentity,
				"Synthetic identity fields are malformed.");
			return {ERowingCommandResultCode::Accepted};
		}
		Impl->Emit(Impl->Scenario.Info);
		if (Impl->Scenario.Info.MachineKind != ERowingMachineKind::IndoorRower)
		{
			Impl->FailReadiness(
				ERowingFaultCode::WrongMachineType,
				ERowingOperation::ReadIdentity,
				"Synthetic machine kind is not an indoor rower.");
			return {ERowingCommandResultCode::Accepted};
		}
		if (Impl->Scenario.Info.SupportState !=
			ERowingMachineSupportState::Allowed)
		{
			Impl->FailReadiness(
				ERowingFaultCode::UnsupportedIdentity,
				ERowingOperation::ReadIdentity,
				"Synthetic identity is outside the allowed profile.");
			return {ERowingCommandResultCode::Accepted};
		}
		Impl->Transition(ERowingConnectionState::Subscribing,
						 ERowingConnectionReason::OperationStarted);
		if (!Impl->Scenario.RequiredPropertiesValid)
		{
			Impl->FailReadiness(ERowingFaultCode::InvalidProperty,
								ERowingOperation::Subscribe,
								"Synthetic required property is unavailable.");
			return {ERowingCommandResultCode::Accepted};
		}
		if (!Impl->Scenario.InitialStatusValid ||
			!Impl->Scenario.AdditionalStatus1Valid)
		{
			Impl->FailReadiness(ERowingFaultCode::InvalidValue,
								ERowingOperation::Subscribe,
								"Synthetic readiness status is invalid.");
			return {ERowingCommandResultCode::Accepted};
		}
		if (Impl->Events.IsOverflowed())
		{
			return {ERowingCommandResultCode::Accepted};
		}
		Impl->LastStatusNs = Impl->NowNs;
		Impl->HasStatusTimestamp = true;
		Impl->StaleEventSent = false;
		Impl->BlockingFaultSent = false;
		Impl->Transition(ERowingConnectionState::Ready,
						 ERowingConnectionReason::ReadinessConfirmed);
		return {ERowingCommandResultCode::Accepted};
	}

	FRowingCommandResult FMockRowingMachine::Disconnect()
	{
		if (Impl->IsShutdown)
		{
			return {ERowingCommandResultCode::Shutdown};
		}
		if (Impl->Events.IsOverflowed())
		{
			return {ERowingCommandResultCode::Shutdown};
		}
		if (Impl->State == ERowingConnectionState::Idle)
		{
			return {ERowingCommandResultCode::InvalidCurrentState};
		}
		Impl->PendingReconnect.reset();
		Impl->ReconnectedSamplePending = false;
		Impl->Transition(ERowingConnectionState::Idle,
						 ERowingConnectionReason::UserRequested);
		return {ERowingCommandResultCode::Accepted};
	}

	ERowingConnectionState FMockRowingMachine::GetConnectionState() const
	{
		return Impl->State;
	}

	FRowingMachineDiagnostics FMockRowingMachine::GetDiagnostics() const
	{
		FRowingMachineDiagnostics Diagnostics;
		Diagnostics.EventQueue = Impl->Events.GetDiagnostics();
		return Diagnostics;
	}

	bool FMockRowingMachine::TryPollEvent(FRowingMachineEvent &OutEvent)
	{
		return Impl->Events.TryPop(OutEvent);
	}

	bool FMockRowingMachine::AdvanceTo(std::uint64_t MonotonicTimestampNs)
	{
		if (Impl->IsShutdown || Impl->Events.IsOverflowed() ||
			MonotonicTimestampNs < Impl->NowNs)
		{
			return false;
		}
		Impl->NowNs = MonotonicTimestampNs;
		if ((Impl->State != ERowingConnectionState::Ready &&
			 Impl->State != ERowingConnectionState::Stale) ||
			!Impl->HasStatusTimestamp)
		{
			return true;
		}
		const std::uint64_t AgeMs =
			(Impl->NowNs - Impl->LastStatusNs) / MillisecondsToNanoseconds;
		if (AgeMs >= Impl->StaleAfterMs &&
			Impl->State == ERowingConnectionState::Ready)
		{
			Impl->Transition(ERowingConnectionState::Stale,
							 ERowingConnectionReason::TelemetryTimedOut);
			if (!Impl->Events.IsOverflowed())
			{
				Impl->Emit(FRowingTelemetryStale{
					Impl->LastSample ? Impl->LastSample->Sequence : 0, AgeMs});
				Impl->StaleEventSent = true;
			}
		}
		if (!Impl->BlockingFaultSent && AgeMs >= Impl->BlockingWarningAfterMs)
		{
			Impl->Emit(MakeFault(ERowingFaultCode::StaleTelemetry,
								 ERowingFaultSeverity::Recoverable,
								 ERowingOperation::ReceiveTelemetry,
								 Impl->State,
								 "Synthetic required telemetry exceeded the "
								 "blocking-warning deadline.",
								 Impl->BlockingWarningAfterMs,
								 AgeMs));
			Impl->BlockingFaultSent = true;
		}
		return !Impl->Events.IsOverflowed();
	}

	bool FMockRowingMachine::PublishTelemetry(FRowingMetricSample Sample)
	{
		if (Impl->IsShutdown || Impl->Events.IsOverflowed())
		{
			return false;
		}
		if (Impl->State != ERowingConnectionState::Ready &&
			Impl->State != ERowingConnectionState::Stale &&
			!(Impl->State == ERowingConnectionState::Subscribing &&
			  Impl->PendingReconnect))
		{
			return false;
		}
		if (Impl->LastSample &&
			(Sample.SourceElapsedMs < Impl->LastSample->SourceElapsedMs ||
			 Sample.DistanceMm < Impl->LastSample->DistanceMm))
		{
			Impl->Emit(MakeFault(
				ERowingFaultCode::InvalidValue,
				ERowingFaultSeverity::Terminal,
				ERowingOperation::Reconnect,
				Impl->State,
				"Synthetic telemetry regressed across a reconnect boundary."));
			if (!Impl->Events.IsOverflowed())
			{
				Impl->Transition(ERowingConnectionState::Unsupported,
								 ERowingConnectionReason::CapabilityRejected);
			}
			return false;
		}

		if (Impl->State == ERowingConnectionState::Subscribing &&
			Impl->PendingReconnect)
		{
			const FMockReconnectIdentity Identity = *Impl->PendingReconnect;
			Impl->Emit(FRowingConnectionRestored{Identity.Info,
												 Impl->PendingReconnectGapMs});
			if (Impl->Events.IsOverflowed())
			{
				return false;
			}
			Impl->PendingReconnect.reset();
			Impl->ReconnectedSamplePending = true;
			Impl->LastStatusNs = Impl->NowNs;
			Impl->HasStatusTimestamp = true;
			Impl->StaleEventSent = false;
			Impl->BlockingFaultSent = false;
			Impl->Transition(ERowingConnectionState::Ready,
							 ERowingConnectionReason::ReadinessConfirmed);
		}
		else if (Impl->State == ERowingConnectionState::Stale)
		{
			Impl->LastStatusNs = Impl->NowNs;
			Impl->StaleEventSent = false;
			Impl->BlockingFaultSent = false;
			Impl->Transition(ERowingConnectionState::Ready,
							 ERowingConnectionReason::TelemetryResumed);
		}
		else
		{
			Impl->LastStatusNs = Impl->NowNs;
		}
		Sample.Sequence = Impl->NextSampleSequence++;
		Sample.ReceivedMonotonicNs = Impl->NowNs;
		if (Impl->ReconnectedSamplePending)
		{
			Sample.QualityFlags |=
				ToRowingQualityFlags(ERowingQualityFlag::DeviceReconnected);
			Impl->ReconnectedSamplePending = false;
		}
		Impl->LastSample = Sample;
		Impl->Emit(std::move(Sample));
		return !Impl->Events.IsOverflowed();
	}

	bool FMockRowingMachine::RejectMalformedSample(
		std::uint64_t ExpectedBytes,
		std::uint64_t ActualBytes)
	{
		if (Impl->IsShutdown || Impl->Events.IsOverflowed() ||
			(Impl->State != ERowingConnectionState::Ready &&
			 Impl->State != ERowingConnectionState::Stale))
		{
			return false;
		}

		Impl->Emit(MakeFault(
			ERowingFaultCode::InvalidPacketLength,
			ERowingFaultSeverity::Terminal,
			ERowingOperation::ReceiveTelemetry,
			Impl->State,
			"Synthetic telemetry packet length did not match its profile.",
			ExpectedBytes,
			ActualBytes));
		if (!Impl->Events.IsOverflowed())
		{
			Impl->Transition(ERowingConnectionState::Unsupported,
							 ERowingConnectionReason::OperationFailed);
		}
		return !Impl->Events.IsOverflowed();
	}

	bool FMockRowingMachine::SimulateLinkLoss()
	{
		if (Impl->IsShutdown || Impl->Events.IsOverflowed() ||
			(Impl->State != ERowingConnectionState::Ready &&
			 Impl->State != ERowingConnectionState::Stale))
		{
			return false;
		}
		Impl->Transition(ERowingConnectionState::Reconnecting,
						 ERowingConnectionReason::LinkLost);
		Impl->LinkLossNs = Impl->NowNs;
		if (!Impl->Events.IsOverflowed())
		{
			Impl->Emit(MakeFault(
				ERowingFaultCode::Disconnected,
				ERowingFaultSeverity::Recoverable,
				ERowingOperation::Reconnect,
				ERowingConnectionState::Reconnecting,
				"Synthetic link loss; official telemetry remains frozen."));
		}
		return !Impl->Events.IsOverflowed();
	}

	void FMockRowingMachine::Shutdown() noexcept
	{
		if (!Impl || Impl->IsShutdown)
		{
			return;
		}
		Impl->IsShutdown = true;
		Impl->PendingReconnect.reset();
		if (!Impl->Events.IsOverflowed())
		{
			Impl->Transition(ERowingConnectionState::Idle,
							 ERowingConnectionReason::Shutdown);
		}
	}

	std::size_t FMockRowingMachine::GetQueuedEventCount() const noexcept
	{
		return Impl->Events.Size();
	}

	std::size_t FMockRowingMachine::GetEventQueueCapacity() const noexcept
	{
		return Impl->Events.GetCapacity();
	}

	bool FMockRowingMachine::HasOverflowed() const noexcept
	{
		return Impl->Events.IsOverflowed();
	}

	struct FMockRowingMachineDiscovery::FImpl
	{
		explicit FImpl(FMockDiscoveryScenario InScenario)
			: Scenario(std::move(InScenario)),
			  Events(Scenario.EventQueueCapacity),
			  Permission(Scenario.Permission)
		{
		}

		void Transition(ERowingConnectionState NewState,
						ERowingConnectionReason Reason)
		{
			if (State == NewState)
			{
				return;
			}
			const auto Previous = State;
			State = NewState;
			if (!Events.TryPush(
					FRowingConnectionStateChanged{Previous, NewState, Reason},
					0))
			{
				Overflow(Previous);
			}
		}

		void Emit(FRowingMachineEventPayload Payload)
		{
			if (!Events.TryPush(std::move(Payload), 0))
			{
				Overflow(State);
			}
		}

		void Overflow(ERowingConnectionState Previous)
		{
			if (Events.IsOverflowed())
			{
				return;
			}
			Events.MarkOverflowed();
			State = ERowingConnectionState::Failed;
			Events.PushReserved(
				FRowingConnectionStateChanged{
					Previous, State, ERowingConnectionReason::OperationFailed},
				0);
			Events.PushReserved(MakeFault(ERowingFaultCode::QueueOverflow,
										  ERowingFaultSeverity::Terminal,
										  ERowingOperation::Scan,
										  State,
										  "Synthetic discovery queue capacity "
										  "exceeded; scan stopped.",
										  Events.GetCapacity(),
										  Events.Size() + 1),
								0);
		}

		FMockDiscoveryScenario Scenario;
		FEventBuffer Events;
		ERowingConnectionState State = ERowingConnectionState::Idle;
		EMockPermission Permission;
		std::vector<FRowingMachineId> DiscoveredIds;
		std::uint64_t ScanNowNs = 0;
		bool IsShutdown = false;
	};

	FMockRowingMachineDiscovery::FMockRowingMachineDiscovery(
		FMockDiscoveryScenario Scenario)
		: Impl(std::make_unique<FImpl>(std::move(Scenario)))
	{
	}

	FMockRowingMachineDiscovery::~FMockRowingMachineDiscovery() = default;

	FRowingCommandResult FMockRowingMachineDiscovery::StartScan()
	{
		if (Impl->IsShutdown || Impl->Events.IsOverflowed())
		{
			return {ERowingCommandResultCode::Shutdown};
		}
		if (Impl->State == ERowingConnectionState::Scanning)
		{
			return {ERowingCommandResultCode::InvalidCurrentState};
		}
		if (Impl->Permission == EMockPermission::Denied)
		{
			Impl->Transition(ERowingConnectionState::PermissionDenied,
							 ERowingConnectionReason::OperationFailed);
			Impl->Emit(MakeFault(ERowingFaultCode::Permission,
								 ERowingFaultSeverity::Recoverable,
								 ERowingOperation::Scan,
								 ERowingConnectionState::PermissionDenied,
								 "Synthetic Bluetooth permission is denied."));
			return {ERowingCommandResultCode::Accepted};
		}
		if (Impl->State == ERowingConnectionState::PermissionDenied ||
			Impl->State == ERowingConnectionState::Failed)
		{
			Impl->Transition(ERowingConnectionState::Idle,
							 ERowingConnectionReason::UserRequested);
		}
		Impl->DiscoveredIds.clear();
		Impl->ScanNowNs = 0;
		Impl->Transition(ERowingConnectionState::Scanning,
						 ERowingConnectionReason::UserRequested);
		for (const FMockMachineScenario &Machine : Impl->Scenario.Machines)
		{
			Impl->DiscoveredIds.push_back(Machine.Descriptor.Id);
			Impl->Emit(Machine.Descriptor);
			if (Impl->Events.IsOverflowed())
			{
				break;
			}
		}
		return {ERowingCommandResultCode::Accepted};
	}

	bool FMockRowingMachineDiscovery::AdvanceScanTo(
		std::uint64_t MonotonicTimestampNs)
	{
		if (Impl->IsShutdown || Impl->Events.IsOverflowed() ||
			Impl->State != ERowingConnectionState::Scanning ||
			MonotonicTimestampNs < Impl->ScanNowNs)
		{
			return false;
		}

		Impl->ScanNowNs = MonotonicTimestampNs;
		const std::uint64_t ElapsedMs =
			Impl->ScanNowNs / MillisecondsToNanoseconds;
		if (ElapsedMs < Impl->Scenario.ScanTimeoutMs)
		{
			return true;
		}

		const bool FoundCandidate = !Impl->DiscoveredIds.empty();
		Impl->Transition(ERowingConnectionState::Idle,
						 ERowingConnectionReason::UserRequested);
		if (!FoundCandidate && !Impl->Events.IsOverflowed())
		{
			Impl->Emit(MakeFault(
				ERowingFaultCode::ScanTimeout,
				ERowingFaultSeverity::Recoverable,
				ERowingOperation::Scan,
				ERowingConnectionState::Idle,
				"Synthetic scan timed out without a candidate.",
				Impl->Scenario.ScanTimeoutMs,
				ElapsedMs));
		}
		return !Impl->Events.IsOverflowed();
	}

	FRowingCommandResult FMockRowingMachineDiscovery::StopScan()
	{
		if (Impl->IsShutdown || Impl->Events.IsOverflowed())
		{
			return {ERowingCommandResultCode::Shutdown};
		}
		if (Impl->State != ERowingConnectionState::Scanning)
		{
			return {ERowingCommandResultCode::InvalidCurrentState};
		}
		Impl->Transition(ERowingConnectionState::Idle,
						 ERowingConnectionReason::UserRequested);
		return {ERowingCommandResultCode::Accepted};
	}

	bool FMockRowingMachineDiscovery::TryPollDiscoveryEvent(
		FRowingMachineEvent &OutEvent)
	{
		return Impl->Events.TryPop(OutEvent);
	}

	std::unique_ptr<IRowingMachine> FMockRowingMachineDiscovery::CreateMachine(
		const FRowingMachineId &MachineId)
	{
		if (Impl->IsShutdown || Impl->Events.IsOverflowed() ||
			Impl->Permission != EMockPermission::Granted)
		{
			return nullptr;
		}
		const auto It = std::find_if(
			Impl->Scenario.Machines.begin(),
			Impl->Scenario.Machines.end(),
			[&](const FMockMachineScenario &Candidate)
			{
				return Candidate.Descriptor.Id == MachineId &&
					   std::find(Impl->DiscoveredIds.begin(),
								 Impl->DiscoveredIds.end(),
								 MachineId) != Impl->DiscoveredIds.end();
			});
		if (It == Impl->Scenario.Machines.end())
		{
			return nullptr;
		}
		if (Impl->State == ERowingConnectionState::Scanning)
		{
			StopScan();
		}
		return std::make_unique<FMockRowingMachine>(
			*It, Impl->Scenario.EventQueueCapacity);
	}

	void FMockRowingMachineDiscovery::SetPermission(EMockPermission Permission)
	{
		if (!Impl->IsShutdown)
		{
			Impl->Permission = Permission;
		}
	}

	void FMockRowingMachineDiscovery::Shutdown() noexcept
	{
		if (!Impl || Impl->IsShutdown)
		{
			return;
		}
		Impl->IsShutdown = true;
		if (!Impl->Events.IsOverflowed())
		{
			Impl->Transition(ERowingConnectionState::Idle,
							 ERowingConnectionReason::Shutdown);
		}
	}

	std::size_t
	FMockRowingMachineDiscovery::GetQueuedEventCount() const noexcept
	{
		return Impl->Events.Size();
	}

	bool FMockRowingMachineDiscovery::HasOverflowed() const noexcept
	{
		return Impl->Events.IsOverflowed();
	}
} // namespace pm5_sim
