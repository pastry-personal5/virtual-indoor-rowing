#pragma once

#include "LocalData/BlobCipher.h"
#include "RowingCore/RowingSession.h"
#include "RowingCore/RowingTelemetry.h"
#include "RowingDevice/RowingMachineTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Local session journal. Began as the Phase 0 Milestone 4 Spike B bounded
// durability diagnostic; Phase 1 Milestone 3
// (docs/phase-1/03-milestone-3-local-data-session-journal.md) promotes it to
// the product schema and adds optional at-rest encryption. Depends only on
// RowingCore types and the IBlobCipher seam; no Apple/SQLite/Protobuf types
// appear here.
namespace LocalData
{
	enum class EJournalEventKind : std::uint8_t
	{
		Started,
		Completed,
		Interrupted,
		Aborted,
		RecoveredAfterUncleanExit,
		// Phase 1 Milestone 4: non-terminal facts journaled by WorkoutRuntime.
		// ScanAndRecover treats any kind other than Completed/Interrupted/
		// Aborted as non-terminal, so these never mask a missing terminal event.
		CapabilityObserved,
		LinkGap
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

	// A sessions row. Session identity is the 16-byte FRowingSessionId; the
	// v1 journal tables still key by its canonical string form.
	struct FSessionRecord
	{
		FRowingSessionId Id;
		std::string UserScope;
		std::optional<std::string> PlanId;
		std::optional<std::string> RouteId;
		ERowingSessionState State = ERowingSessionState::Created;
		std::optional<std::string> StartedAtUtc;
		std::optional<std::string> Timezone;
		std::string Source;
		std::optional<std::string> ContentHash;
	};

	// A session_summaries revision. MetricsPayload is an opaque, caller-encoded
	// blob; it is always sealed before it reaches the database.
	struct FSessionSummary
	{
		FRowingSessionId Id;
		std::uint32_t Revision = 1;
		std::string MetricsPayload;
		std::uint32_t QualityFlags = 0;
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
		// Cipher is not owned and must outlive the writer. With a cipher,
		// sample chunks are sealed at rest; without one they are written in
		// plaintext (Spike B behavior). Session summaries always require a cipher.
		explicit FLocalDataJournalWriter(std::filesystem::path DatabasePath, IBlobCipher *Cipher = nullptr);
		~FLocalDataJournalWriter();

		FLocalDataJournalWriter(const FLocalDataJournalWriter &) = delete;
		FLocalDataJournalWriter &
		operator=(const FLocalDataJournalWriter &) = delete;

		// One second's worth of samples, checksummed and committed in its
		// own transaction. Equivalent to StageChunk() + CommitStagedChunk().
		void AppendChunk(const FSampleChunk &Chunk);

		// A session lifecycle marker (Started/Interrupted/Aborted/etc.),
		// committed in its own transaction. With a cipher, a non-empty
		// PayloadBlob is sealed (associated data binds session, sequence and
		// kind); an empty one is stored as-is. There is no journal-event reader
		// yet, so a future one must apply the same rule.
		void RecordJournalEvent(const FJournalEvent &Event);

		// Journals the observed machine identity/capabilities as a
		// CapabilityObserved event whose payload is the rowing.v1
		// DeviceCapabilityObserved wire message (sealed like any other payload
		// when a cipher is configured).
		void RecordCapabilityObserved(const std::string &SessionId,
									  std::uint64_t Sequence,
									  std::uint64_t MonotonicNs,
									  const FRowingMachineInfo &Info);

		// Two-phase primitives, diagnostic-only: they let the Milestone 4
		// Spike B harness (Tools/durability-spike) crash the process
		// between a write and its commit, to prove the write never
		// survives a kill at that exact boundary. At most one transaction
		// may be staged at a time.
		void StageChunk(const FSampleChunk &Chunk);
		// A commit that fails while SQLite keeps the transaction open (e.g.
		// SQLITE_BUSY) throws but leaves the stage intact; call the same commit
		// again to retry. Staging anything else meanwhile fails.
		void CommitStagedChunk();
		void StageFinalEvent(const FJournalEvent &Event);
		void CommitStagedFinalEvent();

		// Inserts a sessions row in its own transaction. Fails if the session
		// already exists.
		void CreateSession(const FSessionRecord &Session);

		// Updates sessions.state in its own transaction. Throws if the session
		// does not exist or is already Ended (Ended is terminal). Transition
		// legality otherwise belongs to FRowingSessionStateMachine, not here.
		void UpdateSessionState(const FRowingSessionId &Id, ERowingSessionState State);

		// Two-phase final-summary write, same staged-commit shape as
		// StageFinalEvent. Throws if no cipher was supplied. Revisions are
		// append-only: re-staging an existing (session, revision) fails.
		void StageSessionSummary(const FSessionSummary &Summary);
		void CommitStagedSessionSummary();

		// Rolls back whatever is staged and forgets it, so a commit that keeps
		// failing cannot wedge every later write. A no-op when nothing is staged.
		void AbandonStaged() noexcept;

		// Closes the connection. A durability barrier: every prior
		// AppendChunk/RecordJournalEvent call has already committed, so
		// this only releases the handle.
		void Close();

	  private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	// Read-back for tests/verification; not part of the crash-recovery path.
	// Cipher is required to read chunks written with one; a sealed chunk read
	// without a cipher throws FBlobCipherError. When a cipher is supplied, a
	// plaintext ("raw-v1") chunk also throws unless bAllowLegacyPlaintext is
	// set, because the codec column is unauthenticated and would otherwise let
	// anyone with database write access substitute a forged plaintext chunk.
	// Set it only to read databases that predate encryption (Spike B).
	// An unrecognized codec always throws.
	std::vector<FSampleChunk>
	ReadSampleChunks(const std::filesystem::path &DatabasePath,
					 const std::string &SessionId,
					 IBlobCipher *Cipher = nullptr,
					 bool bAllowLegacyPlaintext = false);

	std::optional<FSessionRecord>
	ReadSession(const std::filesystem::path &DatabasePath, const FRowingSessionId &Id);

	// Returns the highest revision, or nullopt if none. Requires the cipher the
	// summary was sealed with.
	std::optional<FSessionSummary>
	ReadLatestSessionSummary(const std::filesystem::path &DatabasePath, const FRowingSessionId &Id, IBlobCipher &Cipher);

	struct FLocalDataRecoveryReport
	{
		std::uint64_t HighestVerifiedSequence = 0;
		std::uint64_t TruncatedChunkCount = 0;
		std::uint64_t DuplicateChunkCount = 0;
		bool RecoveredAfterUncleanExit = false;
		// Sessions whose recovery marker this scan recorded. Unlike
		// RecoveredAfterUncleanExit (true on every later scan too, by design), this is
		// zero on a repeated scan, so a caller can tell a fresh recovery from an old one.
		std::uint64_t NewlyRecoveredSessionCount = 0;
	};

	// Scans every sample_chunks row in ascending first_sequence order,
	// validates each row's CRC32C, deletes an incomplete/corrupt trailing
	// row rather than the whole session (truncate-incomplete-tail), and
	// deletes any row whose sequence range duplicates one already kept
	// (deduplicate-on-reconciliation). If the last journal_events entry for
	// a session is Started with no later terminal event, records a
	// RecoveredAfterUncleanExit marker event, moves that session's sessions row
	// (if any) to Ended in the same transaction, and sets the report flag.
	// This applies to every session in journal_events, not just the last one.
	// A session whose terminal event committed but whose sessions row is still
	// open (crash between the two) has the row moved to Ended.
	// Idempotent: safe to call again on an already-recovered file.
	FLocalDataRecoveryReport
	ScanAndRecover(const std::filesystem::path &DatabasePath);
} // namespace LocalData
