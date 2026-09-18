#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace LocalData
{
	class FBlobCipherError final : public std::runtime_error
	{
	  public:
		explicit FBlobCipherError(const std::string &Message)
			: std::runtime_error(Message)
		{
		}
	};

	// Authenticated per-blob encryption for fitness payloads
	// (docs/architecture/03-macos-unreal-client.md, "Local persistence").
	// Implementations must use a platform-reviewed AES-256-GCM primitive with a
	// fresh random nonce per Seal; the sealed layout is opaque to LocalData.
	// AssociatedData is authenticated but not encrypted: Open must throw
	// FBlobCipherError if it differs from what Seal was given, or if the blob
	// was modified. LocalData stays free of Apple types; the CryptoKit/Keychain
	// implementation lives in a separate Apple-only adapter module.
	class IBlobCipher
	{
	  public:
		virtual ~IBlobCipher() = default;
		virtual std::string Seal(std::string_view Plaintext, std::string_view AssociatedData) = 0;
		virtual std::string Open(std::string_view Sealed, std::string_view AssociatedData) = 0;
	};
} // namespace LocalData
