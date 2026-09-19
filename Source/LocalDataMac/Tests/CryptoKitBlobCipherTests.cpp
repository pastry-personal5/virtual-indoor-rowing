#include "LocalDataMac/CryptoKitBlobCipher.h"

#include "LocalData/LocalDataJournal.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

namespace
{
	int Failures = 0;

	void Expect(const bool Condition, const char *const Expression, const char *const TestName)
	{
		if (!Condition)
		{
			std::cerr << TestName << ": expectation failed: " << Expression << '\n';
			++Failures;
		}
	}

#define EXPECT_TRUE(Expression) Expect((Expression), #Expression, __func__)

	template <typename FCallable>
	bool ThrowsCipherError(FCallable &&Callable)
	{
		try
		{
			Callable();
		}
		catch (const LocalData::FBlobCipherError &)
		{
			return true;
		}
		return false;
	}

	LocalDataMac::FDataKey MakeKey(std::uint8_t Seed)
	{
		LocalDataMac::FDataKey Key{};
		for (std::size_t Index = 0; Index < Key.size(); ++Index)
			Key[Index] = static_cast<std::uint8_t>(Seed + Index);
		return Key;
	}

	void cryptokit_cipher_round_trips_and_hides_plaintext()
	{
		LocalDataMac::FCryptoKitBlobCipher Cipher(MakeKey(1));
		const std::string Plain = "distance=5000123;avg_hr=150";
		const std::string Sealed = Cipher.Seal(Plain, "ctx");
		EXPECT_TRUE(Sealed.size() == Plain.size() + 28);
		EXPECT_TRUE(Sealed.find("distance=") == std::string::npos);
		EXPECT_TRUE(Cipher.Open(Sealed, "ctx") == Plain);
	}

	void cryptokit_cipher_round_trips_empty_plaintext_and_binary_bytes()
	{
		LocalDataMac::FCryptoKitBlobCipher Cipher(MakeKey(2));
		EXPECT_TRUE(Cipher.Open(Cipher.Seal("", ""), "").empty());
		const std::string Binary("\0\1\2\xff\0", 5);
		EXPECT_TRUE(Cipher.Open(Cipher.Seal(Binary, std::string("\0a", 2)), std::string("\0a", 2)) == Binary);
	}

	void cryptokit_cipher_uses_a_fresh_nonce_per_seal()
	{
		LocalDataMac::FCryptoKitBlobCipher Cipher(MakeKey(3));
		const std::string First = Cipher.Seal("same", "ctx");
		const std::string Second = Cipher.Seal("same", "ctx");
		EXPECT_TRUE(First != Second);
		EXPECT_TRUE(First.substr(0, 12) != Second.substr(0, 12));
	}

	void cryptokit_cipher_rejects_tampering_wrong_context_and_wrong_key()
	{
		LocalDataMac::FCryptoKitBlobCipher Cipher(MakeKey(4));
		const std::string Sealed = Cipher.Seal("payload", "ctx");

		EXPECT_TRUE(ThrowsCipherError([&]
									  { Cipher.Open(Sealed, "other-ctx"); }));
		for (const std::size_t Position : {std::size_t{0}, std::size_t{13}, Sealed.size() - 1})
		{
			std::string Tampered = Sealed;
			Tampered[Position] = static_cast<char>(Tampered[Position] ^ 0x01);
			EXPECT_TRUE(ThrowsCipherError([&]
										  { Cipher.Open(Tampered, "ctx"); }));
		}
		EXPECT_TRUE(ThrowsCipherError([&]
									  { Cipher.Open(Sealed.substr(0, 20), "ctx"); }));

		LocalDataMac::FCryptoKitBlobCipher OtherKey(MakeKey(5));
		EXPECT_TRUE(ThrowsCipherError([&]
									  { OtherKey.Open(Sealed, "ctx"); }));
	}

	void cryptokit_cipher_seals_journal_chunks_and_summaries_end_to_end()
	{
		std::random_device Random;
		const auto Path = std::filesystem::temp_directory_path() / ("local_data_mac_" + std::to_string(Random()) + ".sqlite3");
		LocalDataMac::FCryptoKitBlobCipher Cipher(MakeKey(6));

		LocalData::FSampleChunk Chunk;
		Chunk.SessionId = "session-mac";
		Chunk.FirstSequence = 0;
		Chunk.LastSequence = 0;
		FRowingMetricSample Sample;
		Sample.DistanceMm = 1'234'567;
		Chunk.Samples.push_back(Sample);
		std::uint8_t NextByte = 0;
		const FRowingSessionId Id = FRowingSessionId::GenerateV7(1'700'000'000'000ULL, [&NextByte]
																 { return NextByte++; });
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			Writer.AppendChunk(Chunk);
			LocalData::FSessionRecord Record;
			Record.Id = Id;
			Record.UserScope = "guest";
			Record.Source = "pm5";
			Writer.CreateSession(Record);
			LocalData::FSessionSummary Summary;
			Summary.Id = Id;
			Summary.MetricsPayload = "distance=5000123";
			Summary.QualityFlags = 2;
			Writer.StageSessionSummary(Summary);
			Writer.CommitStagedSessionSummary();
		}
		const auto Chunks = LocalData::ReadSampleChunks(Path, "session-mac", &Cipher);
		EXPECT_TRUE(Chunks.size() == 1 && Chunks[0].Samples.size() == 1 && Chunks[0].Samples[0].DistanceMm == 1'234'567);

		const auto Summary = LocalData::ReadLatestSessionSummary(Path, Id, Cipher);
		EXPECT_TRUE(Summary.has_value() && Summary->MetricsPayload == "distance=5000123" && Summary->QualityFlags == 2);

		LocalDataMac::FCryptoKitBlobCipher WrongKey(MakeKey(7));
		EXPECT_TRUE(ThrowsCipherError([&]
									  { LocalData::ReadSampleChunks(Path, "session-mac", &WrongKey); }));
		EXPECT_TRUE(ThrowsCipherError([&]
									  { LocalData::ReadLatestSessionSummary(Path, Id, WrongKey); }));
		std::filesystem::remove(Path);
		std::filesystem::remove(Path.string() + "-wal");
		std::filesystem::remove(Path.string() + "-shm");
	}

	void keychain_data_key_is_created_once_reused_and_deletable()
	{
		std::random_device Random;
		const std::string Service = "dev.virtualrowing.tests.local-data." + std::to_string(Random());
		const std::string Account = "profile-test";

		const auto First = LocalDataMac::LoadOrCreateKeychainDataKey(Service, Account);
		const auto Second = LocalDataMac::LoadOrCreateKeychainDataKey(Service, Account);
		EXPECT_TRUE(First == Second);
		EXPECT_TRUE(First != LocalDataMac::FDataKey{});

		EXPECT_TRUE(LocalDataMac::DeleteKeychainDataKey(Service, Account));
		EXPECT_TRUE(!LocalDataMac::DeleteKeychainDataKey(Service, Account));
		EXPECT_TRUE(LocalDataMac::LoadOrCreateKeychainDataKey(Service, Account) != First);
		LocalDataMac::DeleteKeychainDataKey(Service, Account);
	}
} // namespace

int main()
{
	cryptokit_cipher_round_trips_and_hides_plaintext();
	cryptokit_cipher_round_trips_empty_plaintext_and_binary_bytes();
	cryptokit_cipher_uses_a_fresh_nonce_per_seal();
	cryptokit_cipher_rejects_tampering_wrong_context_and_wrong_key();
	cryptokit_cipher_seals_journal_chunks_and_summaries_end_to_end();
	try
	{
		keychain_data_key_is_created_once_reused_and_deletable();
	}
	catch (const LocalData::FBlobCipherError &Error)
	{
		// Some managed macOS test hosts deny ad-hoc Keychain writes with this
		// status. Keep production Keychain behavior strict, but classify the
		// host limitation as a CTest skip instead of a false product failure.
		const std::string Message = Error.what();
		if (Message.find("OSStatus 100001") != std::string::npos)
		{
			std::cerr << "SKIP: Keychain unavailable on this host (" << Message << ")\n";
			return 77;
		}
		std::cerr << "Keychain test failed: " << Message << '\n';
		return EXIT_FAILURE;
	}

	if (Failures != 0)
	{
		std::cerr << Failures << " local data mac assertion(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "local data mac tests passed\n";
	return EXIT_SUCCESS;
}
