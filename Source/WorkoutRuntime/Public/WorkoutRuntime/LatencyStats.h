#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Aggregated software latency for a run (Phase 1 Milestone 7): from the adapter's
// parsed-event timestamp to the HUD applying the display value on the game thread.
// It is a lower bound on notification-to-visible (it excludes BLE receive-to-parse
// and render/present) and is evidence for the preliminary latency item only, never
// a QA-002 pass. Only aggregates are ever kept: no payloads, identifiers, or serials.
class FLatencyStats
{
  public:
	// Bound on retained samples; further samples are counted but not retained.
	static constexpr std::size_t MaxRetainedSamples = 200000;

	void Record(std::uint64_t LatencyNs);
	std::uint64_t GetCount() const noexcept
	{
		return Count;
	}
	std::uint64_t GetDroppedCount() const noexcept
	{
		return Dropped;
	}
	// Nearest-rank percentile in nanoseconds over the retained samples; 0 when empty.
	std::uint64_t GetPercentileNs(double Percentile) const;
	std::uint64_t GetMaxNs() const noexcept
	{
		return MaxNs;
	}
	void Reset();

	// A small JSON object: schema_version, source revision, sample counts, and
	// p50/p95/p99/max in milliseconds.
	std::string ToJson(const std::string &SourceRevision) const;

  private:
	std::vector<std::uint64_t> Samples;
	std::uint64_t Count = 0;
	std::uint64_t Dropped = 0;
	std::uint64_t MaxNs = 0;
};

// Writes Content to Directory/FileName, creating the directory owner-only and the
// file owner read/write. Returns an empty string on success, else the error.
std::string WriteOwnerOnlyFile(const std::filesystem::path &Directory, const std::string &FileName, const std::string &Content);
