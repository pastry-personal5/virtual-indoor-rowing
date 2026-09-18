#include "LocalData/TelemetryWireMapping.h"

#include "rowing/v1/telemetry.pb.h"

namespace LocalData::Private
{
	namespace
	{
		namespace Wire = rowing::v1;

		[[noreturn]] void ThrowUnmapped(const char *const Field)
		{
			throw FWireMappingError(std::string("unspecified or unknown wire enumerator for ") + Field);
		}

		Wire::WorkoutState ToWire(ERowingWorkoutState Value)
		{
			switch (Value)
			{
			case ERowingWorkoutState::Unknown:
				return Wire::WORKOUT_STATE_UNKNOWN;
			case ERowingWorkoutState::WaitingToBegin:
				return Wire::WORKOUT_STATE_WAITING_TO_BEGIN;
			case ERowingWorkoutState::Active:
				return Wire::WORKOUT_STATE_ACTIVE;
			case ERowingWorkoutState::Paused:
				return Wire::WORKOUT_STATE_PAUSED;
			case ERowingWorkoutState::Resting:
				return Wire::WORKOUT_STATE_RESTING;
			case ERowingWorkoutState::Complete:
				return Wire::WORKOUT_STATE_COMPLETE;
			case ERowingWorkoutState::Terminated:
				return Wire::WORKOUT_STATE_TERMINATED;
			}
			ThrowUnmapped("workout_state");
		}

		ERowingWorkoutState FromWire(Wire::WorkoutState Value)
		{
			switch (Value)
			{
			case Wire::WORKOUT_STATE_UNKNOWN:
				return ERowingWorkoutState::Unknown;
			case Wire::WORKOUT_STATE_WAITING_TO_BEGIN:
				return ERowingWorkoutState::WaitingToBegin;
			case Wire::WORKOUT_STATE_ACTIVE:
				return ERowingWorkoutState::Active;
			case Wire::WORKOUT_STATE_PAUSED:
				return ERowingWorkoutState::Paused;
			case Wire::WORKOUT_STATE_RESTING:
				return ERowingWorkoutState::Resting;
			case Wire::WORKOUT_STATE_COMPLETE:
				return ERowingWorkoutState::Complete;
			case Wire::WORKOUT_STATE_TERMINATED:
				return ERowingWorkoutState::Terminated;
			default:
				ThrowUnmapped("workout_state");
			}
		}

		Wire::RowingState ToWire(ERowingState Value)
		{
			switch (Value)
			{
			case ERowingState::Unknown:
				return Wire::ROWING_STATE_UNKNOWN;
			case ERowingState::Inactive:
				return Wire::ROWING_STATE_INACTIVE;
			case ERowingState::Active:
				return Wire::ROWING_STATE_ACTIVE;
			}
			ThrowUnmapped("rowing_state");
		}

		ERowingState FromWire(Wire::RowingState Value)
		{
			switch (Value)
			{
			case Wire::ROWING_STATE_UNKNOWN:
				return ERowingState::Unknown;
			case Wire::ROWING_STATE_INACTIVE:
				return ERowingState::Inactive;
			case Wire::ROWING_STATE_ACTIVE:
				return ERowingState::Active;
			default:
				ThrowUnmapped("rowing_state");
			}
		}

		Wire::StrokeState ToWire(ERowingStrokeState Value)
		{
			switch (Value)
			{
			case ERowingStrokeState::Unknown:
				return Wire::STROKE_STATE_UNKNOWN;
			case ERowingStrokeState::Waiting:
				return Wire::STROKE_STATE_WAITING;
			case ERowingStrokeState::Drive:
				return Wire::STROKE_STATE_DRIVE;
			case ERowingStrokeState::Dwell:
				return Wire::STROKE_STATE_DWELL;
			case ERowingStrokeState::Recovery:
				return Wire::STROKE_STATE_RECOVERY;
			}
			ThrowUnmapped("stroke_state");
		}

		ERowingStrokeState FromWire(Wire::StrokeState Value)
		{
			switch (Value)
			{
			case Wire::STROKE_STATE_UNKNOWN:
				return ERowingStrokeState::Unknown;
			case Wire::STROKE_STATE_WAITING:
				return ERowingStrokeState::Waiting;
			case Wire::STROKE_STATE_DRIVE:
				return ERowingStrokeState::Drive;
			case Wire::STROKE_STATE_DWELL:
				return ERowingStrokeState::Dwell;
			case Wire::STROKE_STATE_RECOVERY:
				return ERowingStrokeState::Recovery;
			default:
				ThrowUnmapped("stroke_state");
			}
		}

		Wire::MachineKind ToWire(ERowingMachineKind Value)
		{
			switch (Value)
			{
			case ERowingMachineKind::Unknown:
				return Wire::MACHINE_KIND_UNKNOWN;
			case ERowingMachineKind::IndoorRower:
				return Wire::MACHINE_KIND_INDOOR_ROWER;
			case ERowingMachineKind::SkiErg:
				return Wire::MACHINE_KIND_SKI_ERG;
			case ERowingMachineKind::BikeErg:
				return Wire::MACHINE_KIND_BIKE_ERG;
			}
			ThrowUnmapped("machine_kind");
		}

		ERowingMachineKind FromWire(Wire::MachineKind Value)
		{
			switch (Value)
			{
			case Wire::MACHINE_KIND_UNKNOWN:
				return ERowingMachineKind::Unknown;
			case Wire::MACHINE_KIND_INDOOR_ROWER:
				return ERowingMachineKind::IndoorRower;
			case Wire::MACHINE_KIND_SKI_ERG:
				return ERowingMachineKind::SkiErg;
			case Wire::MACHINE_KIND_BIKE_ERG:
				return ERowingMachineKind::BikeErg;
			default:
				ThrowUnmapped("machine_kind");
			}
		}

		Wire::MachineSupportState ToWire(ERowingMachineSupportState Value)
		{
			switch (Value)
			{
			case ERowingMachineSupportState::Allowed:
				return Wire::MACHINE_SUPPORT_STATE_ALLOWED;
			case ERowingMachineSupportState::Warn:
				return Wire::MACHINE_SUPPORT_STATE_WARN;
			case ERowingMachineSupportState::Blocked:
				return Wire::MACHINE_SUPPORT_STATE_BLOCKED;
			}
			ThrowUnmapped("support_state");
		}

		ERowingMachineSupportState FromWire(Wire::MachineSupportState Value)
		{
			switch (Value)
			{
			case Wire::MACHINE_SUPPORT_STATE_ALLOWED:
				return ERowingMachineSupportState::Allowed;
			case Wire::MACHINE_SUPPORT_STATE_WARN:
				return ERowingMachineSupportState::Warn;
			case Wire::MACHINE_SUPPORT_STATE_BLOCKED:
				return ERowingMachineSupportState::Blocked;
			default:
				ThrowUnmapped("support_state");
			}
		}

		void RequireSupportedVersion(std::uint32_t Version)
		{
			if (Version != TelemetryContractVersion)
			{
				throw FWireMappingError("unsupported telemetry contract_version " + std::to_string(Version));
			}
		}
	} // namespace

	std::string SerializeMetricSample(const FRowingMetricSample &Sample)
	{
		Wire::MetricSampled Message;
		Message.set_contract_version(TelemetryContractVersion);
		Message.set_sequence(Sample.Sequence);
		Message.set_source_elapsed_ms(Sample.SourceElapsedMs);
		Message.set_received_monotonic_ns(Sample.ReceivedMonotonicNs);
		Message.set_distance_mm(Sample.DistanceMm);
		if (Sample.SpeedMmPerS)
			Message.set_speed_mm_per_s(*Sample.SpeedMmPerS);
		if (Sample.PaceMsPer500M)
			Message.set_pace_ms_per_500m(*Sample.PaceMsPer500M);
		if (Sample.StrokeRateDeciSpm)
			Message.set_stroke_rate_deci_spm(*Sample.StrokeRateDeciSpm);
		if (Sample.StrokePowerW)
			Message.set_stroke_power_w(*Sample.StrokePowerW);
		if (Sample.AveragePowerW)
			Message.set_average_power_w(*Sample.AveragePowerW);
		if (Sample.Calories)
			Message.set_calories(*Sample.Calories);
		if (Sample.HeartRateBpm)
			Message.set_heart_rate_bpm(*Sample.HeartRateBpm);
		if (Sample.DragFactor)
			Message.set_drag_factor(*Sample.DragFactor);
		if (Sample.StrokeCount)
			Message.set_stroke_count(*Sample.StrokeCount);
		Message.set_workout_state(ToWire(Sample.WorkoutState));
		Message.set_rowing_state(ToWire(Sample.RowingState));
		Message.set_stroke_state(ToWire(Sample.StrokeState));
		Message.set_quality_flags(Sample.QualityFlags);
		return Message.SerializeAsString();
	}

	FRowingMetricSample ParseMetricSample(const std::string &Bytes)
	{
		Wire::MetricSampled Message;
		if (!Message.ParseFromString(Bytes))
		{
			throw FWireMappingError("malformed MetricSampled bytes");
		}
		RequireSupportedVersion(Message.contract_version());

		FRowingMetricSample Sample;
		Sample.Sequence = Message.sequence();
		Sample.SourceElapsedMs = Message.source_elapsed_ms();
		Sample.ReceivedMonotonicNs = Message.received_monotonic_ns();
		Sample.DistanceMm = Message.distance_mm();
		if (Message.has_speed_mm_per_s())
			Sample.SpeedMmPerS = Message.speed_mm_per_s();
		if (Message.has_pace_ms_per_500m())
			Sample.PaceMsPer500M = Message.pace_ms_per_500m();
		if (Message.has_stroke_rate_deci_spm())
			Sample.StrokeRateDeciSpm = Message.stroke_rate_deci_spm();
		if (Message.has_stroke_power_w())
			Sample.StrokePowerW = Message.stroke_power_w();
		if (Message.has_average_power_w())
			Sample.AveragePowerW = Message.average_power_w();
		if (Message.has_calories())
			Sample.Calories = Message.calories();
		if (Message.has_heart_rate_bpm())
			Sample.HeartRateBpm = Message.heart_rate_bpm();
		if (Message.has_drag_factor())
			Sample.DragFactor = Message.drag_factor();
		if (Message.has_stroke_count())
			Sample.StrokeCount = Message.stroke_count();
		Sample.WorkoutState = FromWire(Message.workout_state());
		Sample.RowingState = FromWire(Message.rowing_state());
		Sample.StrokeState = FromWire(Message.stroke_state());
		Sample.QualityFlags = Message.quality_flags();
		return Sample;
	}

	std::string SerializeMachineInfo(const FRowingMachineInfo &Info)
	{
		Wire::DeviceCapabilityObserved Message;
		Message.set_contract_version(TelemetryContractVersion);
		Message.set_manufacturer(Info.Manufacturer);
		Message.set_model(Info.Model);
		Message.set_hardware_version(Info.HardwareVersion);
		Message.set_firmware_version(Info.FirmwareVersion);
		Message.set_machine_kind(ToWire(Info.MachineKind));
		Message.set_supported_metrics(Info.SupportedMetrics);
		Message.set_capability_profile_version(Info.CapabilityProfileVersion);
		Message.set_support_state(ToWire(Info.SupportState));
		return Message.SerializeAsString();
	}

	FRowingMachineInfo ParseMachineInfo(const std::string &Bytes)
	{
		Wire::DeviceCapabilityObserved Message;
		if (!Message.ParseFromString(Bytes))
		{
			throw FWireMappingError("malformed DeviceCapabilityObserved bytes");
		}
		RequireSupportedVersion(Message.contract_version());

		FRowingMachineInfo Info;
		Info.Manufacturer = Message.manufacturer();
		Info.Model = Message.model();
		Info.HardwareVersion = Message.hardware_version();
		Info.FirmwareVersion = Message.firmware_version();
		Info.MachineKind = FromWire(Message.machine_kind());
		Info.SupportedMetrics = Message.supported_metrics();
		Info.CapabilityProfileVersion = Message.capability_profile_version();
		Info.SupportState = FromWire(Message.support_state());
		return Info;
	}
} // namespace LocalData::Private
