#include "LocalData/Crc32c.h"

#include <array>

namespace LocalData::Private
{
	namespace
	{
		constexpr std::uint32_t Polynomial = 0x82F63B78U;

		constexpr std::array<std::uint32_t, 256> BuildTable() noexcept
		{
			std::array<std::uint32_t, 256> Table{};
			for (std::uint32_t Index = 0; Index < 256; ++Index)
			{
				std::uint32_t Value = Index;
				for (int Bit = 0; Bit < 8; ++Bit)
				{
					Value = (Value & 1U) != 0U
								? (Value >> 1) ^ Polynomial
								: (Value >> 1);
				}
				Table[Index] = Value;
			}
			return Table;
		}

		constexpr std::array<std::uint32_t, 256> Table = BuildTable();
	} // namespace

	std::uint32_t ComputeCrc32c(std::string_view Data) noexcept
	{
		std::uint32_t Crc = 0xFFFFFFFFU;
		for (const char Byte : Data)
		{
			const std::uint8_t Index =
				static_cast<std::uint8_t>(Crc ^ static_cast<std::uint8_t>(Byte));
			Crc = Table[Index] ^ (Crc >> 8);
		}
		return Crc ^ 0xFFFFFFFFU;
	}
} // namespace LocalData::Private
