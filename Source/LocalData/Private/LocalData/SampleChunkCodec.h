#pragma once

#include "RowingCore/RowingTelemetry.h"

#include <string>
#include <vector>

namespace LocalData::Private
{
	// Compact fixed-layout binary encoding of a chunk's samples for
	// sample_chunks.payload_blob. Spike-scoped: single-host, single-build
	// serialization, not a versioned wire format.
	std::string EncodeSamples(const std::vector<FRowingMetricSample> &Samples);
	std::vector<FRowingMetricSample> DecodeSamples(const std::string &Blob);
} // namespace LocalData::Private
