#pragma once

#include "LocalData/BlobCipher.h"

#include <array>
#include <cstdint>
#include <string>

namespace LocalDataMac
{
	using FDataKey = std::array<std::uint8_t, 32>;

	// AES-256-GCM via Apple CryptoKit, with a fresh random 96-bit nonce per Seal.
	// Sealed layout: nonce || ciphertext || tag.
	class FCryptoKitBlobCipher final : public LocalData::IBlobCipher
	{
	  public:
		explicit FCryptoKitBlobCipher(const FDataKey &Key);
		// Zeroes the held key. The caller's own FDataKey copy is its to wipe.
		~FCryptoKitBlobCipher() override;
		FCryptoKitBlobCipher(const FCryptoKitBlobCipher &) = delete;
		FCryptoKitBlobCipher &operator=(const FCryptoKitBlobCipher &) = delete;

		std::string Seal(std::string_view Plaintext, std::string_view AssociatedData) override;
		std::string Open(std::string_view Sealed, std::string_view AssociatedData) override;

	  private:
		FDataKey Key;
	};

	// The per-local-profile data key lives in the macOS Keychain as a generic
	// password. LoadOrCreate generates a random 256-bit key on first use.
	// Throws LocalData::FBlobCipherError on any Keychain failure.
	FDataKey LoadOrCreateKeychainDataKey(const std::string &Service, const std::string &Account);

	// Explicit local data deletion: removes the key, after which sealed data is
	// unrecoverable. Returns false if no key existed.
	bool DeleteKeychainDataKey(const std::string &Service, const std::string &Account);
} // namespace LocalDataMac
