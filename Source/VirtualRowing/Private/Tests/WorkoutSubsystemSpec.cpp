#include "Misc/AutomationTest.h"

#include "Engine/GameInstance.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "WorkoutSubsystem.h"

#include "WorkoutRuntime/WorkoutDisplay.h"
#include "WorkoutRuntime/WorkoutSnapshot.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FWorkoutSubsystemSpec, "VirtualRowing.WorkoutSubsystem", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
TStrongObjectPtr<UGameInstance> GameInstance;
TStrongObjectPtr<UWorkoutSubsystem> Subsystem;
TSharedPtr<uint64> NowNs;

void AdvanceMs(uint64 Milliseconds)
{
	// Pump in device-frame-sized steps, as a running game would.
	for (uint64 Elapsed = 0; Elapsed < Milliseconds; Elapsed += 50)
	{
		*NowNs += 50ULL * 1000000ULL;
		Subsystem->Pump();
	}
}
END_DEFINE_SPEC(FWorkoutSubsystemSpec)

void FWorkoutSubsystemSpec::Define()
{
	BeforeEach([this]()
			   {
		NowNs = MakeShared<uint64>(1000000000ULL);
		// A game-instance subsystem must be outered to a UGameInstance.
		GameInstance.Reset(NewObject<UGameInstance>(GetTransientPackage()));
		Subsystem.Reset(NewObject<UWorkoutSubsystem>(GameInstance.Get()));
		Subsystem->SetHudEnabled(false);
		const TSharedPtr<uint64> Clock = NowNs;
		Subsystem->SetClockForTesting([Clock]() { return *Clock; }); });

	AfterEach([this]()
			  {
		 Subsystem.Reset();
		 GameInstance.Reset(); });

	It("shows the no-device display and never simulates without a simulator", [this]()
	   {
		TestFalse(TEXT("no device"), Subsystem->HasDevice());
		TestFalse(TEXT("no session"), Subsystem->HasSession());
		TestTrue(TEXT("no snapshot"), Subsystem->GetSnapshot() == nullptr);
		TestTrue(TEXT("no-device phase"), Subsystem->GetDisplay().Phase == EWorkoutDisplayPhase::NoDevice);
		Subsystem->Pump();
		TestTrue(TEXT("still no session after a pump"), !Subsystem->HasSession()); });

	It("rejects an unknown simulator fixture and stays idle", [this]()
	   {
		TestFalse(TEXT("unknown fixture rejected"), Subsystem->StartSimulator(TEXT("not_a_fixture")));
		TestFalse(TEXT("no device"), Subsystem->HasDevice()); });

	It("maps simulator snapshots to display values", [this]()
	   {
		TestTrue(TEXT("simulator starts"), Subsystem->StartSimulator(TEXT("steady30min")));
		AdvanceMs(5000);
		const FWorkoutSnapshot *Snapshot = Subsystem->GetSnapshot();
		if (!TestNotNull(TEXT("snapshot"), Snapshot))
			return;
		if (!TestTrue(TEXT("a sample arrived"), Snapshot->LatestSample.has_value()))
			return;
		const FWorkoutDisplay &Display = Subsystem->GetDisplay();
		TestTrue(TEXT("rowing"), Display.Phase == EWorkoutDisplayPhase::Rowing);
		TestEqual(TEXT("distance is the device distance in whole meters"), FString(UTF8_TO_TCHAR(Display.Distance.c_str())), FString::Printf(TEXT("%llu"), Snapshot->LatestSample->DistanceMm / 1000ULL));
		TestEqual(TEXT("elapsed matches the formatter"), FString(UTF8_TO_TCHAR(Display.Elapsed.c_str())), FString(UTF8_TO_TCHAR(FormatWorkoutElapsed(Snapshot->LatestSample->SourceElapsedMs).c_str())));
		TestNotEqual(TEXT("power shows the device average power when no stroke power is reported"), FString(UTF8_TO_TCHAR(Display.Watts.c_str())), FString(WorkoutDisplayPlaceholder));
		TestEqual(TEXT("display revision follows the snapshot"), Display.Revision, Snapshot->Revision);
		TestTrue(TEXT("Escape-independent: end is offered, start new is not"), Display.bCanEnd && !Display.bCanStartNew);

		const uint64 Generation = Subsystem->GetDisplayGeneration();
		Subsystem->Pump();
		TestEqual(TEXT("an unchanged snapshot does not bump the display generation"), Subsystem->GetDisplayGeneration(), Generation); });

	It("keeps draining the machine after the session ends", [this]()
	   {
		TestTrue(TEXT("simulator starts"), Subsystem->StartSimulator(TEXT("steady30min")));
		AdvanceMs(3000);
		TestTrue(TEXT("session ends"), Subsystem->EndSession());
		const FWorkoutDisplay EndedDisplay = Subsystem->GetDisplay();
		TestTrue(TEXT("ended phase"), EndedDisplay.Phase == EWorkoutDisplayPhase::Ended);
		TestTrue(TEXT("start new offered, end not"), EndedDisplay.bCanStartNew && !EndedDisplay.bCanEnd);
		TestFalse(TEXT("ending twice is refused"), Subsystem->EndSession());

		// Far more virtual time than the machine's 512-event queue holds at ~10 Hz.
		AdvanceMs(120000);
		TestEqual(TEXT("the machine queue is empty, not filling"), Subsystem->GetPendingMachineEventCountForTesting(), 0);
		TestEqual(TEXT("the ended session still shows its final distance"), FString(UTF8_TO_TCHAR(Subsystem->GetDisplay().Distance.c_str())), FString(UTF8_TO_TCHAR(EndedDisplay.Distance.c_str()))); });

	It("ends by itself when the device reports the workout complete or terminated", [this]()
	   {
		TestTrue(TEXT("simulator starts"), Subsystem->StartSimulator(TEXT("device_completed")));
		AdvanceMs(8000);
		TestTrue(TEXT("session ended without a user action"), Subsystem->GetDisplay().Phase == EWorkoutDisplayPhase::Ended);
		TestEqual(TEXT("banner"), FString(UTF8_TO_TCHAR(Subsystem->GetDisplay().Banner.c_str())), FString(TEXT("Session complete")));
		TestTrue(TEXT("Start New offered"), Subsystem->GetDisplay().bCanStartNew);

		TestTrue(TEXT("simulator starts"), Subsystem->StartSimulator(TEXT("device_terminated")));
		AdvanceMs(8000);
		TestTrue(TEXT("terminated ends too"), Subsystem->GetDisplay().Phase == EWorkoutDisplayPhase::Ended);
		TestEqual(TEXT("terminated is an abort"), FString(UTF8_TO_TCHAR(Subsystem->GetDisplay().Banner.c_str())), FString(TEXT("Session aborted"))); });

	It("survives a long stall without overflowing the machine queue", [this]()
	   {
		TestTrue(TEXT("simulator starts"), Subsystem->StartSimulator(TEXT("steady30min")));
		AdvanceMs(1000);
		const uint64 ElapsedBefore = Subsystem->GetSnapshot()->LatestSample->SourceElapsedMs;

		// A ten-minute pause (debugger break, suspended game): far more frames than the
		// machine's 512-event queue holds if it were all replayed at once.
		*NowNs += 600ULL * 1000000000ULL;
		Subsystem->Pump();
		TestFalse(TEXT("the machine has not failed"), Subsystem->GetDisplay().Connection == EWorkoutDisplayConnection::Failed);
		TestTrue(TEXT("still rowing"), Subsystem->GetDisplay().Phase == EWorkoutDisplayPhase::Rowing);
		TestEqual(TEXT("queue drained"), Subsystem->GetPendingMachineEventCountForTesting(), 0);

		AdvanceMs(1000);
		TestTrue(TEXT("the fixture continues after the stall"), Subsystem->GetSnapshot()->LatestSample->SourceElapsedMs > ElapsedBefore);
		TestTrue(TEXT("the stall was skipped, not replayed"), Subsystem->GetSnapshot()->LatestSample->SourceElapsedMs < ElapsedBefore + 5000);

		// A clock that moves backwards must neither crash nor rewind the session.
		const uint64 ElapsedNow = Subsystem->GetSnapshot()->LatestSample->SourceElapsedMs;
		*NowNs -= 30ULL * 1000000000ULL;
		Subsystem->Pump();
		TestTrue(TEXT("no rewind"), Subsystem->GetSnapshot()->LatestSample->SourceElapsedMs >= ElapsedNow); });

	It("replaces the ended session on Start New", [this]()
	   {
		TestTrue(TEXT("simulator starts"), Subsystem->StartSimulator(TEXT("steady30min")));
		AdvanceMs(2000);
		TestFalse(TEXT("Start New is refused while the session is live"), Subsystem->StartNewSession());
		const FRowingSessionId FirstId = Subsystem->GetSnapshot()->SessionId;
		TestTrue(TEXT("session ends"), Subsystem->EndSession());
		TestTrue(TEXT("Start New accepted"), Subsystem->StartNewSession());
		const FWorkoutSnapshot *Snapshot = Subsystem->GetSnapshot();
		if (!TestNotNull(TEXT("snapshot"), Snapshot))
			return;
		TestTrue(TEXT("new session is not ended"), Snapshot->State != ERowingSessionState::Ended);
		AdvanceMs(2000);
		TestTrue(TEXT("new session id"), !(Subsystem->GetSnapshot()->SessionId == FirstId));
		TestTrue(TEXT("new session is rowing"), Subsystem->GetDisplay().Phase == EWorkoutDisplayPhase::Rowing); });
}

#endif // WITH_DEV_AUTOMATION_TESTS
