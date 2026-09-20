#pragma once

#include "ContentRuntime/ContentManifest.h"

namespace ContentRuntimeMac
{
	class FCryptoKitContentVerifier final : public ContentRuntime::IContentSignatureVerifier
	{
	  public:
		bool Verify(const ContentRuntime::FEd25519PublicKey &PublicKey,
					std::span<const std::uint8_t> Payload,
					std::span<const std::uint8_t> Signature) const override;
	};
} // namespace ContentRuntimeMac
