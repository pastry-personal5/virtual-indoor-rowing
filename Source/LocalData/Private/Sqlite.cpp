#include "LocalData/Sqlite.h"

namespace LocalData::Private
{
	namespace
	{
		void ThrowIfNotOk(sqlite3 *Handle, int ResultCode, std::string_view Context)
		{
			if (ResultCode != SQLITE_OK && ResultCode != SQLITE_ROW &&
				ResultCode != SQLITE_DONE)
			{
				throw FSqliteError(std::string(Context) + ": " +
								   (Handle != nullptr ? sqlite3_errmsg(Handle)
													  : sqlite3_errstr(ResultCode)));
			}
		}

		// Concurrent connections (a writer plus read/recovery openers) wait for
		// each other's short transactions instead of failing with SQLITE_BUSY.
		constexpr int BusyTimeoutMs = 5000;
	} // namespace

	void FSqliteStatement::BindInt64(int Index, std::int64_t Value)
	{
		sqlite3_bind_int64(Statement, Index, Value);
	}

	void FSqliteStatement::BindText(int Index, std::string_view Value)
	{
		sqlite3_bind_text(Statement, Index, Value.data(), static_cast<int>(Value.size()), SQLITE_TRANSIENT);
	}

	void FSqliteStatement::BindBlob(int Index, std::string_view Value)
	{
		sqlite3_bind_blob(Statement, Index, Value.data(), static_cast<int>(Value.size()), SQLITE_TRANSIENT);
	}

	void FSqliteStatement::BindNull(int Index)
	{
		sqlite3_bind_null(Statement, Index);
	}

	void FSqliteStatement::BindOptionalText(int Index, const std::optional<std::string> &Value)
	{
		if (Value.has_value())
		{
			BindText(Index, *Value);
		}
		else
		{
			BindNull(Index);
		}
	}

	bool FSqliteStatement::Step()
	{
		const int ResultCode = sqlite3_step(Statement);
		if (ResultCode == SQLITE_ROW)
		{
			return true;
		}
		if (ResultCode == SQLITE_DONE)
		{
			return false;
		}
		throw FSqliteError(std::string("sqlite3_step failed: ") +
						   sqlite3_errstr(ResultCode));
	}

	std::int64_t FSqliteStatement::ColumnInt64(int Index) const
	{
		return sqlite3_column_int64(Statement, Index);
	}

	std::optional<std::string> FSqliteStatement::ColumnOptionalText(int Index) const
	{
		if (sqlite3_column_type(Statement, Index) == SQLITE_NULL)
		{
			return std::nullopt;
		}
		return ColumnText(Index);
	}

	std::string FSqliteStatement::ColumnText(int Index) const
	{
		const auto *Text =
			reinterpret_cast<const char *>(sqlite3_column_text(Statement, Index));
		const int Bytes = sqlite3_column_bytes(Statement, Index);
		return Text != nullptr ? std::string(Text, static_cast<std::size_t>(Bytes))
							   : std::string();
	}

	std::string FSqliteStatement::ColumnBlob(int Index) const
	{
		const auto *Blob = sqlite3_column_blob(Statement, Index);
		const int Bytes = sqlite3_column_bytes(Statement, Index);
		return Blob != nullptr
				   ? std::string(reinterpret_cast<const char *>(Blob),
								 static_cast<std::size_t>(Bytes))
				   : std::string();
	}

	FSqliteConnection::FSqliteConnection(const std::filesystem::path &DatabasePath)
	{
		const int OpenResult =
			sqlite3_open_v2(DatabasePath.string().c_str(), &Handle, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
		if (OpenResult != SQLITE_OK)
		{
			const std::string Message =
				Handle != nullptr ? sqlite3_errmsg(Handle) : sqlite3_errstr(OpenResult);
			sqlite3_close(Handle);
			Handle = nullptr;
			throw FSqliteError("sqlite3_open_v2 failed: " + Message);
		}
		sqlite3_busy_timeout(Handle, BusyTimeoutMs);
		Execute("PRAGMA journal_mode=WAL;");
		Execute("PRAGMA synchronous=NORMAL;");
		Execute("PRAGMA foreign_keys=ON;");
	}

	FSqliteConnection::~FSqliteConnection()
	{
		if (Handle != nullptr)
		{
			sqlite3_close_v2(Handle);
		}
	}

	void FSqliteConnection::Execute(std::string_view Sql)
	{
		char *ErrorMessage = nullptr;
		const int ResultCode = sqlite3_exec(
			Handle, std::string(Sql).c_str(), nullptr, nullptr, &ErrorMessage);
		if (ResultCode != SQLITE_OK)
		{
			const std::string Message =
				ErrorMessage != nullptr ? ErrorMessage : sqlite3_errstr(ResultCode);
			sqlite3_free(ErrorMessage);
			throw FSqliteError("sqlite3_exec failed: " + Message);
		}
	}

	FSqliteStatement FSqliteConnection::Prepare(std::string_view Sql)
	{
		sqlite3_stmt *Statement = nullptr;
		const int ResultCode = sqlite3_prepare_v2(
			Handle, Sql.data(), static_cast<int>(Sql.size()), &Statement, nullptr);
		ThrowIfNotOk(Handle, ResultCode, "sqlite3_prepare_v2 failed");
		return FSqliteStatement(Statement);
	}

	void FSqliteConnection::BeginImmediate()
	{
		Execute("BEGIN IMMEDIATE;");
	}

	void FSqliteConnection::Commit()
	{
		Execute("COMMIT;");
	}

	void FSqliteConnection::Rollback()
	{
		Execute("ROLLBACK;");
	}

	void FSqliteConnection::RollbackNoThrow() noexcept
	{
		sqlite3_exec(Handle, "ROLLBACK;", nullptr, nullptr, nullptr);
	}

	bool FSqliteConnection::IsInTransaction() noexcept
	{
		return sqlite3_get_autocommit(Handle) == 0;
	}

	int FSqliteConnection::ChangedRowCount() noexcept
	{
		return sqlite3_changes(Handle);
	}
} // namespace LocalData::Private
