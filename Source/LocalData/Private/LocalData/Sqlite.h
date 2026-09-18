#pragma once

// The only file in this module that includes sqlite3.h. Public headers
// never see sqlite3 types, per Scripts/check_public_header_dependencies.py
// and CLAUDE.md's rule that RowingCore-adjacent contracts stay
// database-runtime-independent above the adapter seam.
#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace LocalData::Private
{
	class FSqliteError final : public std::runtime_error
	{
	  public:
		explicit FSqliteError(std::string Message)
			: std::runtime_error(std::move(Message))
		{
		}
	};

	class FSqliteStatement final
	{
	  public:
		FSqliteStatement() = default;
		explicit FSqliteStatement(sqlite3_stmt *InStatement) noexcept
			: Statement(InStatement)
		{
		}
		~FSqliteStatement()
		{
			sqlite3_finalize(Statement);
		}

		FSqliteStatement(const FSqliteStatement &) = delete;
		FSqliteStatement &operator=(const FSqliteStatement &) = delete;

		FSqliteStatement(FSqliteStatement &&Other) noexcept
			: Statement(Other.Statement)
		{
			Other.Statement = nullptr;
		}
		FSqliteStatement &operator=(FSqliteStatement &&Other) noexcept
		{
			if (this != &Other)
			{
				sqlite3_finalize(Statement);
				Statement = Other.Statement;
				Other.Statement = nullptr;
			}
			return *this;
		}

		void BindInt64(int Index, std::int64_t Value);
		void BindText(int Index, std::string_view Value);
		void BindBlob(int Index, std::string_view Value);
		void BindNull(int Index);
		void BindOptionalText(int Index, const std::optional<std::string> &Value);

		// Steps once; returns true while a row is available (SQLITE_ROW),
		// false once the statement is done (SQLITE_DONE). Throws on error.
		bool Step();

		std::int64_t ColumnInt64(int Index) const;
		std::string ColumnText(int Index) const;
		std::string ColumnBlob(int Index) const;
		std::optional<std::string> ColumnOptionalText(int Index) const;

	  private:
		sqlite3_stmt *Statement = nullptr;
	};

	class FSqliteConnection final
	{
	  public:
		explicit FSqliteConnection(const std::filesystem::path &DatabasePath);
		~FSqliteConnection();

		FSqliteConnection(const FSqliteConnection &) = delete;
		FSqliteConnection &operator=(const FSqliteConnection &) = delete;

		void Execute(std::string_view Sql);
		FSqliteStatement Prepare(std::string_view Sql);

		void BeginImmediate();
		void Commit();
		void Rollback();
		// Best-effort rollback for error paths: SQLite may already have rolled
		// the transaction back itself, and that must not mask the original error.
		void RollbackNoThrow() noexcept;

		// True while a transaction is open. SQLite closes one itself on some
		// errors, so this is the authority on whether a rollback is still owed.
		bool IsInTransaction() noexcept;

		// Runs Body in BEGIN IMMEDIATE ... COMMIT, rolling back and rethrowing on
		// any failure. BEGIN sits outside the try: if it fails because another
		// transaction is already open, that transaction is not ours to roll back.
		template <typename FBody>
		void InTransaction(FBody &&Body)
		{
			BeginImmediate();
			try
			{
				Body();
				Commit();
			}
			catch (...)
			{
				RollbackNoThrow();
				throw;
			}
		}

		// Rows changed by the most recently completed INSERT/UPDATE/DELETE.
		int ChangedRowCount() noexcept;

	  private:
		sqlite3 *Handle = nullptr;
	};
} // namespace LocalData::Private
