// memset_s (C11 Annex K) is declared by <string.h> only when this is defined;
// unlike memset it cannot be optimized away when wiping key material.
#define __STDC_WANT_LIB_EXT1__ 1

#include "LocalDataMac/CryptoKitBlobCipher.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <optional>
#include <string.h>
#include <vector>

extern "C"
{
	std::int64_t LocalDataAesGcmSeal(const std::uint8_t *Key, const std::uint8_t *Plaintext, std::int64_t PlaintextLength, const std::uint8_t *Aad, std::int64_t AadLength, std::uint8_t *Output, std::int64_t OutputCapacity);
	std::int64_t LocalDataAesGcmOpen(const std::uint8_t *Key, const std::uint8_t *Sealed, std::int64_t SealedLength, const std::uint8_t *Aad, std::int64_t AadLength, std::uint8_t *Output, std::int64_t OutputCapacity);
}

namespace LocalDataMac
{
	namespace
	{
		// nonce (12) + tag (16)
		constexpr std::size_t SealOverhead = 28;

		const std::uint8_t *Bytes(std::string_view View)
		{
			return reinterpret_cast<const std::uint8_t *>(View.data());
		}

		struct FCfString
		{
			CFStringRef Ref;
			explicit FCfString(const std::string &Value)
				: Ref(CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(Value.data()), static_cast<CFIndex>(Value.size()), kCFStringEncodingUTF8, false))
			{
				if (Ref == nullptr)
					throw LocalData::FBlobCipherError("keychain identifier is not valid UTF-8");
			}
			~FCfString()
			{
				CFRelease(Ref);
			}
			FCfString(const FCfString &) = delete;
			FCfString &operator=(const FCfString &) = delete;
		};

		CFMutableDictionaryRef MakeQuery(const FCfString &Service, const FCfString &Account)
		{
			CFMutableDictionaryRef Query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
			CFDictionarySetValue(Query, kSecClass, kSecClassGenericPassword);
			CFDictionarySetValue(Query, kSecAttrService, Service.Ref);
			CFDictionarySetValue(Query, kSecAttrAccount, Account.Ref);
			return Query;
		}
	} // namespace

	FCryptoKitBlobCipher::FCryptoKitBlobCipher(const FDataKey &InKey)
		: Key(InKey)
	{
	}

	FCryptoKitBlobCipher::~FCryptoKitBlobCipher()
	{
		memset_s(Key.data(), Key.size(), 0, Key.size());
	}

	std::string FCryptoKitBlobCipher::Seal(std::string_view Plaintext, std::string_view AssociatedData)
	{
		std::string Out(Plaintext.size() + SealOverhead, '\0');
		const std::int64_t Written = LocalDataAesGcmSeal(Key.data(), Bytes(Plaintext), static_cast<std::int64_t>(Plaintext.size()), Bytes(AssociatedData), static_cast<std::int64_t>(AssociatedData.size()), reinterpret_cast<std::uint8_t *>(Out.data()), static_cast<std::int64_t>(Out.size()));
		if (Written < 0)
			throw LocalData::FBlobCipherError("AES-GCM seal failed");
		Out.resize(static_cast<std::size_t>(Written));
		return Out;
	}

	std::string FCryptoKitBlobCipher::Open(std::string_view Sealed, std::string_view AssociatedData)
	{
		if (Sealed.size() < SealOverhead)
			throw LocalData::FBlobCipherError("sealed blob is too short");
		std::string Out(Sealed.size() - SealOverhead, '\0');
		// Non-null output even when the plaintext is empty.
		std::uint8_t Scratch = 0;
		std::uint8_t *Destination = Out.empty() ? &Scratch : reinterpret_cast<std::uint8_t *>(Out.data());
		const std::int64_t Written = LocalDataAesGcmOpen(Key.data(), Bytes(Sealed), static_cast<std::int64_t>(Sealed.size()), Bytes(AssociatedData), static_cast<std::int64_t>(AssociatedData.size()), Destination, static_cast<std::int64_t>(Out.size()));
		if (Written < 0)
			throw LocalData::FBlobCipherError("AES-GCM authentication failed");
		Out.resize(static_cast<std::size_t>(Written));
		return Out;
	}

	namespace
	{
		// Returns nullopt only when SecItemAdd reports the item already exists.
		std::optional<FDataKey> TryLoadOrCreate(const std::string &Service, const std::string &Account)
		{
			const FCfString ServiceString(Service);
			const FCfString AccountString(Account);

			CFMutableDictionaryRef Query = MakeQuery(ServiceString, AccountString);
			CFDictionarySetValue(Query, kSecReturnData, kCFBooleanTrue);
			CFDictionarySetValue(Query, kSecMatchLimit, kSecMatchLimitOne);
			CFTypeRef Found = nullptr;
			OSStatus Status = SecItemCopyMatching(Query, &Found);
			CFRelease(Query);

			FDataKey Key{};
			if (Status == errSecSuccess)
			{
				const auto Data = static_cast<CFDataRef>(Found);
				const bool Valid = CFGetTypeID(Found) == CFDataGetTypeID() && CFDataGetLength(Data) == static_cast<CFIndex>(Key.size());
				if (Valid)
					CFDataGetBytes(Data, CFRangeMake(0, static_cast<CFIndex>(Key.size())), Key.data());
				CFRelease(Found);
				if (!Valid)
					throw LocalData::FBlobCipherError("stored data key has an unexpected size");
				return Key;
			}
			if (Status != errSecItemNotFound)
				throw LocalData::FBlobCipherError("keychain lookup failed, OSStatus " + std::to_string(Status));

			if (SecRandomCopyBytes(kSecRandomDefault, Key.size(), Key.data()) != errSecSuccess)
				throw LocalData::FBlobCipherError("could not generate a data key");

			CFDataRef KeyData = CFDataCreate(kCFAllocatorDefault, Key.data(), static_cast<CFIndex>(Key.size()));
			CFMutableDictionaryRef Add = MakeQuery(ServiceString, AccountString);
			CFDictionarySetValue(Add, kSecValueData, KeyData);
			CFDictionarySetValue(Add, kSecAttrAccessible, kSecAttrAccessibleWhenUnlockedThisDeviceOnly);
			Status = SecItemAdd(Add, nullptr);
			CFRelease(Add);
			CFRelease(KeyData);
			if (Status == errSecDuplicateItem)
				return std::nullopt;
			if (Status != errSecSuccess)
				throw LocalData::FBlobCipherError("keychain add failed, OSStatus " + std::to_string(Status));
			return Key;
		}
	} // namespace

	FDataKey LoadOrCreateKeychainDataKey(const std::string &Service, const std::string &Account)
	{
		if (auto Key = TryLoadOrCreate(Service, Account))
		{
			const FDataKey Result = *Key;
			memset_s(Key->data(), Key->size(), 0, Key->size());
			return Result;
		}
		// A concurrent creator won the race: its item is visible now. If the add
		// still collides, the lookup cannot see the existing item (ACL or keychain
		// mismatch) and retrying forever would only overflow the stack.
		if (auto Key = TryLoadOrCreate(Service, Account))
		{
			const FDataKey Result = *Key;
			memset_s(Key->data(), Key->size(), 0, Key->size());
			return Result;
		}
		throw LocalData::FBlobCipherError("keychain item exists but cannot be read");
	}

	bool DeleteKeychainDataKey(const std::string &Service, const std::string &Account)
	{
		const FCfString ServiceString(Service);
		const FCfString AccountString(Account);
		CFMutableDictionaryRef Query = MakeQuery(ServiceString, AccountString);
		const OSStatus Status = SecItemDelete(Query);
		CFRelease(Query);
		if (Status == errSecSuccess)
			return true;
		if (Status == errSecItemNotFound)
			return false;
		throw LocalData::FBlobCipherError("keychain delete failed, OSStatus " + std::to_string(Status));
	}
} // namespace LocalDataMac
