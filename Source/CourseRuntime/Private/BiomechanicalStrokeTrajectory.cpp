#include "CourseRuntime/BiomechanicalStrokeTrajectory.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	constexpr double MinimumDurationSeconds = 1.0e-9;

	double Clamp01(double Value) noexcept
	{
		// The presentation runtime must never turn a malformed timing value into
		// non-finite component transforms. Treat it as the safe catch endpoint.
		if (!std::isfinite(Value))
			return 0.0;
		return std::clamp(Value, 0.0, 1.0);
	}

	FQuinticKinematicState Channel(double StrokePose,
								   EBiomechanicalStrokePhase Phase,
								   double Start,
								   double End,
								   double PhaseDurationSeconds) noexcept
	{
		const double Interval = End - Start;
		if (Interval <= 0.0)
			return {};
		if (Phase == EBiomechanicalStrokePhase::Drive)
		{
			const double U = (StrokePose - Start) / Interval;
			return FBiomechanicalStrokeTrajectory::MinimumJerk(
				0.0, 1.0, U, PhaseDurationSeconds * Interval);
		}

		const double RecoveryProgress = 1.0 - StrokePose;
		const double U = (RecoveryProgress - Start) / Interval;
		return FBiomechanicalStrokeTrajectory::MinimumJerk(
			1.0, 0.0, U, PhaseDurationSeconds * Interval);
	}
} // namespace

FQuinticKinematicState FBiomechanicalStrokeTrajectory::MinimumJerk(
	double StartPosition,
	double EndPosition,
	double NormalizedTime,
	double DurationSeconds) noexcept
{
	const double U = Clamp01(NormalizedTime);
	if (!(DurationSeconds > MinimumDurationSeconds) || !std::isfinite(DurationSeconds))
		return {U >= 1.0 ? EndPosition : StartPosition, 0.0, 0.0};
	const double U2 = U * U;
	const double U3 = U2 * U;
	const double U4 = U3 * U;
	const double U5 = U4 * U;
	const double Delta = EndPosition - StartPosition;
	const double Position = StartPosition + Delta * (10.0 * U3 - 15.0 * U4 + 6.0 * U5);
	const double Velocity = Delta * (30.0 * U2 - 60.0 * U3 + 30.0 * U4) / DurationSeconds;
	const double Acceleration = Delta * (60.0 * U - 180.0 * U2 + 120.0 * U3) /
								(DurationSeconds * DurationSeconds);
	return {Position, Velocity, Acceleration};
}

FQuinticKinematicState FBiomechanicalStrokeTrajectory::QuinticHermite(
	const FQuinticKinematicState &Start,
	const FQuinticKinematicState &End,
	double NormalizedTime,
	double DurationSeconds) noexcept
{
	const double U = Clamp01(NormalizedTime);
	if (!(DurationSeconds > MinimumDurationSeconds) || !std::isfinite(DurationSeconds))
		return U >= 1.0 ? End : Start;

	const double T = DurationSeconds;
	const double T2 = T * T;
	const double Delta = End.Position - Start.Position;
	const double C0 = Start.Position;
	const double C1 = Start.Velocity * T;
	const double C2 = 0.5 * Start.Acceleration * T2;
	const double C3 = 10.0 * Delta - (6.0 * Start.Velocity + 4.0 * End.Velocity) * T -
					  (1.5 * Start.Acceleration - 0.5 * End.Acceleration) * T2;
	const double C4 = -15.0 * Delta + (8.0 * Start.Velocity + 7.0 * End.Velocity) * T +
					  (1.5 * Start.Acceleration - End.Acceleration) * T2;
	const double C5 = 6.0 * Delta - (3.0 * Start.Velocity + 3.0 * End.Velocity) * T -
					  (0.5 * Start.Acceleration - 0.5 * End.Acceleration) * T2;
	const double U2 = U * U;
	const double U3 = U2 * U;
	const double U4 = U3 * U;
	const double U5 = U4 * U;
	const double Position = C0 + C1 * U + C2 * U2 + C3 * U3 + C4 * U4 + C5 * U5;
	const double Velocity = (C1 + 2.0 * C2 * U + 3.0 * C3 * U2 + 4.0 * C4 * U3 + 5.0 * C5 * U4) / T;
	const double Acceleration = (2.0 * C2 + 6.0 * C3 * U + 12.0 * C4 * U2 + 20.0 * C5 * U3) / T2;
	return {Position, Velocity, Acceleration};
}

FQuinticKinematicState FBiomechanicalStrokeTrajectory::PiecewiseQuinticHermite(
	std::span<const FQuinticHermiteKnot> Knots,
	double TimeSeconds) noexcept
{
	if (Knots.empty())
		return {};
	if (Knots.size() == 1 || TimeSeconds <= Knots.front().TimeSeconds)
		return Knots.front().State;
	if (TimeSeconds >= Knots.back().TimeSeconds)
		return Knots.back().State;

	for (std::size_t Index = 1; Index < Knots.size(); ++Index)
	{
		const FQuinticHermiteKnot &Before = Knots[Index - 1];
		const FQuinticHermiteKnot &After = Knots[Index];
		const double Duration = After.TimeSeconds - Before.TimeSeconds;
		if (TimeSeconds <= After.TimeSeconds)
		{
			if (!(Duration > MinimumDurationSeconds))
				return After.State;
			return QuinticHermite(Before.State, After.State, (TimeSeconds - Before.TimeSeconds) / Duration, Duration);
		}
	}
	return Knots.back().State;
}

FBiomechanicalStrokeState FBiomechanicalStrokeTrajectory::Evaluate(
	double StrokePose,
	EBiomechanicalStrokePhase Phase,
	double PhaseDurationSeconds) noexcept
{
	const double Pose = Clamp01(StrokePose);
	FBiomechanicalStrokeState Result;
	Result.Stroke = {Pose,
					 Phase == EBiomechanicalStrokePhase::Drive ? 1.0 / std::max(PhaseDurationSeconds, MinimumDurationSeconds)
															   : -1.0 / std::max(PhaseDurationSeconds, MinimumDurationSeconds),
					 0.0};
	if (!(PhaseDurationSeconds > MinimumDurationSeconds) || !std::isfinite(PhaseDurationSeconds))
		Result.Stroke = {Pose, 0.0, 0.0};

	if (Phase == EBiomechanicalStrokePhase::Drive)
	{
		Result.Seat = Channel(Pose, Phase, 0.00, 0.65, PhaseDurationSeconds);
		Result.Torso = Channel(Pose, Phase, 0.35, 0.88, PhaseDurationSeconds);
		Result.Arms = Channel(Pose, Phase, 0.72, 1.00, PhaseDurationSeconds);
	}
	else
	{
		Result.Seat = Channel(Pose, Phase, 0.35, 1.00, PhaseDurationSeconds);
		Result.Torso = Channel(Pose, Phase, 0.12, 0.65, PhaseDurationSeconds);
		Result.Arms = Channel(Pose, Phase, 0.00, 0.28, PhaseDurationSeconds);
	}

	// Two C2 Hermite segments reproduce the minimum-jerk sweep while making the
	// authored mid-stroke velocity knot explicit. It has one velocity maximum.
	const double Direction = Phase == EBiomechanicalStrokePhase::Drive ? 1.0 : -1.0;
	const double Progress = Phase == EBiomechanicalStrokePhase::Drive ? Pose : 1.0 - Pose;
	const std::array<FQuinticHermiteKnot, 3> OarKnots{{
		{0.0, {Phase == EBiomechanicalStrokePhase::Drive ? 0.0 : 1.0, 0.0, 0.0}},
		{0.5 * PhaseDurationSeconds,
		 {0.5, Direction * 1.875 / std::max(PhaseDurationSeconds, MinimumDurationSeconds), 0.0}},
		{PhaseDurationSeconds,
		 {Phase == EBiomechanicalStrokePhase::Drive ? 1.0 : 0.0, 0.0, 0.0}},
	}};
	Result.Oar = PiecewiseQuinticHermite(OarKnots, Progress * PhaseDurationSeconds);
	return Result;
}
