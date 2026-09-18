#include "WorkoutRuntime/SessionWireMapping.h"

#include "rowing/v1/session.pb.h"
#include "rowing/v1/session_summary.pb.h"

namespace WorkoutRuntime::Private
{
	namespace
	{
		void RequireSupportedVersion(std::uint32_t Version)
		{
			if (Version != SessionContractVersion)
				throw FSessionWireMappingError("unsupported contract_version " + std::to_string(Version));
		}
	} // namespace

	std::string SerializeSessionSummary(const FWorkoutSummary &Summary)
	{
		rowing::v1::SessionSummary Message;
		Message.set_contract_version(SessionContractVersion);
		Message.set_total_distance_mm(Summary.TotalDistanceMm);
		Message.set_elapsed_ms(Summary.ElapsedMs);
		Message.set_accepted_sample_count(Summary.AcceptedSampleCount);
		Message.set_rejected_sample_count(Summary.RejectedSampleCount);
		Message.set_gap_count(Summary.GapCount);
		Message.set_total_gap_ms(Summary.TotalGapMs);
		if (Summary.AveragePaceMsPer500M)
			Message.set_average_pace_ms_per_500m(*Summary.AveragePaceMsPer500M);
		if (Summary.AveragePowerW)
			Message.set_average_power_w(*Summary.AveragePowerW);
		if (Summary.AverageStrokeRateDeciSpm)
			Message.set_average_stroke_rate_deci_spm(*Summary.AverageStrokeRateDeciSpm);
		if (Summary.MaxHeartRateBpm)
			Message.set_max_heart_rate_bpm(*Summary.MaxHeartRateBpm);
		if (Summary.StrokeCount)
			Message.set_stroke_count(*Summary.StrokeCount);
		if (Summary.Calories)
			Message.set_calories(*Summary.Calories);
		std::string Bytes;
		if (!Message.SerializeToString(&Bytes))
			throw FSessionWireMappingError("SessionSummary failed to serialize");
		return Bytes;
	}

	FWorkoutSummary ParseSessionSummary(const std::string &Bytes)
	{
		rowing::v1::SessionSummary Message;
		if (!Message.ParseFromString(Bytes))
			throw FSessionWireMappingError("SessionSummary failed to parse");
		RequireSupportedVersion(Message.contract_version());
		FWorkoutSummary Summary;
		Summary.TotalDistanceMm = Message.total_distance_mm();
		Summary.ElapsedMs = Message.elapsed_ms();
		Summary.AcceptedSampleCount = Message.accepted_sample_count();
		Summary.RejectedSampleCount = Message.rejected_sample_count();
		Summary.GapCount = Message.gap_count();
		Summary.TotalGapMs = Message.total_gap_ms();
		if (Message.has_average_pace_ms_per_500m())
			Summary.AveragePaceMsPer500M = Message.average_pace_ms_per_500m();
		if (Message.has_average_power_w())
			Summary.AveragePowerW = Message.average_power_w();
		if (Message.has_average_stroke_rate_deci_spm())
			Summary.AverageStrokeRateDeciSpm = Message.average_stroke_rate_deci_spm();
		if (Message.has_max_heart_rate_bpm())
			Summary.MaxHeartRateBpm = Message.max_heart_rate_bpm();
		if (Message.has_stroke_count())
			Summary.StrokeCount = Message.stroke_count();
		if (Message.has_calories())
			Summary.Calories = Message.calories();
		return Summary;
	}

	std::string SerializeLinkGap(const FLinkGap &Gap)
	{
		rowing::v1::LinkGapRecorded Message;
		Message.set_contract_version(SessionContractVersion);
		Message.set_gap_duration_ms(Gap.GapDurationMs);
		Message.set_device_reported(Gap.bDeviceReported);
		std::string Bytes;
		if (!Message.SerializeToString(&Bytes))
			throw FSessionWireMappingError("LinkGapRecorded failed to serialize");
		return Bytes;
	}

	FLinkGap ParseLinkGap(const std::string &Bytes)
	{
		rowing::v1::LinkGapRecorded Message;
		if (!Message.ParseFromString(Bytes))
			throw FSessionWireMappingError("LinkGapRecorded failed to parse");
		RequireSupportedVersion(Message.contract_version());
		return FLinkGap{Message.gap_duration_ms(), Message.device_reported()};
	}
} // namespace WorkoutRuntime::Private
