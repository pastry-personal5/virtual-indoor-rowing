#include "ContentRuntimeMac/CryptoKitContentVerifier.h"

#include <cstddef>
#include <cstdint>

extern "C" bool VirContentEd25519Verify(const std::uint8_t *PublicKey,
										std::size_t PublicKeyLength,
										const std::uint8_t *Message,
										std::size_t MessageLength,
										const std::uint8_t *Signature,
										std::size_t SignatureLength);

namespace ContentRuntimeMac
{
	bool FCryptoKitContentVerifier::Verify(const ContentRuntime::FEd25519PublicKey &PublicKey,
										   std::span<const std::uint8_t> Payload,
										   std::span<const std::uint8_t> Signature) const
	{
		return VirContentEd25519Verify(PublicKey.data(), PublicKey.size(), Payload.data(), Payload.size(), Signature.data(), Signature.size());
	}
} // namespace ContentRuntimeMac
