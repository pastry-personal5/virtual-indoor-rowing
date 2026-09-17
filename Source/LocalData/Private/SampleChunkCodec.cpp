#include "LocalData/SampleChunkCodec.h"

#include <cstring>
#include <type_traits>

namespace LocalData::Private
{
	namespace
	{
		template <typename T>
		void AppendRaw(std::string &Out, const T &Value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const auto *Bytes = reinterpret_cast<const char *>(&Value);
			Out.append(Bytes, sizeof(T));
		}

		template <typename T>
		T ReadRaw(const std::string &Blob, std::size_t &Offset)
		{
			T Value{};
			std::memcpy(&Value, Blob.data() + Offset, sizeof(T));
			Offset += sizeof(T);
			return Value;
		}

		enum EPresenceBit : std::uint8_t
		{
			PresenceSpeed = 1U << 0,
			PresencePace = 1U << 1,
			PresenceStrokeRate = 1U << 2,
			PresenceStrokePower = 1U << 3,
			PresenceAveragePower = 1U << 4,
			PresenceCalories = 1U << 5,
			PresenceHeartRate = 1U << 6,
			PresenceDragFactor = 1U << 7
		};

		template <typename T>
		void AppendOptional(std::uint8_t &Presence, EPresenceBit Bit, const std::optional<T> &Value, std::string &Deferred)
		{
			if (Value.has_value())
			{
				Presence |= static_cast<std::uint8_t>(Bit);
				AppendRaw(Deferred, *Value);
			}
		}
	} // namespace

	std::string EncodeSamples(const std::vector<FRowingMetricSample> &Samples)
	{
		std::string Out;
		const auto Count = static_cast<std::uint32_t>(Samples.size());
		AppendRaw(Out, Count);
		for (const FRowingMetricSample &Sample : Samples)
		{
			AppendRaw(Out, Sample.Sequence);
			AppendRaw(Out, Sample.SourceElapsedMs);
			AppendRaw(Out, Sample.ReceivedMonotonicNs);
			AppendRaw(Out, Sample.DistanceMm);

			std::uint8_t Presence = 0;
			std::string Deferred;
			AppendOptional(Presence, PresenceSpeed, Sample.SpeedMmPerS, Deferred);
			AppendOptional(Presence, PresencePace, Sample.PaceMsPer500M, Deferred);
			AppendOptional(Presence, PresenceStrokeRate, Sample.StrokeRateDeciSpm, Deferred);
			AppendOptional(Presence, PresenceStrokePower, Sample.StrokePowerW, Deferred);
			AppendOptional(Presence, PresenceAveragePower, Sample.AveragePowerW, Deferred);
			AppendOptional(Presence, PresenceCalories, Sample.Calories, Deferred);
			AppendOptional(Presence, PresenceHeartRate, Sample.HeartRateBpm, Deferred);
			AppendOptional(Presence, PresenceDragFactor, Sample.DragFactor, Deferred);
			AppendRaw(Out, Presence);
			Out += Deferred;

			const std::uint8_t StrokeCountPresent =
				Sample.StrokeCount.has_value() ? 1U : 0U;
			AppendRaw(Out, StrokeCountPresent);
			if (Sample.StrokeCount.has_value())
			{
				AppendRaw(Out, *Sample.StrokeCount);
			}

			AppendRaw(Out, Sample.WorkoutState);
			AppendRaw(Out, Sample.RowingState);
			AppendRaw(Out, Sample.StrokeState);
			AppendRaw(Out, Sample.QualityFlags);
		}
		return Out;
	}

	std::vector<FRowingMetricSample> DecodeSamples(const std::string &Blob)
	{
		std::vector<FRowingMetricSample> Samples;
		std::size_t Offset = 0;
		if (Blob.size() < sizeof(std::uint32_t))
		{
			return Samples;
		}
		const auto Count = ReadRaw<std::uint32_t>(Blob, Offset);
		Samples.reserve(Count);
		for (std::uint32_t Index = 0; Index < Count; ++Index)
		{
			FRowingMetricSample Sample;
			Sample.Sequence = ReadRaw<std::uint64_t>(Blob, Offset);
			Sample.SourceElapsedMs = ReadRaw<std::uint64_t>(Blob, Offset);
			Sample.ReceivedMonotonicNs = ReadRaw<std::uint64_t>(Blob, Offset);
			Sample.DistanceMm = ReadRaw<std::uint64_t>(Blob, Offset);

			const auto Presence = ReadRaw<std::uint8_t>(Blob, Offset);
			if ((Presence & PresenceSpeed) != 0U)
			{
				Sample.SpeedMmPerS = ReadRaw<std::uint32_t>(Blob, Offset);
			}
			if ((Presence & PresencePace) != 0U)
			{
				Sample.PaceMsPer500M = ReadRaw<std::uint32_t>(Blob, Offset);
			}
			if ((Presence & PresenceStrokeRate) != 0U)
			{
				Sample.StrokeRateDeciSpm = ReadRaw<std::uint32_t>(Blob, Offset);
			}
			if ((Presence & PresenceStrokePower) != 0U)
			{
				Sample.StrokePowerW = ReadRaw<std::uint32_t>(Blob, Offset);
			}
			if ((Presence & PresenceAveragePower) != 0U)
			{
				Sample.AveragePowerW = ReadRaw<std::uint32_t>(Blob, Offset);
			}
			if ((Presence & PresenceCalories) != 0U)
			{
				Sample.Calories = ReadRaw<std::uint32_t>(Blob, Offset);
			}
			if ((Presence & PresenceHeartRate) != 0U)
			{
				Sample.HeartRateBpm = ReadRaw<std::uint32_t>(Blob, Offset);
			}
			if ((Presence & PresenceDragFactor) != 0U)
			{
				Sample.DragFactor = ReadRaw<std::uint32_t>(Blob, Offset);
			}

			const auto StrokeCountPresent = ReadRaw<std::uint8_t>(Blob, Offset);
			if (StrokeCountPresent != 0U)
			{
				Sample.StrokeCount = ReadRaw<std::uint64_t>(Blob, Offset);
			}

			Sample.WorkoutState = ReadRaw<ERowingWorkoutState>(Blob, Offset);
			Sample.RowingState = ReadRaw<ERowingState>(Blob, Offset);
			Sample.StrokeState = ReadRaw<ERowingStrokeState>(Blob, Offset);
			Sample.QualityFlags = ReadRaw<FRowingQualityFlags>(Blob, Offset);

			Samples.push_back(Sample);
		}
		return Samples;
	}
} // namespace LocalData::Private
