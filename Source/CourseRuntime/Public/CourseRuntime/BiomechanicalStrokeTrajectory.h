#pragma once

#include <cstdint>
#include <span>

enum class EBiomechanicalStrokePhase : std::uint8_t
{
	Drive,
	Recovery
};

struct FQuinticKinematicState
{
	double Position = 0.0;
	double Velocity = 0.0;
	double Acceleration = 0.0;
};

struct FQuinticHermiteKnot
{
	double TimeSeconds = 0.0;
	FQuinticKinematicState State;
};

struct FBiomechanicalStrokeState
{
	FQuinticKinematicState Stroke;
	FQuinticKinematicState Seat;
	FQuinticKinematicState Torso;
	FQuinticKinematicState Arms;
	FQuinticKinematicState Oar;
};

// Engine-independent authored stroke trajectories. Positions are normalized;
// velocities and accelerations are expressed per second and per second squared.
// The public presentation snapshot retains only the normalized positions.
class FBiomechanicalStrokeTrajectory final
{
  public:
	static FQuinticKinematicState MinimumJerk(double StartPosition,
											  double EndPosition,
											  double NormalizedTime,
											  double DurationSeconds) noexcept;

	static FQuinticKinematicState QuinticHermite(const FQuinticKinematicState &Start,
												 const FQuinticKinematicState &End,
												 double NormalizedTime,
												 double DurationSeconds) noexcept;

	static FQuinticKinematicState PiecewiseQuinticHermite(
		std::span<const FQuinticHermiteKnot> Knots,
		double TimeSeconds) noexcept;

	static FBiomechanicalStrokeState Evaluate(double StrokePose,
											  EBiomechanicalStrokePhase Phase,
											  double PhaseDurationSeconds) noexcept;
};
