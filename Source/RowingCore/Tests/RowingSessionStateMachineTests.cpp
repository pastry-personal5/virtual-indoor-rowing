#include "RowingCore/RowingSession.h"

#include <cstdlib>
#include <iostream>

namespace
{
	int Failures = 0;

	void Expect(const bool Condition, const char *const Expression, const char *const TestName)
	{
		if (!Condition)
		{
			std::cerr << TestName << ": expectation failed: " << Expression << '\n';
			++Failures;
		}
	}

#define EXPECT_TRUE(Expression) Expect((Expression), #Expression, __func__)

	FRowingSessionId MakeTestSessionId()
	{
		std::uint8_t NextByte = 0;
		return FRowingSessionId::GenerateV7(1758067200000ULL, [&NextByte]() mutable
											{ return NextByte++; });
	}

	void generated_session_id_carries_version_and_variant_bits()
	{
		const FRowingSessionId Id = MakeTestSessionId();
		const auto &Bytes = Id.GetBytes();
		EXPECT_TRUE((Bytes[6] & 0xF0) == 0x70);
		EXPECT_TRUE((Bytes[8] & 0xC0) == 0x80);
		EXPECT_TRUE(Id.ToCanonicalString().size() == 36);
	}

	void rowing_session_starts_created()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		EXPECT_TRUE(Machine.GetState() == ERowingSessionState::Created);
	}

	void rowing_session_created_to_active_on_session_started()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		const auto Change = Machine.TryTransition(ERowingSessionStateReason::SessionStarted);
		EXPECT_TRUE(Change.has_value());
		EXPECT_TRUE(Change->PreviousState == ERowingSessionState::Created);
		EXPECT_TRUE(Change->NewState == ERowingSessionState::Active);
		EXPECT_TRUE(!Change->Disposition.has_value());
		EXPECT_TRUE(Machine.GetState() == ERowingSessionState::Active);
	}

	void rowing_session_active_to_connection_lost_on_link_lost()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		Machine.TryTransition(ERowingSessionStateReason::SessionStarted);
		const auto Change = Machine.TryTransition(ERowingSessionStateReason::LinkLost);
		EXPECT_TRUE(Change.has_value());
		EXPECT_TRUE(Machine.GetState() == ERowingSessionState::ConnectionLost);
	}

	void rowing_session_connection_lost_recovers_to_active()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		Machine.TryTransition(ERowingSessionStateReason::SessionStarted);
		Machine.TryTransition(ERowingSessionStateReason::LinkLost);
		const auto Change = Machine.TryTransition(ERowingSessionStateReason::LinkRestored);
		EXPECT_TRUE(Change.has_value());
		EXPECT_TRUE(Machine.GetState() == ERowingSessionState::Active);
	}

	void rowing_session_connection_lost_to_ended_interrupted_on_recovered_after_unclean_exit()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		Machine.TryTransition(ERowingSessionStateReason::SessionStarted);
		Machine.TryTransition(ERowingSessionStateReason::LinkLost);
		const auto Change = Machine.TryTransition(ERowingSessionStateReason::RecoveredAfterUncleanExit);
		EXPECT_TRUE(Change.has_value());
		EXPECT_TRUE(Change->NewState == ERowingSessionState::Ended);
		EXPECT_TRUE(Change->Disposition == ERowingSessionDisposition::Interrupted);
	}

	void rowing_session_active_to_ended_completed_on_user_completed()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		Machine.TryTransition(ERowingSessionStateReason::SessionStarted);
		const auto Change = Machine.TryTransition(ERowingSessionStateReason::UserCompleted);
		EXPECT_TRUE(Change.has_value());
		EXPECT_TRUE(Change->Disposition == ERowingSessionDisposition::Completed);
	}

	void rowing_session_created_to_ended_aborted_on_user_aborted()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		const auto Change = Machine.TryTransition(ERowingSessionStateReason::UserAborted);
		EXPECT_TRUE(Change.has_value());
		EXPECT_TRUE(Change->PreviousState == ERowingSessionState::Created);
		EXPECT_TRUE(Change->Disposition == ERowingSessionDisposition::Aborted);
	}

	void rowing_session_ended_is_terminal()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		Machine.TryTransition(ERowingSessionStateReason::UserAborted);
		EXPECT_TRUE(Machine.GetState() == ERowingSessionState::Ended);
		EXPECT_TRUE(!Machine.TryTransition(ERowingSessionStateReason::SessionStarted).has_value());
		EXPECT_TRUE(!Machine.TryTransition(ERowingSessionStateReason::UserCompleted).has_value());
		EXPECT_TRUE(Machine.GetState() == ERowingSessionState::Ended);
	}

	void rowing_session_rejects_active_to_created()
	{
		FRowingSessionStateMachine Machine(MakeTestSessionId());
		Machine.TryTransition(ERowingSessionStateReason::SessionStarted);
		EXPECT_TRUE(!Machine.TryTransition(ERowingSessionStateReason::None).has_value());
		EXPECT_TRUE(Machine.GetState() == ERowingSessionState::Active);
	}
} // namespace

int main()
{
	generated_session_id_carries_version_and_variant_bits();
	rowing_session_starts_created();
	rowing_session_created_to_active_on_session_started();
	rowing_session_active_to_connection_lost_on_link_lost();
	rowing_session_connection_lost_recovers_to_active();
	rowing_session_connection_lost_to_ended_interrupted_on_recovered_after_unclean_exit();
	rowing_session_active_to_ended_completed_on_user_completed();
	rowing_session_created_to_ended_aborted_on_user_aborted();
	rowing_session_ended_is_terminal();
	rowing_session_rejects_active_to_created();

	return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
