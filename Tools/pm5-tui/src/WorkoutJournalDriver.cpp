#include "WorkoutJournalDriver.h"

#include "LocalData/LocalDataJournal.h"
#include "LocalDataMac/CryptoKitBlobCipher.h"
#include "WorkoutRuntime/LocalDataJournalSink.h"

#include <chrono>
#include <exception>
#include <random>

namespace PM5Tui
{
	namespace
	{
		constexpr const char *KeychainService = "dev.virtualrowing.pm5-diagnostic";
		constexpr const char *KeychainAccount = "workout-journal-data-key";
		constexpr const char *DatabaseName = "pm5-tui-journal.sqlite3";

		const char *ToString(ERowingSessionState State)
		{
			switch (State)
			{
			case ERowingSessionState::Created:
				return "Created";
			case ERowingSessionState::Active:
				return "Active";
			case ERowingSessionState::ConnectionLost:
				return "ConnectionLost";
			case ERowingSessionState::Ended:
				return "Ended";
			}
			return "Unknown";
		}

		const char *ToString(ERowingSessionDisposition Disposition)
		{
			switch (Disposition)
			{
			case ERowingSessionDisposition::Completed:
				return "Completed";
			case ERowingSessionDisposition::Interrupted:
				return "Interrupted";
			case ERowingSessionDisposition::Aborted:
				return "Aborted";
			}
			return "Unknown";
		}

		std::string Counts(const FWorkoutSnapshot &Snapshot)
		{
			return " accepted=" + std::to_string(Snapshot.AcceptedSampleCount) +
				   " rejected=" + std::to_string(Snapshot.RejectedSampleCount) +
				   " gaps=" + std::to_string(Snapshot.GapCount) +
				   " dropped=" + std::to_string(Snapshot.DroppedSampleCount) +
				   " journal_errors=" + std::to_string(Snapshot.JournalErrorCount);
		}
	} // namespace

	FWorkoutJournalDriver::FWorkoutJournalDriver(const std::filesystem::path &Directory)
	{
		try
		{
			std::filesystem::create_directories(Directory);
			std::filesystem::permissions(Directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
			const std::filesystem::path Database = Directory / DatabaseName;

			// A previous run that was killed mid-row left an open session behind.
			if (std::filesystem::exists(Database))
			{
				const LocalData::FLocalDataRecoveryReport Report = LocalData::ScanAndRecover(Database);
				if (Report.RecoveredAfterUncleanExit)
					RecoveryNote = "Recovered an unfinished session from the previous run";
			}

			LocalDataMac::FDataKey Key = LocalDataMac::LoadOrCreateKeychainDataKey(KeychainService, KeychainAccount);
			Cipher = std::make_unique<LocalDataMac::FCryptoKitBlobCipher>(Key);
			Key.fill(0);
			Writer = std::make_unique<LocalData::FLocalDataJournalWriter>(Database, Cipher.get());
			Sink = std::make_unique<FLocalDataJournalSink>(*Writer);
		}
		catch (const std::exception &Failure)
		{
			Error = Failure.what();
			Sink.reset();
			Writer.reset();
			Cipher.reset();
		}
	}

	FWorkoutJournalDriver::~FWorkoutJournalDriver()
	{
		// Session first: it holds pointers into the sink and machine.
		Session.reset();
	}

	void FWorkoutJournalDriver::StartSession(std::uint64_t NowNs)
	{
		if (!IsAvailable() || Machine == nullptr)
		{
			Session.reset();
			return;
		}
		FWorkoutSessionDependencies Dependencies;
		Dependencies.Machine = Machine;
		Dependencies.Sink = Sink.get();
		Dependencies.UnixTimeMs = []
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
		};
		const auto Entropy = std::make_shared<std::random_device>();
		Dependencies.RandomByte = [Entropy]
		{
			return static_cast<std::uint8_t>((*Entropy)());
		};
		FWorkoutSessionConfig Config;
		Config.Source = "pm5-tui";
		Session = std::make_unique<FWorkoutSession>(std::move(Dependencies), std::move(Config));
		Session->Tick(NowNs);
		LastLoggedState = Session->GetSnapshot().State;
		LastLoggedGapCount = 0;
	}

	void FWorkoutJournalDriver::SyncMachine(IRowingMachine *NewMachine, std::uint64_t NowNs)
	{
		if (NewMachine == Machine)
			return;
		if (Session)
			Session->Abort(NowNs);
		Session.reset();
		Machine = NewMachine;
		StartSession(NowNs);
	}

	void FWorkoutJournalDriver::Ingest(const FRowingMachineEvent &Event)
	{
		if (Session)
			Session->Ingest(Event);
	}

	std::vector<std::string> FWorkoutJournalDriver::DescribeChange()
	{
		std::vector<std::string> Lines;
		if (!Session)
			return Lines;
		const FWorkoutSnapshot &Snapshot = Session->GetSnapshot();
		if (Snapshot.State != LastLoggedState || Snapshot.GapCount != LastLoggedGapCount)
		{
			std::string Line = std::string("event=WorkoutSessionState state=") + ToString(Snapshot.State);
			if (Snapshot.Disposition)
				Line += std::string(" disposition=") + ToString(*Snapshot.Disposition);
			Line += Counts(Snapshot);
			Lines.push_back(std::move(Line));
			LastLoggedState = Snapshot.State;
			LastLoggedGapCount = Snapshot.GapCount;
		}
		return Lines;
	}

	std::vector<std::string> FWorkoutJournalDriver::Tick(std::uint64_t NowNs)
	{
		if (!Session)
			return {};
		Session->Tick(NowNs);
		std::vector<std::string> Lines = DescribeChange();
		// A finished row must not swallow the next one: start a fresh session.
		if (Session->GetSnapshot().State == ERowingSessionState::Ended)
			StartSession(NowNs);
		return Lines;
	}

	std::vector<std::string> FWorkoutJournalDriver::EndSession(std::uint64_t NowNs)
	{
		if (!Session)
			return {};
		Session->End(NowNs);
		std::vector<std::string> Lines = DescribeChange();
		StartSession(NowNs);
		return Lines;
	}

	std::string FWorkoutJournalDriver::StatusText() const
	{
		if (!IsAvailable())
			return "unavailable (" + Error + ")";
		if (!Session)
			return "waiting for a PM5";
		const FWorkoutSnapshot &Snapshot = Session->GetSnapshot();
		std::string Text = ToString(Snapshot.State);
		Text += " | " + std::to_string(Snapshot.AcceptedSampleCount) + " samples | " + std::to_string(Snapshot.GapCount) + " gaps";
		Text += Snapshot.bJournalHealthy ? " | journal OK" : " | JOURNAL ERRORS " + std::to_string(Snapshot.JournalErrorCount);
		return Text;
	}
} // namespace PM5Tui
