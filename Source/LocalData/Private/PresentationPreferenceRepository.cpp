#include "LocalData/PresentationPreferenceRepository.h"

#include "LocalData/Sqlite.h"

#include <stdexcept>
#include <string_view>

namespace LocalData
{
	namespace
	{
		constexpr std::string_view WorkoutJournalFilename = "workout-journal.sqlite3";

		void RequireAppStatePath(const std::filesystem::path &Path)
		{
			if (Path.filename() == WorkoutJournalFilename)
				throw std::invalid_argument("presentation preferences must never open workout-journal.sqlite3");
		}

		const char *StoredPresetName(ECameraPreferencePreset Preset)
		{
			switch (Preset)
			{
			case ECameraPreferencePreset::Close:
				return "Close";
			case ECameraPreferencePreset::Wide:
				return "Wide";
			case ECameraPreferencePreset::Medium:
			default:
				return "Medium";
			}
		}

		ECameraPreferencePreset ParseStoredPreset(std::string_view Value)
		{
			if (Value == "Close")
				return ECameraPreferencePreset::Close;
			if (Value == "Wide")
				return ECameraPreferencePreset::Wide;
			return ECameraPreferencePreset::Medium;
		}
	} // namespace

	struct FPresentationPreferenceRepository::FImpl
	{
		explicit FImpl(const std::filesystem::path &Path)
			: Connection(Path)
		{
			Connection.Execute("CREATE TABLE IF NOT EXISTS presentation_preferences ("
							   "singleton INTEGER PRIMARY KEY CHECK (singleton = 1),"
							   "camera_preset TEXT NOT NULL CHECK (camera_preset IN ('Close','Medium','Wide')),"
							   "updated_at TEXT NOT NULL"
							   ");");
		}

		Private::FSqliteConnection Connection;
	};

	FPresentationPreferenceRepository::FPresentationPreferenceRepository(std::filesystem::path DatabasePath)
	{
		RequireAppStatePath(DatabasePath);
		Impl = std::make_unique<FImpl>(DatabasePath);
	}

	FPresentationPreferenceRepository::~FPresentationPreferenceRepository() = default;

	ECameraPreferencePreset FPresentationPreferenceRepository::LoadCameraPreset() const
	{
		auto Query = Impl->Connection.Prepare("SELECT camera_preset FROM presentation_preferences WHERE singleton=1;");
		return Query.Step() ? ParseStoredPreset(Query.ColumnText(0)) : ECameraPreferencePreset::Medium;
	}

	void FPresentationPreferenceRepository::SaveCameraPreset(ECameraPreferencePreset Preset)
	{
		if (Preset != ECameraPreferencePreset::Close && Preset != ECameraPreferencePreset::Medium && Preset != ECameraPreferencePreset::Wide)
			Preset = ECameraPreferencePreset::Medium;
		auto Upsert = Impl->Connection.Prepare(
			"INSERT INTO presentation_preferences(singleton,camera_preset,updated_at) VALUES(1,?,datetime('now')) "
			"ON CONFLICT(singleton) DO UPDATE SET camera_preset=excluded.camera_preset,updated_at=excluded.updated_at;");
		Upsert.BindText(1, StoredPresetName(Preset));
		Upsert.Step();
	}

	FCoalescedCameraPreference::FCoalescedCameraPreference(std::filesystem::path InDatabasePath)
		: DatabasePath(std::move(InDatabasePath))
	{
		RequireAppStatePath(DatabasePath);
		try
		{
			FPresentationPreferenceRepository Repository(DatabasePath);
			Current.store(Repository.LoadCameraPreset(), std::memory_order_relaxed);
		}
		catch (...)
		{
			bPersistenceFailure.store(true, std::memory_order_relaxed);
		}
		Worker = std::thread([this]
							 { Run(); });
	}

	FCoalescedCameraPreference::~FCoalescedCameraPreference()
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			bStop = true;
		}
		Wake.notify_one();
		if (Worker.joinable())
			Worker.join();
	}

	ECameraPreferencePreset FCoalescedCameraPreference::Get() const noexcept
	{
		return Current.load(std::memory_order_relaxed);
	}

	void FCoalescedCameraPreference::Set(ECameraPreferencePreset Preset) noexcept
	{
		if (Preset != ECameraPreferencePreset::Close && Preset != ECameraPreferencePreset::Medium && Preset != ECameraPreferencePreset::Wide)
			Preset = ECameraPreferencePreset::Medium;
		Current.store(Preset, std::memory_order_relaxed);
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			Pending = Preset;
		}
		Wake.notify_one();
	}

	bool FCoalescedCameraPreference::HadPersistenceFailure() const noexcept
	{
		return bPersistenceFailure.load(std::memory_order_relaxed);
	}

	void FCoalescedCameraPreference::WaitForIdleForTesting()
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		Idle.wait(Lock, [this]
				  { return !Pending && !bWriting; });
	}

	void FCoalescedCameraPreference::Run() noexcept
	{
		for (;;)
		{
			ECameraPreferencePreset Value = ECameraPreferencePreset::Medium;
			{
				std::unique_lock<std::mutex> Lock(Mutex);
				Wake.wait(Lock, [this]
						  { return bStop || Pending.has_value(); });
				if (bStop && !Pending)
					return;
				Value = *Pending;
				Pending.reset();
				bWriting = true;
			}
			try
			{
				FPresentationPreferenceRepository Repository(DatabasePath);
				Repository.SaveCameraPreset(Value);
			}
			catch (...)
			{
				bPersistenceFailure.store(true, std::memory_order_relaxed);
			}
			{
				std::lock_guard<std::mutex> Lock(Mutex);
				bWriting = false;
				if (!Pending)
					Idle.notify_all();
			}
		}
	}
} // namespace LocalData
