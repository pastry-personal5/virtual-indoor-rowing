#pragma once

#include "LocalData/BlobCipher.h"
#include "LocalData/LocalDataJournal.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace OnlineClient
{
	enum class ESyncStatus
	{
		Queued,
		Uploading,
		Processing,
		Accepted,
		NeedsAttention,
	};

	struct FSyncConfig
	{
		bool Enabled = false;
		std::string BaseUrl;
		std::string BootstrapSecret;
	};

	struct FUploadGrant
	{
		std::string UploadUrl;
		std::string ExpectedDigest;
	};

	class ITransport
	{
	  public:
		virtual ~ITransport() = default;
		virtual std::string Bootstrap(const std::string &BootstrapSecret, std::string &Identity, std::chrono::system_clock::time_point &ExpiresAt) = 0;
		virtual bool CreateSession(const std::string &Token, const FRowingSessionId &Id, const std::string &Disposition, const std::string &Digest, const std::string &IdempotencyKey) = 0;
		virtual bool FinalizeSession(const std::string &Token, const FRowingSessionId &Id, const std::string &Digest, const std::string &IdempotencyKey) = 0;
		virtual FUploadGrant RequestUpload(const std::string &Token, const FRowingSessionId &Id) = 0;
		virtual bool UploadObject(const FUploadGrant &Grant, const std::string &ObjectBytes) = 0;
		virtual bool CompleteUpload(const std::string &Token, const FRowingSessionId &Id) = 0;
		virtual std::string ProcessingStatus(const std::string &Token, const FRowingSessionId &Id) = 0;
	};

	// macOS NSURLSession adapter. The coordinator remains engine-independent;
	// this adapter is the only implementation that touches Apple networking.
	class FMacHttpTransport final : public ITransport
	{
	  public:
		explicit FMacHttpTransport(std::string InBaseUrl);
		~FMacHttpTransport() override;

		std::string Bootstrap(const std::string &BootstrapSecret, std::string &Identity, std::chrono::system_clock::time_point &ExpiresAt) override;
		bool CreateSession(const std::string &Token, const FRowingSessionId &Id, const std::string &Disposition, const std::string &Digest, const std::string &IdempotencyKey) override;
		bool FinalizeSession(const std::string &Token, const FRowingSessionId &Id, const std::string &Digest, const std::string &IdempotencyKey) override;
		FUploadGrant RequestUpload(const std::string &Token, const FRowingSessionId &Id) override;
		bool UploadObject(const FUploadGrant &Grant, const std::string &ObjectBytes) override;
		bool CompleteUpload(const std::string &Token, const FRowingSessionId &Id) override;
		std::string ProcessingStatus(const std::string &Token, const FRowingSessionId &Id) override;

	  private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	class FCoordinator final
	{
	  public:
		FCoordinator(std::filesystem::path DatabasePath,
					 LocalData::IBlobCipher &Cipher,
					 ITransport &Transport,
					 FSyncConfig Config);

		// Processes one durable outbox item. It never throws transport failures
		// into the workout path; state is retained for a later retry.
		bool ProcessOnce();

	  private:
		std::filesystem::path DatabasePath;
		LocalData::IBlobCipher &Cipher;
		ITransport &Transport;
		FSyncConfig Config;
		std::string Token;
		std::string Identity;
		std::chrono::system_clock::time_point TokenExpiresAt{};
	};
} // namespace OnlineClient
