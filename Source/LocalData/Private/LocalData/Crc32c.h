#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace LocalData::Private
{
	// Castagnoli CRC-32C (polynomial 0x82F63B78), the algorithm named by
	// docs/adr/0004-offline-first-session-journal.md and
	// docs/phase-0/07-milestone-4-spikes.md for sample_chunks.crc32c.
	std::uint32_t ComputeCrc32c(std::string_view Data) noexcept;
} // namespace LocalData::Private
