#pragma once

#include "LocalData/Sqlite.h"

namespace LocalData::Private
{
	// Idempotent: creates schema_migrations/journal_events/sample_chunks if
	// absent and records schema version 1. Safe to call on every open.
	void EnsureSchema(FSqliteConnection &Connection);
} // namespace LocalData::Private
