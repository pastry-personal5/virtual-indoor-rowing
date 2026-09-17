#pragma once

#include "RowingCore/RowingTelemetry.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Phase 0 Milestone 4 Spike B (docs/phase-0/07-milestone-4-spikes.md): a
// bounded local-durability diagnostic, not the product session journal.
// Schema is limited to schema_migrations/journal_events/sample_chunks; the
// full sessions/sync_outbox/cloud_links tables from
// docs/architecture/03-macos-unreal-client.md remain planned for a later
// phase. Depends only on RowingCore telemetry types; nothing depends on this
// module yet.
namespace LocalData
{
	enum class EJournalEventKind : std::uint8_t
	{
		Started,
		Completed,
		Interrupted,
		Aborted,
		RecoveredAfterUncleanExit
	};

	struct FJournalEvent
	{
		std::string SessionId;
		std::uint64_t Sequence = 0;
		std::uint64_t MonotonicNs = 0;
		EJournalEventKind Kind = EJournalEventKind::Started;
		std::uint32_t PayloadVersion = 1;
		std::string PayloadBlob;
	};

	struct FSampleChunk
	{
		std::string SessionId;
		std::uint64_t FirstSequence = 0;
		std::uint64_t LastSequence = 0;
		std::vector<FRowingMetricSample> Samples;
	};

	// Owns one SQLite WAL-mode connection. Bootstraps schema_migrations on
	// first open. Every public method commits its own transaction before
	// returning, except the explicit two-phase final-summary pair below,
	// which exists only so the Milestone 4 Spike B harness
	// (Tools/durability-spike) can deterministically simulate a process
	// crash after the final-summary write but before its commit.
	class FLocalDataJournalWriter
	{
	  public:
		explicit FLocalDataJournalWriter(std::filesystem::path DatabasePath);
		~FLocalDataJournalWriter();

		FLocalDataJournalWriter(const FLocalDataJournalWriter &) = delete;
		FLocalDataJournalWriter &
		operator=(const FLocalDataJournalWriter &) = delete;

		// One second's worth of samples, checksummed and committed in its
		// own transaction. Equivalent to StageChunk() + CommitStagedChunk().
		void AppendChunk(const FSampleChunk &Chunk);

		// A session lifecycle marker (Started/Interrupted/Aborted/etc.),
		// committed in its own transaction.
		void RecordJournalEvent(const FJournalEvent &Event);

		// Two-phase primitives, diagnostic-only: they let the Milestone 4
		// Spike B harness (Tools/durability-spike) crash the process
		// between a write and its commit, to prove the write never
		// survives a kill at that exact boundary. At most one transaction
		// may be staged at a time.
		void StageChunk(const FSampleChunk &Chunk);
		void CommitStagedChunk();
		void StageFinalEvent(const FJournalEvent &Event);
		void CommitStagedFinalEvent();

		// Closes the connection. A durability barrier: every prior
		// AppendChunk/RecordJournalEvent call has already committed, so
		// this only releases the handle.
		void Close();

	  private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	// Read-back for tests/verification; not part of the crash-recovery path.
	std::vector<FSampleChunk>
	ReadSampleChunks(const std::filesystem::path &DatabasePath,
					 const std::string &SessionId);

	struct FLocalDataRecoveryReport
	{
		std::uint64_t HighestVerifiedSequence = 0;
		std::uint64_t TruncatedChunkCount = 0;
		std::uint64_t DuplicateChunkCount = 0;
		bool RecoveredAfterUncleanExit = false;
	};

	// Scans every sample_chunks row in ascending first_sequence order,
	// validates each row's CRC32C, deletes an incomplete/corrupt trailing
	// row rather than the whole session (truncate-incomplete-tail), and
	// deletes any row whose sequence range duplicates one already kept
	// (deduplicate-on-reconciliation). If the last journal_events entry for
	// a session is Started with no later terminal event, records a
	// RecoveredAfterUncleanExit marker event and sets the report flag.
	// Idempotent: safe to call again on an already-recovered file.
	FLocalDataRecoveryReport
	ScanAndRecover(const std::filesystem::path &DatabasePath);
} // namespace LocalData
