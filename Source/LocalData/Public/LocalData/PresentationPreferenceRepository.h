#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace LocalData
{
	enum class ECameraPreferencePreset : std::uint8_t
	{
		Close,
		Medium,
		Wide
	};

	// Typed singleton preference stored in the owner-only app-state database.
	// This repository refuses the workout journal by name before opening SQLite.
	class FPresentationPreferenceRepository final
	{
	  public:
		explicit FPresentationPreferenceRepository(std::filesystem::path DatabasePath);
		~FPresentationPreferenceRepository();

		FPresentationPreferenceRepository(const FPresentationPreferenceRepository &) = delete;
		FPresentationPreferenceRepository &operator=(const FPresentationPreferenceRepository &) = delete;

		ECameraPreferencePreset LoadCameraPreset() const;
		void SaveCameraPreset(ECameraPreferencePreset Preset);

	  private:
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
	};

	// Publishes the selected value immediately and coalesces SQLite writes on a
	// dedicated worker. I/O failure is retained as a diagnostic bit only; it is
	// never allowed to affect workout state or the in-memory selection.
	class FCoalescedCameraPreference final
	{
	  public:
		explicit FCoalescedCameraPreference(std::filesystem::path DatabasePath);
		~FCoalescedCameraPreference();

		FCoalescedCameraPreference(const FCoalescedCameraPreference &) = delete;
		FCoalescedCameraPreference &operator=(const FCoalescedCameraPreference &) = delete;

		ECameraPreferencePreset Get() const noexcept;
		void Set(ECameraPreferencePreset Preset) noexcept;
		bool HadPersistenceFailure() const noexcept;
		void WaitForIdleForTesting();

	  private:
		void Run() noexcept;

		std::filesystem::path DatabasePath;
		std::atomic<ECameraPreferencePreset> Current{ECameraPreferencePreset::Medium};
		std::atomic<bool> bPersistenceFailure{false};
		std::mutex Mutex;
		std::condition_variable Wake;
		std::condition_variable Idle;
		std::optional<ECameraPreferencePreset> Pending;
		bool bWriting = false;
		bool bStop = false;
		std::thread Worker;
	};
} // namespace LocalData
