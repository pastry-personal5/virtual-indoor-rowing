#include "WorkoutRuntime/LatencyStats.h"

#include "WorkoutRuntime/AppJournal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>

void FLatencyStats::Record(std::uint64_t LatencyNs)
{
	++Count;
	MaxNs = std::max(MaxNs, LatencyNs);
	if (Samples.size() < MaxRetainedSamples)
		Samples.push_back(LatencyNs);
	else
		++Dropped;
}

std::uint64_t FLatencyStats::GetPercentileNs(double Percentile) const
{
	if (Samples.empty())
		return 0;
	std::vector<std::uint64_t> Sorted = Samples;
	std::sort(Sorted.begin(), Sorted.end());
	const double Clamped = std::clamp(Percentile, 0.0, 100.0);
	// Nearest rank: the smallest sample with at least Percentile% of samples at or below it.
	const std::size_t Rank = static_cast<std::size_t>(std::ceil(Clamped / 100.0 * static_cast<double>(Sorted.size())));
	return Sorted[Rank == 0 ? 0 : Rank - 1];
}

void FLatencyStats::Reset()
{
	Samples.clear();
	Count = 0;
	Dropped = 0;
	MaxNs = 0;
}

namespace
{
	std::string Millis(std::uint64_t Ns)
	{
		char Buffer[32];
		std::snprintf(Buffer, sizeof(Buffer), "%.3f", static_cast<double>(Ns) / 1.0e6);
		return Buffer;
	}
} // namespace

std::string FLatencyStats::ToJson(const std::string &SourceRevision) const
{
	std::string Escaped;
	for (const char Character : SourceRevision)
	{
		if (Character == '"' || Character == '\\')
			Escaped += '\\';
		if (static_cast<unsigned char>(Character) >= 0x20)
			Escaped += Character;
	}
	return "{\"schema_version\":1,\"kind\":\"hud_software_latency\",\"source_revision\":\"" + Escaped + "\",\"sample_count\":" + std::to_string(Count) + ",\"retained_count\":" + std::to_string(Samples.size()) + ",\"dropped_count\":" + std::to_string(Dropped) + ",\"p50_ms\":" + Millis(GetPercentileNs(50.0)) + ",\"p95_ms\":" + Millis(GetPercentileNs(95.0)) + ",\"p99_ms\":" + Millis(GetPercentileNs(99.0)) + ",\"max_ms\":" + Millis(MaxNs) + "}\n";
}

std::string WriteOwnerOnlyFile(const std::filesystem::path &Directory, const std::string &FileName, const std::string &Content)
{
	try
	{
		EnsureOwnerOnlyDirectory(Directory);
		const std::filesystem::path Path = Directory / FileName;
		{
			std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
			if (!Out)
				return "could not open " + FileName;
			Out << Content;
			if (!Out)
				return "could not write " + FileName;
		}
		std::filesystem::permissions(Path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
		return {};
	}
	catch (const std::exception &Failure)
	{
		return Failure.what();
	}
}
