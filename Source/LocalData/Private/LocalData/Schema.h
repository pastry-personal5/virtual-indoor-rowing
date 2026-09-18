#pragma once

#include "LocalData/Sqlite.h"

namespace LocalData::Private
{
	// Idempotent: applies any missing migration in order. Version 1 creates
	// journal_events/sample_chunks; version 2 adds the remaining product
	// tables. Safe to call on every open, including on a Spike B v1 database.
	void EnsureSchema(FSqliteConnection &Connection);
} // namespace LocalData::Private
