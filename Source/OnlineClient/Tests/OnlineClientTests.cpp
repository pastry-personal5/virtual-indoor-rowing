#include "OnlineClient/OnlineClient.h"

#include <filesystem>
#include <iostream>

namespace
{
	int Failures = 0;
	void Expect(bool Value, const char *Expression)
	{
		if (!Value)
		{
			std::cerr << "failed: " << Expression << '\n';
			++Failures;
		}
	}
#define EXPECT_TRUE(Value) Expect((Value), #Value)

	class FCipher final : public LocalData::IBlobCipher
	{
	  public:
		std::string Seal(std::string_view Plain, std::string_view Aad) override
		{
			return std::string(Aad) + ":" + std::string(Plain);
		}
		std::string Open(std::string_view Sealed, std::string_view Aad) override
		{
			const std::string Prefix = std::string(Aad) + ":";
			if (Sealed.substr(0, Prefix.size()) != Prefix)
				throw LocalData::FBlobCipherError("bad aad");
			return std::string(Sealed.substr(Prefix.size()));
		}
	};

	class FTransport final : public OnlineClient::ITransport
	{
	  public:
		std::string Object;
		std::string Bootstrap(const std::string &, std::string &Identity, std::chrono::system_clock::time_point &Expires) override
		{
			Identity = "dev_test";
			Expires = std::chrono::system_clock::now() + std::chrono::hours(1);
			return "token";
		}
		bool CreateSession(const std::string &, const FRowingSessionId &, const std::string &, const std::string &, const std::string &) override
		{
			return true;
		}
		bool FinalizeSession(const std::string &, const FRowingSessionId &, const std::string &, const std::string &) override
		{
			return true;
		}
		OnlineClient::FUploadGrant RequestUpload(const std::string &, const FRowingSessionId &) override
		{
			return {"loopback", Digest};
		}
		bool UploadObject(const OnlineClient::FUploadGrant &, const std::string &Bytes) override
		{
			Object = Bytes;
			return true;
		}
		bool CompleteUpload(const std::string &, const FRowingSessionId &) override
		{
			return true;
		}
		std::string ProcessingStatus(const std::string &, const FRowingSessionId &) override
		{
			return "accepted_with_warnings";
		}
		std::string Digest;
	};
} // namespace

int main()
{
	const auto Path = std::filesystem::temp_directory_path() / "online_client_test.sqlite3";
	std::error_code Error;
	std::filesystem::remove(Path, Error);
	FCipher Cipher;
	const FRowingSessionId Id = FRowingSessionId::GenerateV7(1'700'000'000'000ULL, []
															 { static std::uint8_t Byte = 1; return Byte++; });
	{
		LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
		LocalData::FSessionRecord Session;
		Session.Id = Id;
		Session.UserScope = "guest";
		Session.Source = "pm5";
		Writer.CreateSession(Session);
		LocalData::FFinalizedSession Finalized;
		Finalized.TerminalEvent = {Id.ToCanonicalString(), 1, 1, LocalData::EJournalEventKind::Completed, 1, {}};
		Finalized.Summary = {Id, 1, "summary", 0};
		Writer.FinalizeSession(Finalized);
	}
	const auto Pending = LocalData::ReadPendingSyncOutbox(Path);
	FTransport Transport;
	Transport.Digest = Pending.front().ObjectDigestSha256;
	OnlineClient::FSyncConfig Config;
	Config.Enabled = true;
	Config.BaseUrl = "http://127.0.0.1";
	Config.BootstrapSecret = "secret";
	OnlineClient::FCoordinator Coordinator(Path, Cipher, Transport, Config);
	EXPECT_TRUE(Coordinator.ProcessOnce());
	EXPECT_TRUE(!Transport.Object.empty() && static_cast<unsigned char>(Transport.Object[0]) == 0x28);
	EXPECT_TRUE(LocalData::ReadPendingSyncOutbox(Path).empty());
	std::filesystem::remove(Path, Error);
	std::filesystem::remove(Path.string() + "-wal", Error);
	std::filesystem::remove(Path.string() + "-shm", Error);
	return Failures == 0 ? 0 : 1;
}
