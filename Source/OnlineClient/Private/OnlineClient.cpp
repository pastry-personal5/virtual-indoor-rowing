#include "OnlineClient/OnlineClient.h"

#include <ctime>

namespace OnlineClient
{
	namespace
	{
		std::string Disposition(const std::filesystem::path &DatabasePath, const FRowingSessionId &Id, LocalData::IBlobCipher &Cipher)
		{
			const auto Events = LocalData::ReadJournalEvents(DatabasePath, Id.ToCanonicalString(), &Cipher);
			for (auto It = Events.rbegin(); It != Events.rend(); ++It)
			{
				if (It->Kind == LocalData::EJournalEventKind::Completed)
					return "completed";
				if (It->Kind == LocalData::EJournalEventKind::Interrupted)
					return "interrupted";
				if (It->Kind == LocalData::EJournalEventKind::Aborted)
					return "aborted";
			}
			return {};
		}

		std::string Digest(const LocalData::FSyncOutboxItem &Item)
		{
			return Item.ObjectDigestSha256;
		}
	} // namespace

	FCoordinator::FCoordinator(std::filesystem::path InDatabasePath,
							   LocalData::IBlobCipher &InCipher,
							   ITransport &InTransport,
							   FSyncConfig InConfig)
		: DatabasePath(std::move(InDatabasePath)), Cipher(InCipher), Transport(InTransport), Config(std::move(InConfig))
	{
	}

	bool FCoordinator::ProcessOnce()
	{
		if (!Config.Enabled || Config.BaseUrl.empty() || Config.BootstrapSecret.empty())
			return false;
		const auto Items = LocalData::ReadPendingSyncOutbox(DatabasePath);
		if (Items.empty())
			return false;
		if (Token.empty() || std::chrono::system_clock::now() + std::chrono::minutes(1) >= TokenExpiresAt)
		{
			Token = Transport.Bootstrap(Config.BootstrapSecret, Identity, TokenExpiresAt);
			if (Token.empty())
				return false;
		}
		const auto &Item = Items.front();
		const std::string SessionDisposition = Disposition(DatabasePath, Item.SessionId, Cipher);
		if (SessionDisposition.empty())
		{
			LocalData::UpdateSyncOutbox(DatabasePath, Item.OperationId, "needs_attention", Item.Attempt + 1, "missing terminal disposition");
			return false;
		}
		const std::string Object = LocalData::ReadSessionObject(DatabasePath, Item.SessionId, Cipher);
		if (!Transport.CreateSession(Token, Item.SessionId, SessionDisposition, Digest(Item), Item.OperationId + ":create") ||
			!Transport.FinalizeSession(Token, Item.SessionId, Digest(Item), Item.OperationId + ":finalize"))
		{
			LocalData::UpdateSyncOutbox(DatabasePath, Item.OperationId, "queued", Item.Attempt + 1, "control request failed");
			return false;
		}
		LocalData::UpdateSyncOutbox(DatabasePath, Item.OperationId, "uploading", Item.Attempt, {});
		const FUploadGrant Grant = Transport.RequestUpload(Token, Item.SessionId);
		if (Grant.ExpectedDigest != Digest(Item) || Grant.UploadUrl.empty() || !Transport.UploadObject(Grant, Object) || !Transport.CompleteUpload(Token, Item.SessionId))
		{
			LocalData::UpdateSyncOutbox(DatabasePath, Item.OperationId, "needs_attention", Item.Attempt + 1, "object transfer failed or digest mismatch");
			return false;
		}
		const std::string ProcessingStatus = Transport.ProcessingStatus(Token, Item.SessionId);
		if (ProcessingStatus == "accepted" || ProcessingStatus == "accepted_with_warnings")
		{
			LocalData::UpdateSyncOutbox(DatabasePath, Item.OperationId, "accepted", Item.Attempt, {});
			return true;
		}
		if (ProcessingStatus == "rejected")
			LocalData::UpdateSyncOutbox(DatabasePath, Item.OperationId, "needs_attention", Item.Attempt, "server rejected SessionObject");
		else
			LocalData::UpdateSyncOutbox(DatabasePath, Item.OperationId, "processing", Item.Attempt, {});
		return false;
	}
} // namespace OnlineClient
