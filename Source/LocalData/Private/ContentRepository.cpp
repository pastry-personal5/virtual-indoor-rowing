#include "LocalData/ContentRepository.h"

#include "LocalData/Schema.h"
#include "LocalData/Sqlite.h"

#include <limits>

namespace LocalData
{
	namespace
	{
		void RequireIdle(bool bWorkoutActive)
		{
			if (!ContentRuntime::ContentOperationAllowed(bWorkoutActive))
				throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::WorkoutActive, "content operation is blocked during an active workout");
		}

		std::int64_t CheckedInt64(std::uint64_t Value, const char *Name)
		{
			if (Value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
				throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::InvalidSize, std::string(Name) + " exceeds SQLite range");
			return static_cast<std::int64_t>(Value);
		}

		ContentRuntime::FInstalledContentRecord ReadContent(Private::FSqliteStatement &Statement)
		{
			ContentRuntime::FInstalledContentRecord Record;
			Record.ContentSetId = Statement.ColumnText(0);
			Record.SemanticVersion = Statement.ColumnText(1);
			Record.ManifestHashHex = Statement.ColumnText(2);
			const auto State = ContentRuntime::ParseInstalledContentState(Statement.ColumnText(3));
			if (!State)
				throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::Malformed, "stored content state is invalid");
			Record.State = *State;
			Record.RouteId = Statement.ColumnOptionalText(4).value_or("");
			Record.CatalogRevision = static_cast<std::uint64_t>(Statement.ColumnInt64(5));
			Record.IssuedAtUnixSeconds = Statement.ColumnInt64(6);
			Record.ExpiresAtUnixSeconds = Statement.ColumnInt64(7);
			Record.InstallPath = Statement.ColumnOptionalText(8).value_or("");
			Record.FailureCategory = Statement.ColumnOptionalText(9).value_or("");
			return Record;
		}

		constexpr std::string_view SelectContentColumns =
			"content_id, version, manifest_hash, status, route_id, catalog_revision, "
			"issued_at_unix_seconds, expires_at_unix_seconds, install_path, failure_category";
	} // namespace

	struct FContentRepository::FImpl
	{
		explicit FImpl(const std::filesystem::path &DatabasePath)
			: Connection(DatabasePath)
		{
			Private::EnsureSchema(Connection);
		}

		mutable Private::FSqliteConnection Connection;
	};

	FContentRepository::FContentRepository(std::filesystem::path DatabasePath)
		: Impl(std::make_unique<FImpl>(DatabasePath))
	{
	}

	FContentRepository::~FContentRepository() = default;

	void FContentRepository::SaveStaged(const ContentRuntime::FInstalledContentRecord &Record, bool bWorkoutActive)
	{
		RequireIdle(bWorkoutActive);
		if (Record.ContentSetId.empty() || Record.RouteId.empty() || Record.SemanticVersion.empty() || Record.ManifestHashHex.size() != 64 ||
			Record.CatalogRevision == 0 || Record.ExpiresAtUnixSeconds <= Record.IssuedAtUnixSeconds || Record.InstallPath.empty())
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::Malformed, "staged content record is incomplete");
		Impl->Connection.InTransaction([&]
									   {
			auto Existing = Impl->Connection.Prepare("SELECT status FROM installed_content WHERE content_id = ? AND version = ?;");
		Existing.BindText(1, Record.ContentSetId);
		Existing.BindText(2, Record.SemanticVersion);
		if (Existing.Step())
		{
			const auto State = ContentRuntime::ParseInstalledContentState(Existing.ColumnText(0));
			if (State && (*State == ContentRuntime::EInstalledContentState::Active || *State == ContentRuntime::EInstalledContentState::LastKnownGood))
				throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::Malformed, "cannot overwrite retained content with a staged record");
		}
		auto Upsert = Impl->Connection.Prepare(
			"INSERT INTO installed_content(content_id, version, manifest_hash, status, last_verified_at, route_id, catalog_revision, issued_at_unix_seconds, expires_at_unix_seconds, install_path, failure_category) "
			"VALUES(?, ?, ?, 'staged', NULL, ?, ?, ?, ?, ?, NULL) "
			"ON CONFLICT(content_id, version) DO UPDATE SET manifest_hash=excluded.manifest_hash, status='staged', last_verified_at=NULL, route_id=excluded.route_id, catalog_revision=excluded.catalog_revision, issued_at_unix_seconds=excluded.issued_at_unix_seconds, expires_at_unix_seconds=excluded.expires_at_unix_seconds, install_path=excluded.install_path, failure_category=NULL;");
		Upsert.BindText(1, Record.ContentSetId);
		Upsert.BindText(2, Record.SemanticVersion);
		Upsert.BindText(3, Record.ManifestHashHex);
		Upsert.BindText(4, Record.RouteId);
		Upsert.BindInt64(5, CheckedInt64(Record.CatalogRevision, "catalog revision"));
		Upsert.BindInt64(6, Record.IssuedAtUnixSeconds);
		Upsert.BindInt64(7, Record.ExpiresAtUnixSeconds);
		Upsert.BindText(8, Record.InstallPath);
		Upsert.Step(); });
	}

	void FContentRepository::MarkVerified(std::string_view ContentSetId, bool bWorkoutActive)
	{
		RequireIdle(bWorkoutActive);
		auto Update = Impl->Connection.Prepare(
			"UPDATE installed_content SET status='verified', last_verified_at=datetime('now'), failure_category=NULL "
			"WHERE content_id=? AND status='staged';");
		Update.BindText(1, ContentSetId);
		Update.Step();
		if (Impl->Connection.ChangedRowCount() != 1)
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::Malformed, "content is not in staged state");
	}

	void FContentRepository::ActivateVerified(std::string_view ContentSetId, bool bWorkoutActive)
	{
		RequireIdle(bWorkoutActive);
		const auto Existing = Find(ContentSetId);
		if (Existing && Existing->State == ContentRuntime::EInstalledContentState::Active)
			return;
		Impl->Connection.InTransaction([&]
									   {
		Impl->Connection.Execute("DELETE FROM installed_content WHERE status='last_known_good';");
		Impl->Connection.Execute("UPDATE installed_content SET status='last_known_good' WHERE status='active';");
		auto Activate = Impl->Connection.Prepare("UPDATE installed_content SET status='active', failure_category=NULL WHERE content_id=? AND status='verified';");
		Activate.BindText(1, ContentSetId);
		Activate.Step();
		if (Impl->Connection.ChangedRowCount() != 1)
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::Malformed, "content is not verified for activation"); });
	}

	void FContentRepository::MarkFailed(std::string_view ContentSetId, std::string FailureCategory, bool bWorkoutActive)
	{
		RequireIdle(bWorkoutActive);
		auto Update = Impl->Connection.Prepare("UPDATE installed_content SET status='failed', failure_category=? WHERE content_id=? AND status IN ('staged','verified','active','last_known_good');");
		Update.BindText(1, FailureCategory);
		Update.BindText(2, ContentSetId);
		Update.Step();
	}

	void FContentRepository::ApplyWithdrawal(std::string_view ContentSetId, bool bWorkoutActive)
	{
		RequireIdle(bWorkoutActive);
		auto Update = Impl->Connection.Prepare("UPDATE installed_content SET status='withdrawn', failure_category='withdrawn' WHERE content_id=?;");
		Update.BindText(1, ContentSetId);
		Update.Step();
	}

	std::optional<ContentRuntime::FInstalledContentRecord> FContentRepository::Find(std::string_view ContentSetId) const
	{
		auto Query = Impl->Connection.Prepare("SELECT " + std::string(SelectContentColumns) + " FROM installed_content WHERE content_id=? ORDER BY catalog_revision DESC LIMIT 1;");
		Query.BindText(1, ContentSetId);
		if (!Query.Step())
			return std::nullopt;
		return ReadContent(Query);
	}

	std::optional<ContentRuntime::FInstalledContentRecord> FContentRepository::FindByState(ContentRuntime::EInstalledContentState State) const
	{
		auto Query = Impl->Connection.Prepare("SELECT " + std::string(SelectContentColumns) + " FROM installed_content WHERE status=? LIMIT 1;");
		Query.BindText(1, ContentRuntime::InstalledContentStateName(State));
		if (!Query.Step())
			return std::nullopt;
		return ReadContent(Query);
	}

	std::vector<ContentRuntime::FInstalledContentRecord> FContentRepository::ListRetained() const
	{
		auto Query = Impl->Connection.Prepare("SELECT " + std::string(SelectContentColumns) + " FROM installed_content ORDER BY catalog_revision DESC;");
		std::vector<ContentRuntime::FInstalledContentRecord> Records;
		while (Query.Step())
			Records.push_back(ReadContent(Query));
		return Records;
	}

	std::uint64_t FContentRepository::AcceptedCatalogRevision() const
	{
		auto Query = Impl->Connection.Prepare("SELECT accepted_revision FROM content_catalog_state WHERE singleton=1;");
		return Query.Step() ? static_cast<std::uint64_t>(Query.ColumnInt64(0)) : 0;
	}

	void FContentRepository::AcceptCatalogRevision(std::uint64_t Revision, std::string ManifestHashHex)
	{
		if (Revision == 0 || ManifestHashHex.size() != 64)
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::Malformed, "catalog revision record is invalid");
		Impl->Connection.InTransaction([&]
									   {
		auto Existing = Impl->Connection.Prepare("SELECT accepted_revision, manifest_hash FROM content_catalog_state WHERE singleton=1;");
		if (Existing.Step())
		{
			const auto AcceptedRevision = static_cast<std::uint64_t>(Existing.ColumnInt64(0));
			const std::string AcceptedHash = Existing.ColumnText(1);
			if (Revision < AcceptedRevision || (Revision == AcceptedRevision && ManifestHashHex != AcceptedHash))
				throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::RevisionRollback, "catalog revision cannot move backwards or change in place");
		}
		auto Upsert = Impl->Connection.Prepare(
			"INSERT INTO content_catalog_state(singleton, accepted_revision, manifest_hash, accepted_at) VALUES(1, ?, ?, datetime('now')) "
			"ON CONFLICT(singleton) DO UPDATE SET accepted_revision=excluded.accepted_revision, manifest_hash=excluded.manifest_hash, accepted_at=excluded.accepted_at;");
		Upsert.BindInt64(1, CheckedInt64(Revision, "catalog revision"));
		Upsert.BindText(2, ManifestHashHex);
		Upsert.Step(); });
	}

	void FContentRepository::SaveDownload(const FContentDownloadRecord &Record, bool bWorkoutActive)
	{
		RequireIdle(bWorkoutActive);
		if (Record.ContentSetId.empty() || Record.PackageUrl.empty() || Record.StagingPath.empty() || Record.ExpectedSizeBytes == 0 || Record.ReceivedSizeBytes > Record.ExpectedSizeBytes)
			throw ContentRuntime::FContentValidationError(ContentRuntime::EContentError::Malformed, "download resume record is invalid");
		auto Upsert = Impl->Connection.Prepare(
			"INSERT INTO content_downloads(content_set_id, package_url, staging_path, expected_size_bytes, received_size_bytes, entity_tag, updated_at) "
			"VALUES(?, ?, ?, ?, ?, ?, datetime('now')) ON CONFLICT(content_set_id) DO UPDATE SET package_url=excluded.package_url, staging_path=excluded.staging_path, expected_size_bytes=excluded.expected_size_bytes, received_size_bytes=excluded.received_size_bytes, entity_tag=excluded.entity_tag, updated_at=excluded.updated_at;");
		Upsert.BindText(1, Record.ContentSetId);
		Upsert.BindText(2, Record.PackageUrl);
		Upsert.BindText(3, Record.StagingPath);
		Upsert.BindInt64(4, CheckedInt64(Record.ExpectedSizeBytes, "expected download size"));
		Upsert.BindInt64(5, CheckedInt64(Record.ReceivedSizeBytes, "received download size"));
		Upsert.BindText(6, Record.EntityTag);
		Upsert.Step();
	}

	std::optional<FContentDownloadRecord> FContentRepository::FindDownload(std::string_view ContentSetId) const
	{
		auto Query = Impl->Connection.Prepare("SELECT content_set_id, package_url, staging_path, expected_size_bytes, received_size_bytes, entity_tag FROM content_downloads WHERE content_set_id=?;");
		Query.BindText(1, ContentSetId);
		if (!Query.Step())
			return std::nullopt;
		return FContentDownloadRecord{Query.ColumnText(0), Query.ColumnText(1), Query.ColumnText(2), static_cast<std::uint64_t>(Query.ColumnInt64(3)), static_cast<std::uint64_t>(Query.ColumnInt64(4)), Query.ColumnText(5)};
	}

	void FContentRepository::RemoveDownload(std::string_view ContentSetId, bool bWorkoutActive)
	{
		RequireIdle(bWorkoutActive);
		auto Delete = Impl->Connection.Prepare("DELETE FROM content_downloads WHERE content_set_id=?;");
		Delete.BindText(1, ContentSetId);
		Delete.Step();
	}
} // namespace LocalData
