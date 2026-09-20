#include "ContentRuntimeMac/CryptoKitContentVerifier.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
	std::vector<std::uint8_t> Hex(std::string_view Text)
	{
		auto Nibble = [](char Character) -> std::uint8_t
		{
			if (Character >= '0' && Character <= '9')
				return static_cast<std::uint8_t>(Character - '0');
			return static_cast<std::uint8_t>(Character - 'a' + 10);
		};
		assert(Text.size() % 2 == 0);
		std::vector<std::uint8_t> Bytes;
		for (std::size_t Index = 0; Index < Text.size(); Index += 2)
			Bytes.push_back(static_cast<std::uint8_t>((Nibble(Text[Index]) << 4U) | Nibble(Text[Index + 1])));
		return Bytes;
	}
} // namespace

int main()
{
	// RFC 8032, test vector 1: Ed25519 signature of the empty message.
	const auto PublicBytes = Hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
	auto Signature = Hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
						 "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
	ContentRuntime::FEd25519PublicKey PublicKey{};
	assert(PublicBytes.size() == PublicKey.size());
	std::copy(PublicBytes.begin(), PublicBytes.end(), PublicKey.begin());
	ContentRuntimeMac::FCryptoKitContentVerifier Verifier;
	assert(Verifier.Verify(PublicKey, {}, Signature));
	Signature[0] ^= 1;
	assert(!Verifier.Verify(PublicKey, {}, Signature));
	assert(!Verifier.Verify(PublicKey, {}, std::span(Signature).first(63)));
	std::cout << "CryptoKit content signature tests passed\n";
}
