"""Phase 2 content-origin fixture and canary checks.

The fixture is deliberately not presented as a cooked Unreal package.  It is a
deterministic origin/catalog exercise for the signed manifest, resumable
download, next-launch activation, withdrawal, rollback, and Standard fallback
state machine.  Publication of a real Han package requires an externally
cooked UE 5.8 IoStore pair and a release-owner signing seed.
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from urllib.parse import urlsplit

from vir_dev import common


# RFC 8032's published test key is only for this disposable fixture. It is
# deliberately not one of the client trust keys.
FIXTURE_TEST_SEED = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc4" "4449c5697b326919703bac031cae7f60")
FIXTURE_TEST_PUBLIC_KEY = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
CONTENT_SET = "han-river-alpha-1"
# This label is exclusive to the disposable canary fixture. Release package
# semantic versions are never sourced from the repository; release_package()
# requires VIR_CONTENT_VERSION from the signing environment.
FIXTURE_VERSION_LABEL = "fixture"
ROUTE_ID = "route.han-river.5k"
SOURCE_CANDIDATE_ROOT = common.ROOT / "Content" / "Phase2" / "HanRiver"
REVIEWED_PATH_MAP_RELATIVE = "Maps/L_HanRiver_BlueHour.umap"
# The owner-selected signed presentation distance. The runtime map's current
# water footprint is separately verified and does not alter this route contract.
HAN_KIT_FORWARD_COURSE_LENGTH_MM = 5_000_000
# Point 1 is the course/map start. Unreal presents the signed integer-mm frame
# in centimetres, so this is the nearest representable location to the owner
# requested map coordinate (40628.756492, 10112.881706, 0) cm.
HAN_RIVER_ROUTE_LOCAL_ORIGIN_MM = (406_288, 101_129, 0)
HAN_RIVER_START_UNREAL_CM = (40_628.8, 10_112.9, 0.0)
# Saved `HanRouteGate_CP00` through `HanRouteGate_CP20` X/Y transforms from
# L_HanRiver_BlueHour. CP01 and CP06 are distance-weighted/interpolated bends
# so the signed Hermite path retains the required 100 m minimum turn radius.
# CP20 follows the CP19-to-former-CP20 heading and is solved for the exact 5 km
# signed-path length.
AUTHORED_HAN_ROUTE_GATE_WORLD_MM = (
	(406_288, 101_129), (324_414, 83_024), (149_304, 44_302),
	(-28_393, 50_063), (-225_901, 18_178), (-414_930, 5_037),
	(-638_656, 27_518), (-862_382, 50_000), (-1_062_407, 100_000),
	(-1_238_943, 100_000), (-1_402_468, -22_000), (-1_645_405, -103_783),
	(-1_898_194, -239_600), (-2_155_319, -368_800), (-2_391_733, -418_800),
	(-2_707_262, -488_050), (-2_956_052, -422_700), (-3_293_803, -496_791),
	(-3_627_406, -614_800), (-3_872_193, -803_131), (-4_267_788, -1_040_335),
)
# This must stay byte-for-byte aligned with UContentSubsystem::TrustedKeys().
# The release command accepts only a private key whose public half is this
# current key; `content-next` is reserved for a separately reviewed rotation.
CURRENT_PUBLIC_KEY = bytes.fromhex("66c36d6a3036db503cab310d11c06bc42ac2bf725949cc5028b80abce1fa2468")
REQUIRED_IOSTORE_FILES = ("HanRiver.pak", "HanRiver.utoc", "HanRiver.ucas")
IOSTORE_TOC_MAGIC = b"-==--==--==--==-"


def _varint(value: int) -> bytes:
	if value < 0:
		raise ValueError("protobuf varint cannot encode a negative value")
	result = bytearray()
	while value > 0x7F:
		result.append((value & 0x7F) | 0x80)
		value >>= 7
	result.append(value)
	return bytes(result)


def _field(number: int, value: bytes | int, *, wire: int = 2) -> bytes:
	if wire == 0:
		return _varint(number << 3) + _varint(int(value))
	return _varint((number << 3) | wire) + _varint(len(value)) + value


def _string(number: int, value: str) -> bytes:
	return _field(number, value.encode())


def _bytes(number: int, value: bytes) -> bytes:
	return _field(number, value)


def _compat() -> bytes:
	# Build 2 is the first route-schema-v2 client. An old build therefore rejects
	# the catalog before activation instead of improvising legacy Han geometry.
	return b"".join((_field(1, 2, wire=0), _field(2, 2, wire=0), _field(3, 1, wire=0), _field(4, 2, wire=0)))


def _sint_field(number: int, value: int) -> bytes:
	zigzag = (value << 1) ^ (value >> 63)
	return _field(number, zigzag, wire=0)


def _vector(values: list[int] | tuple[int, int, int]) -> bytes:
	if len(values) != 3 or not all(isinstance(value, int) for value in values):
		raise ValueError("route path vector must contain three integers")
	return b"".join(_sint_field(index + 1, value) for index, value in enumerate(values) if value != 0)


def _canonical_lookup_bytes(entries: list[dict[str, int]]) -> bytes:
	result = bytearray(b"VIRPATHLOOKUP1\0")
	result += struct.pack("<I", len(entries))
	for entry in entries:
		result += struct.pack("<QII", entry["route_distance_mm"], entry["segment_index"], entry["segment_parameter_ppm"])
	return bytes(result)


def _fixture_path_source() -> dict[str, object]:
	"""Straight deterministic path used only by tool/unit fixtures."""
	points = []
	for index in range(8):
		distance = 5_000_000 * index // 7
		previous = 0 if index == 0 else 5_000_000 * (index - 1) // 7
		next_distance = 5_000_000 if index == 7 else 5_000_000 * (index + 1) // 7
		points.append({
			"id": f"fixture-{index}", "route_distance_mm": distance,
			"position_mm": [distance, 0, 0],
			"arrive_tangent_mm": [next_distance - distance if index == 0 else distance - previous, 0, 0],
			"leave_tangent_mm": [distance - previous if index == 7 else next_distance - distance, 0, 0],
		})
	lookup = []
	for distance in range(0, 5_000_001, 5_000):
		segment = 6 if distance == 5_000_000 else distance * 7 // 5_000_000
		start = 5_000_000 * segment // 7
		end = 5_000_000 * (segment + 1) // 7
		parameter = 1_000_000 if distance == 5_000_000 else (distance - start) * 1_000_000 // (end - start)
		lookup.append({"route_distance_mm": distance, "segment_index": segment, "segment_parameter_ppm": parameter})
	return {
		"schema_version": 2, "path_format_version": 1, "route_id": ROUTE_ID,
		"route_local_origin_mm": [0, 0, 0], "route_local_yaw_microradians": 0,
		"control_points": points, "arc_length_lookup": lookup,
	}


def _hermite_point(start: tuple[float, float], leave: tuple[float, float],
				   end: tuple[float, float], arrive: tuple[float, float], parameter: float) -> tuple[float, float]:
	"""Evaluate the horizontal signed route curve without Unreal types."""
	t = parameter
	t2 = t * t
	t3 = t2 * t
	return (
		(2.0 * t3 - 3.0 * t2 + 1.0) * start[0] + (t3 - 2.0 * t2 + t) * leave[0]
		+ (-2.0 * t3 + 3.0 * t2) * end[0] + (t3 - t2) * arrive[0],
		(2.0 * t3 - 3.0 * t2 + 1.0) * start[1] + (t3 - 2.0 * t2 + t) * leave[1]
		+ (-2.0 * t3 + 3.0 * t2) * end[1] + (t3 - t2) * arrive[1],
	)


def _legacy_river_centre_path_source() -> dict[str, object]:
	"""Build the reviewable, gently curving Han centreline approximation.

	The local frame is intentionally presentation-only: +X is forward progress
	and +Y is starboard. The frame is rotated 180 degrees and anchored at the
	owner-selected map start, so world-space progress is westbound.
	The offsets follow the broad bends in the reviewed virtual waterline while
	remaining near its visual centre, not a real-world navigation claim. All
	emitted geometry and lookup data are integer values.
	"""
	bend_profile = (
		(0, 0),
		(110_000, -28_000),
		(220_000, -72_000),
		(330_000, -82_000),
		(450_000, -32_000),
		(570_000, 32_000),
		(690_000, 78_000),
		(800_000, 92_000),
		(910_000, 50_000),
		(1_000_000, 0),
	)
	control_point_count = 21
	profile = []
	for index in range(control_point_count):
		fraction = 1_000_000 * index // (control_point_count - 1)
		source_fraction = 1_000_000 - fraction
		for bend_index, (start_fraction, start_offset) in enumerate(bend_profile[:-1]):
			end_fraction, end_offset = bend_profile[bend_index + 1]
			if source_fraction <= end_fraction:
				range_fraction = end_fraction - start_fraction
				offset = start_offset + (end_offset - start_offset) * (source_fraction - start_fraction) // range_fraction
				# The frame yaw rotates local Y as well as local X. Negating the
				# reversed offset retains the reviewed world-space waterline.
				profile.append((fraction, -offset))
				break
	# Four samples per metre on the shortest span keep inverse lookup error well
	# below the 5 m signed spacing while making repeatable source validation fast.
	samples_per_segment = 512

	def control_points(forward_mm: int) -> list[tuple[tuple[int, int], tuple[int, int], tuple[int, int]]]:
		positions = [(forward_mm * fraction // 1_000_000, lateral_mm) for fraction, lateral_mm in profile]
		result = []
		for index, position in enumerate(positions):
			previous = positions[max(index - 1, 0)]
			next_position = positions[min(index + 1, len(positions) - 1)]
			if index == 0:
				arrive = leave = (next_position[0] - position[0], next_position[1] - position[1])
			elif index == len(positions) - 1:
				arrive = leave = (position[0] - previous[0], position[1] - previous[1])
			else:
				arrive = leave = ((next_position[0] - previous[0]) // 2, (next_position[1] - previous[1]) // 2)
			result.append((position, arrive, leave))
		return result

	def sampled_curve(forward_mm: int) -> tuple[list[tuple[int, int, int]], float]:
		points = control_points(forward_mm)
		samples: list[tuple[int, int, int]] = []
		length = 0.0
		previous: tuple[float, float] | None = None
		for segment in range(len(points) - 1):
			start, _, leave = points[segment]
			end, arrive, _ = points[segment + 1]
			for step in range(samples_per_segment + 1):
				if segment and step == 0:
					continue
				parameter = step / samples_per_segment
				position = _hermite_point(start, leave, end, arrive, parameter)
				if previous is not None:
					length += ((position[0] - previous[0]) ** 2 + (position[1] - previous[1]) ** 2) ** 0.5
				previous = position
				samples.append((segment, step, int(round(length))))
		return samples, length

	# Scale the forward axis until the signed curve's actual arc length reaches
	# the usable forward half of the fixed Han kit. The binary search result is
	# integer millimetres, so repeat runs are byte-stable.
	low, high = HAN_KIT_FORWARD_COURSE_LENGTH_MM - 200_000, HAN_KIT_FORWARD_COURSE_LENGTH_MM
	while low < high:
		middle = (low + high + 1) // 2
		if sampled_curve(middle)[1] <= HAN_KIT_FORWARD_COURSE_LENGTH_MM:
			low = middle
		else:
			high = middle - 1
	points = control_points(low)
	samples, measured_length = sampled_curve(low)
	if abs(measured_length - HAN_KIT_FORWARD_COURSE_LENGTH_MM) > 500.0:
		raise ValueError("Han river-centre source cannot meet the Han-kit arc-length tolerance")

	control_distances = [0]
	for segment in range(1, len(points) - 1):
		control_distances.append(next(distance for index, step, distance in samples if index == segment - 1 and step == samples_per_segment))
	control_distances.append(HAN_KIT_FORWARD_COURSE_LENGTH_MM)
	if any(right <= left for left, right in zip(control_distances, control_distances[1:])):
		raise ValueError("Han river-centre source has unordered control points")

	lookup = []
	for route_distance in range(0, HAN_KIT_FORWARD_COURSE_LENGTH_MM + 1, 5_000):
		if route_distance == HAN_KIT_FORWARD_COURSE_LENGTH_MM:
			lookup.append({"route_distance_mm": route_distance, "segment_index": len(points) - 2, "segment_parameter_ppm": 1_000_000})
			continue
		for sample_index in range(1, len(samples)):
			previous_sample = samples[sample_index - 1]
			next_sample = samples[sample_index]
			if next_sample[2] < route_distance:
				continue
			span = next_sample[2] - previous_sample[2]
			fraction = 0.0 if span == 0 else (route_distance - previous_sample[2]) / span
			segment = next_sample[0]
			step = next_sample[1] - 1 + fraction
			lookup.append({
				"route_distance_mm": route_distance,
				"segment_index": segment,
				"segment_parameter_ppm": int(round(step * 1_000_000 / samples_per_segment)),
			})
			break
		else:
			raise ValueError("Han river-centre lookup did not reach its route distance")

	return {
		"schema_version": 2, "path_format_version": 1, "route_id": ROUTE_ID,
		"route_local_origin_mm": list(HAN_RIVER_ROUTE_LOCAL_ORIGIN_MM),
		"route_local_yaw_microradians": 3_141_593,
		"control_points": [
			{
				"id": f"river-centre-{index + 1:02d}",
				"route_distance_mm": control_distances[index],
				"position_mm": [position[0], position[1], 0],
				"arrive_tangent_mm": [arrive[0], arrive[1], 0],
				"leave_tangent_mm": [leave[0], leave[1], 0],
			}
			for index, (position, arrive, leave) in enumerate(points)
		],
		"arc_length_lookup": lookup,
	}


def _river_centre_path_source() -> dict[str, object]:
	"""Resample the Blue Hour water-tile centreline into the signed 5 km path.

	The source points are millimetre-quantized world-space centres sampled from
	the connected OSM water-tile corridor in ``L_HanRiver_BlueHour``. Point 1 is
	the owner-selected start. Remaining control points are equal-distance samples
	of that corridor, not an affine translation of the former path.
	"""
	# Only turning points and changes in the 50 m water-tile scan are retained;
	# straight spans interpolate exactly between them. Coordinates are Unreal mm.
	world_centreline_mm = (
		(406_288, 101_129), (306_288, -114_330), (256_288, -129_248),
		(206_288, -129_248), (156_288, -550_000), (106_288, -550_000),
		(56_288, -450_000), (6_288, -450_000), (-43_712, -400_000),
		(-93_712, -400_000), (-143_712, -300_000), (-193_712, -250_000),
		(-243_712, -150_000), (-293_712, -150_000), (-343_712, -100_000),
		(-493_712, -100_000), (-543_712, 0), (-593_712, 0),
		(-643_712, 50_000), (-893_712, 50_000), (-943_712, 100_000),
		(-1_393_712, 100_000), (-1_443_712, 150_000),
		(-1_493_712, 150_000), (-1_543_712, 250_000),
		(-1_593_712, 200_000), (-1_643_712, 150_000),
		(-1_693_712, 150_000), (-1_743_712, 100_000),
		(-1_793_712, 100_000), (-1_843_712, 50_000),
		(-2_093_712, 50_000), (-2_143_712, 0), (-2_193_712, 0),
		(-2_243_712, -50_000), (-2_293_712, -50_000),
		(-2_343_712, -100_000), (-2_393_712, -100_000),
		(-2_443_712, -150_000), (-2_493_712, -150_000),
		(-2_543_712, -200_000), (-2_593_712, -200_000),
		(-2_643_712, -250_000), (-2_693_712, -250_000),
		(-2_743_712, -300_000), (-2_793_712, -300_000),
		(-2_843_712, -350_000), (-2_893_712, -350_000),
		(-2_943_712, -450_000), (-2_993_712, -450_000),
		(-3_043_712, -500_000), (-3_093_712, -500_000),
		(-3_143_712, -600_000), (-3_293_712, -600_000),
		(-3_343_712, -700_000), (-3_493_712, -700_000),
		(-3_543_712, -750_000), (-3_593_712, -750_000),
		(-3_643_712, -800_000), (-3_693_712, -800_000),
		(-3_743_712, -900_000), (-3_793_712, -900_000),
		(-3_843_712, -1_050_000), (-3_893_712, -1_200_000),
		(-3_943_712, -1_300_000), (-3_993_712, -1_300_000),
		(-4_043_712, -1_350_000), (-4_193_712, -1_350_000),
		(-4_243_712, -1_400_000), (-4_293_712, -1_400_000),
		(-4_343_712, -1_450_000), (-4_493_712, -1_450_000),
		(-4_543_712, -1_500_000), (-4_593_712, -1_500_000),
	)
	frame_yaw = 3.141593
	cos_yaw = math.cos(frame_yaw)
	sin_yaw = math.sin(frame_yaw)
	origin_x, origin_y, _ = HAN_RIVER_ROUTE_LOCAL_ORIGIN_MM
	local_centreline = []
	for world_x, world_y in world_centreline_mm:
		delta_x = world_x - origin_x
		delta_y = world_y - origin_y
		local_centreline.append((
			int(round(cos_yaw * delta_x + sin_yaw * delta_y)),
			int(round(-sin_yaw * delta_x + cos_yaw * delta_y)),
		))
	local_centreline[0] = (0, 0)

	segment_lengths = [math.dist(left, right) for left, right in zip(local_centreline, local_centreline[1:])]
	prefix_lengths = [0.0]
	for length in segment_lengths:
		prefix_lengths.append(prefix_lengths[-1] + length)

	def point_at(distance_mm: float) -> tuple[int, int]:
		distance_mm = min(max(distance_mm, 0.0), prefix_lengths[-1])
		for index, end_distance in enumerate(prefix_lengths[1:]):
			if distance_mm <= end_distance:
				start_distance = prefix_lengths[index]
				alpha = 0.0 if end_distance == start_distance else (distance_mm - start_distance) / (end_distance - start_distance)
				start = local_centreline[index]
				end = local_centreline[index + 1]
				return (int(round(start[0] + (end[0] - start[0]) * alpha)), int(round(start[1] + (end[1] - start[1]) * alpha)))
		return local_centreline[-1]

	def control_points(terminal_distance_mm: int) -> list[tuple[tuple[int, int], tuple[int, int], tuple[int, int]]]:
		positions = [point_at(terminal_distance_mm * index / 20.0) for index in range(21)]
		result = []
		for index, position in enumerate(positions):
			previous = positions[max(index - 1, 0)]
			next_position = positions[min(index + 1, len(positions) - 1)]
			if index == 0:
				arrive = leave = (next_position[0] - position[0], next_position[1] - position[1])
			elif index == len(positions) - 1:
				arrive = leave = (position[0] - previous[0], position[1] - previous[1])
			else:
				arrive = leave = ((next_position[0] - previous[0]) // 2, (next_position[1] - previous[1]) // 2)
			result.append((position, arrive, leave))
		return result

	samples_per_segment = 512

	def sampled_curve(points: list[tuple[tuple[int, int], tuple[int, int], tuple[int, int]]]) -> tuple[list[tuple[int, int, int]], float]:
		samples: list[tuple[int, int, int]] = []
		length = 0.0
		previous: tuple[float, float] | None = None
		for segment in range(len(points) - 1):
			start, _, leave = points[segment]
			end, arrive, _ = points[segment + 1]
			for step in range(samples_per_segment + 1):
				if segment and step == 0:
					continue
				position = _hermite_point(start, leave, end, arrive, step / samples_per_segment)
				if previous is not None:
					length += math.dist(position, previous)
				previous = position
				samples.append((segment, step, int(round(length))))
		return samples, length

	low, high = 1, int(prefix_lengths[-1])
	while low < high:
		middle = (low + high + 1) // 2
		if sampled_curve(control_points(middle))[1] <= HAN_KIT_FORWARD_COURSE_LENGTH_MM:
			low = middle
		else:
			high = middle - 1
	points = control_points(low)
	samples, measured_length = sampled_curve(points)
	if abs(measured_length - HAN_KIT_FORWARD_COURSE_LENGTH_MM) > 500.0:
		raise ValueError("water-tile centreline cannot meet the signed 5 km tolerance")

	control_distances = [0]
	for segment in range(1, len(points) - 1):
		control_distances.append(next(distance for index, step, distance in samples if index == segment - 1 and step == samples_per_segment))
	control_distances.append(HAN_KIT_FORWARD_COURSE_LENGTH_MM)
	lookup = []
	for route_distance in range(0, HAN_KIT_FORWARD_COURSE_LENGTH_MM + 1, 5_000):
		if route_distance == HAN_KIT_FORWARD_COURSE_LENGTH_MM:
			lookup.append({"route_distance_mm": route_distance, "segment_index": len(points) - 2, "segment_parameter_ppm": 1_000_000})
			continue
		for sample_index in range(1, len(samples)):
			previous_sample = samples[sample_index - 1]
			next_sample = samples[sample_index]
			if next_sample[2] < route_distance:
				continue
			span = next_sample[2] - previous_sample[2]
			fraction = 0.0 if span == 0 else (route_distance - previous_sample[2]) / span
			lookup.append({"route_distance_mm": route_distance, "segment_index": next_sample[0], "segment_parameter_ppm": int(round((next_sample[1] - 1 + fraction) * 1_000_000 / samples_per_segment))})
			break
		else:
			raise ValueError("water-tile centreline lookup did not reach its route distance")

	return {
		"schema_version": 2, "path_format_version": 1, "route_id": ROUTE_ID,
		"route_local_origin_mm": list(HAN_RIVER_ROUTE_LOCAL_ORIGIN_MM),
		"route_local_yaw_microradians": 3_141_593,
		"control_points": [{
			"id": f"river-centre-{index + 1:02d}", "route_distance_mm": control_distances[index],
			"position_mm": [position[0], position[1], 0],
			"arrive_tangent_mm": [arrive[0], arrive[1], 0],
			"leave_tangent_mm": [leave[0], leave[1], 0],
		} for index, (position, arrive, leave) in enumerate(points)],
		"arc_length_lookup": lookup,
	}


def _authored_gate_path_source() -> dict[str, object]:
	"""Compile the saved Blue Hour route-gate transforms into a 5 km path."""
	frame_yaw = 3.141593
	cos_yaw = math.cos(frame_yaw)
	sin_yaw = math.sin(frame_yaw)
	origin_x, origin_y, _ = HAN_RIVER_ROUTE_LOCAL_ORIGIN_MM
	local_positions = [
		(
			int(round(cos_yaw * (world_x - origin_x) + sin_yaw * (world_y - origin_y))),
			int(round(-sin_yaw * (world_x - origin_x) + cos_yaw * (world_y - origin_y))),
		)
		for world_x, world_y in AUTHORED_HAN_ROUTE_GATE_WORLD_MM
	]
	local_positions[0] = (0, 0)

	points = []
	for index, position in enumerate(local_positions):
		previous = local_positions[max(index - 1, 0)]
		next_position = local_positions[min(index + 1, len(local_positions) - 1)]
		if index == 0:
			arrive = leave = (next_position[0] - position[0], next_position[1] - position[1])
		elif index == len(local_positions) - 1:
			arrive = leave = (position[0] - previous[0], position[1] - previous[1])
		else:
			arrive = leave = ((next_position[0] - previous[0]) // 2, (next_position[1] - previous[1]) // 2)
		points.append((position, arrive, leave))

	samples_per_segment = 512
	samples: list[tuple[int, int, int]] = []
	measured_length = 0.0
	previous_sample: tuple[float, float] | None = None
	for segment in range(len(points) - 1):
		start, _, leave = points[segment]
		end, arrive, _ = points[segment + 1]
		for step in range(samples_per_segment + 1):
			if segment and step == 0:
				continue
			position = _hermite_point(start, leave, end, arrive, step / samples_per_segment)
			if previous_sample is not None:
				measured_length += math.dist(position, previous_sample)
			previous_sample = position
			samples.append((segment, step, int(round(measured_length))))
	if abs(measured_length - HAN_KIT_FORWARD_COURSE_LENGTH_MM) > 500.0:
		raise ValueError("authored Han route gates cannot meet the signed 5 km tolerance")

	control_distances = [0]
	for segment in range(1, len(points) - 1):
		control_distances.append(next(distance for index, step, distance in samples if index == segment - 1 and step == samples_per_segment))
	control_distances.append(HAN_KIT_FORWARD_COURSE_LENGTH_MM)
	lookup = []
	for route_distance in range(0, HAN_KIT_FORWARD_COURSE_LENGTH_MM + 1, 5_000):
		if route_distance == HAN_KIT_FORWARD_COURSE_LENGTH_MM:
			lookup.append({"route_distance_mm": route_distance, "segment_index": len(points) - 2, "segment_parameter_ppm": 1_000_000})
			continue
		for sample_index in range(1, len(samples)):
			previous, next_sample = samples[sample_index - 1], samples[sample_index]
			if next_sample[2] < route_distance:
				continue
			span = next_sample[2] - previous[2]
			fraction = 0.0 if span == 0 else (route_distance - previous[2]) / span
			lookup.append({
				"route_distance_mm": route_distance,
				"segment_index": next_sample[0],
				"segment_parameter_ppm": int(round((next_sample[1] - 1 + fraction) * 1_000_000 / samples_per_segment)),
			})
			break
		else:
			raise ValueError("authored Han route-gate lookup did not reach its route distance")

	return {
		"schema_version": 2, "path_format_version": 1, "route_id": ROUTE_ID,
		"route_local_origin_mm": list(HAN_RIVER_ROUTE_LOCAL_ORIGIN_MM),
		"route_local_yaw_microradians": 3_141_593,
		"control_points": [{
			"id": f"han-route-gate-{index:02d}", "route_distance_mm": control_distances[index],
			"position_mm": [position[0], position[1], 0],
			"arrive_tangent_mm": [arrive[0], arrive[1], 0],
			"leave_tangent_mm": [leave[0], leave[1], 0],
		} for index, (position, arrive, leave) in enumerate(points)],
		"arc_length_lookup": lookup,
	}


def _candidate_path_source() -> dict[str, object]:
	"""Return the reproducible, deliberately unapproved Han v2 path candidate.

	It gives review tooling an exact integer-mm payload and lookup to inspect.
	It is not a substitute for the Unreal map calibration, clearance audit, or
	owner endpoint approval required before publication.
	"""
	source = _authored_gate_path_source()
	source.update({
		"owner_approval": "pending",
		"review_status": "candidate-generated-not-approved",
		"generator": {
			"id": "vir.han-authored-route-gates.v1",
			"lookup_spacing_mm": 5_000,
			"note": "A deterministic 5 km path compiled from the saved Blue Hour route-gate transforms pending Unreal map review.",
		},
		"review_inputs": {
			"route_beats_sha256": hashlib.sha256((SOURCE_CANDIDATE_ROOT / "route-beats.json").read_bytes()).hexdigest(),
			"waterline_sha256": hashlib.sha256((SOURCE_CANDIDATE_ROOT / "han-river-5k.geojson").read_bytes()).hexdigest(),
		},
	})
	return source


def _candidate_path_source_bytes() -> bytes:
	return (json.dumps(_candidate_path_source(), indent=2, sort_keys=True) + "\n").encode()


def _owner_approved_path_source() -> dict[str, object]:
	"""Bind an owner decision to the exact reviewed runtime-map bytes.

	ADR-0016 removes the baseline/calibration and clearance audit as a
	publication prerequisite. They remain optional evidence for later course
	work; this release gate preserves only the owner decision and map identity.
	"""
	source = _candidate_path_source()
	map_path = SOURCE_CANDIDATE_ROOT / REVIEWED_PATH_MAP_RELATIVE
	if not map_path.is_file():
		raise ValueError(f"reviewed Han map is missing: {map_path}")
	source.update({
		"owner_approval": "approved",
		"review_status": "owner-approved-map-identity",
		"review": {
			"map": REVIEWED_PATH_MAP_RELATIVE,
			"map_sha256": hashlib.sha256(map_path.read_bytes()).hexdigest(),
			"owner_reviewed_on": "2026-09-29",
			"record": "Owner decision: approve the v2 Han path for publication under ADR-0016.",
		},
	})
	return source


def _owner_approved_path_source_bytes() -> bytes:
	return (json.dumps(_owner_approved_path_source(), indent=2, sort_keys=True) + "\n").encode()


def generate_path_source_candidate(output: Path | None = None) -> int:
	"""Write or verify the deterministic, unapproved v2 Han path source."""
	path = output or SOURCE_CANDIDATE_ROOT / "presentation-path-v2.json"
	expected = _candidate_path_source_bytes()
	if path.exists():
		if path.read_bytes() != expected:
			if path.read_bytes() == _owner_approved_path_source_bytes():
				print(f"OK owner-approved Han v2 path source: {path}")
				return 0
			raise ValueError(f"{path.name} differs from its deterministic candidate generator")
		print(f"OK deterministic Han v2 path candidate: {path} (owner approval pending)")
		return 0
	path.write_bytes(expected)
	print(f"OK wrote deterministic Han v2 path candidate: {path} (owner approval pending)")
	return 0


def approve_path_source(output: Path | None = None) -> int:
	"""Record the owner's approval against the exact reviewed runtime map."""
	path = output or SOURCE_CANDIDATE_ROOT / "presentation-path-v2.json"
	if not path.is_file():
		raise ValueError(f"{path.name} must be generated before it can be approved")
	if path.read_bytes() == _owner_approved_path_source_bytes():
		print(f"OK owner-approved Han v2 path source: {path}")
		return 0
	if path.read_bytes() != _candidate_path_source_bytes():
		raise ValueError(f"{path.name} differs from the deterministic candidate and cannot be approved")
	path.write_bytes(_owner_approved_path_source_bytes())
	print(f"OK owner-approved Han v2 path source: {path}")
	return 0


def _presentation_path(source: dict[str, object]) -> bytes:
	if source.get("schema_version") != 2 or source.get("path_format_version") != 1 or source.get("route_id") != ROUTE_ID:
		raise ValueError("route path source identity or format is invalid")
	points = source.get("control_points")
	lookup = source.get("arc_length_lookup")
	if not isinstance(points, list) or not 8 <= len(points) <= 40 or not isinstance(lookup, list) or len(lookup) < 2:
		raise ValueError("route path source must contain 8-40 points and a lookup")
	encoded_points = []
	for point in points:
		if not isinstance(point, dict):
			raise ValueError("route path control point is invalid")
		encoded_points.append(b"".join((
			_string(1, point["id"]),
			_field(2, point["route_distance_mm"], wire=0) if point["route_distance_mm"] else b"",
			_field(3, _vector(point["position_mm"])), _field(4, _vector(point["arrive_tangent_mm"])),
			_field(5, _vector(point["leave_tangent_mm"])),
		)))
	encoded_lookup = []
	for entry in lookup:
		if not isinstance(entry, dict):
			raise ValueError("route path lookup entry is invalid")
		encoded_lookup.append(b"".join((
			_field(1, entry["route_distance_mm"], wire=0) if entry["route_distance_mm"] else b"",
			_field(2, entry["segment_index"], wire=0) if entry["segment_index"] else b"",
			_field(3, entry["segment_parameter_ppm"], wire=0) if entry["segment_parameter_ppm"] else b"",
		)))
	return b"".join((
		_field(1, 1, wire=0), _string(2, ROUTE_ID),
		_field(3, _vector(source["route_local_origin_mm"])),
		_sint_field(4, source["route_local_yaw_microradians"]) if source["route_local_yaw_microradians"] else b"",
		*(_field(5, point) for point in encoded_points),
		*(_field(6, entry) for entry in encoded_lookup),
		_bytes(7, hashlib.sha256(_canonical_lookup_bytes(lookup)).digest()),
	))


def _route(metadata_hash: bytes = b"") -> bytes:
	checkpoint = _string(1, "bridge-1") + _field(2, 1_000_000, wire=0)
	fields = [
		_field(1, 2, wire=0), _string(2, ROUTE_ID), _string(3, FIXTURE_VERSION_LABEL),
		_string(4, CONTENT_SET), _field(5, HAN_KIT_FORWARD_COURSE_LENGTH_MM, wire=0),
		_field(7, checkpoint), _field(8, _compat()),
		_string(9, "route.han_river.name"), _string(10, "route.han_river.description"),
	]
	if metadata_hash:
		fields.append(_bytes(11, metadata_hash))
	fields.append(_field(12, _presentation_path(_fixture_path_source())))
	return b"".join(fields)


def _archive(entries: dict[str, bytes]) -> bytes:
	result = bytearray(b"VIRCNT1\n")
	result += struct.pack("<I", len(entries))
	for path in sorted(entries):
		payload = entries[path]
		encoded = path.encode()
		result += struct.pack("<HQ", len(encoded), len(payload))
		result += hashlib.sha256(payload).digest()
		result += encoded + payload
	return bytes(result)


def _archive_file(output: Path, entries: dict[str, bytes | Path]) -> int:
	"""Write the bounded VIRCNT container without loading cooked IoStore data into RAM."""
	if output.exists():
		raise ValueError(f"refusing to overwrite existing release artifact: {output}")
	output.parent.mkdir(parents=True, exist_ok=True)
	with output.open("xb") as destination:
		destination.write(b"VIRCNT1\n")
		destination.write(struct.pack("<I", len(entries)))
		for relative_path in sorted(entries):
			value = entries[relative_path]
			if isinstance(value, Path):
				if not value.is_file() or value.is_symlink():
					raise ValueError(f"cooked content member is missing or unsafe: {value}")
				size = value.stat().st_size
				digest = _sha256_file(value)
			else:
				size = len(value)
				digest = hashlib.sha256(value).digest()
			encoded_path = relative_path.encode("ascii")
			if not encoded_path or len(encoded_path) > 512 or size <= 0:
				raise ValueError(f"release archive member is invalid: {relative_path}")
			destination.write(struct.pack("<HQ", len(encoded_path), size))
			destination.write(digest)
			destination.write(encoded_path)
			if isinstance(value, Path):
				with value.open("rb") as source:
					shutil.copyfileobj(source, destination, length=1024 * 1024)
			else:
				destination.write(value)
	return output.stat().st_size


def _sha256_file(path: Path) -> bytes:
	digest = hashlib.sha256()
	with path.open("rb") as source:
		for chunk in iter(lambda: source.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.digest()


def _validate_cooked_iostore(cooked_members: dict[str, Path]) -> None:
	for name, path in cooked_members.items():
		if not path.is_file() or path.is_symlink() or path.stat().st_size == 0:
			raise ValueError(f"required cooked IoStore member is missing or unsafe: {name}")
	with cooked_members["HanRiver.utoc"].open("rb") as source:
		if source.read(len(IOSTORE_TOC_MAGIC)) != IOSTORE_TOC_MAGIC:
			raise ValueError("HanRiver.utoc does not have the Unreal IoStore TOC header")


def _inventory(entries: dict[str, bytes]) -> bytes:
	# ContentInventoryV1: schema_version=1, content_set_id=2, entries=3.
	result = bytearray(_field(1, 1, wire=0) + _string(2, CONTENT_SET))
	classes = {"licenses/NOTICE.txt": "LicenseNotice", "route.pb": "DataAsset"}
	for path in sorted(entries):
		if path in {"inventory.pb", "HanRiver.pak", "HanRiver.utoc", "HanRiver.ucas"}:
			continue
		entry = _string(1, path) + _string(2, classes.get(path, "DataAsset"))
		entry += _field(3, len(entries[path]), wire=0) + _bytes(4, hashlib.sha256(entries[path]).digest())
		result += _field(3, bytes(entry))
	return bytes(result)


def _manifest(route_bytes: bytes, inventory_bytes: bytes, package: bytes, revision: int, issued: int, *, withdrawn: bool = False) -> bytes:
	unsigned = b"".join((
		_field(1, 1, wire=0), _field(2, revision, wire=0), _field(3, issued, wire=0),
		_field(4, issued + 7 * 24 * 60 * 60, wire=0), _field(5, _compat()),
		_field(6, route_bytes), _string(7, f"https://origin.invalid/content/{CONTENT_SET}/{FIXTURE_VERSION_LABEL}/HanRiver.vircontent"),
		_bytes(8, hashlib.sha256(package).digest()), _field(9, len(package), wire=0),
		_field(10, sum(len(value) for value in (route_bytes, inventory_bytes)) + len(package), wire=0),
		_bytes(11, hashlib.sha256(inventory_bytes).digest()),
		_field(12, 1, wire=0) if withdrawn else b"",
		_string(13, "content.han.withdrawn") if withdrawn else b"",
	))
	# Use the host OpenSSL Ed25519 implementation so the canary has no Python
	# package dependency. This is the published RFC test key, not a production
	# signing secret; the release command must supply the restricted key instead.
	with tempfile.TemporaryDirectory(prefix="vir-content-sign-") as temp:
		key_path = Path(temp) / "key.der"
		payload_path = Path(temp) / "payload"
		key_path.write_bytes(bytes.fromhex("302e020100300506032b657004220420") + FIXTURE_TEST_SEED)
		payload_path.write_bytes(unsigned)
		signature = subprocess.run(
			["openssl", "pkeyutl", "-sign", "-inkey", str(key_path), "-rawin", "-in", str(payload_path)],
			check=True, capture_output=True,
		).stdout
	envelope = _field(1, 1, wire=0) + _bytes(2, unsigned) + _string(3, "fixture-test") + _bytes(4, signature)
	return bytes(envelope)


def _release_route(beats: list[dict[str, object]], semantic_version: str, path_source: dict[str, object] | None = None) -> bytes:
	"""Create the canonical route definition from the approved beat source."""
	checkpoints = []
	for beat in beats:
		distance_m = beat.get("distance_m")
		checkpoint_id = beat.get("id")
		if not isinstance(distance_m, int) or not isinstance(checkpoint_id, str):
			raise ValueError("route beat has an invalid checkpoint")
		if 0 < distance_m * 1000 < HAN_KIT_FORWARD_COURSE_LENGTH_MM:
			checkpoints.append(_string(1, checkpoint_id) + _field(2, distance_m * 1000, wire=0))
	prefix = b"".join((
		_field(1, 2, wire=0), _string(2, ROUTE_ID), _string(3, semantic_version),
		_string(4, CONTENT_SET), _field(5, HAN_KIT_FORWARD_COURSE_LENGTH_MM, wire=0),
		*(_field(7, checkpoint) for checkpoint in checkpoints),
		_field(8, _compat()), _string(9, "route.han_river.name"),
		_string(10, "route.han_river.description"),
	))
	path = _field(12, _presentation_path(path_source or _fixture_path_source()))
	hashable = prefix + path
	return prefix + _bytes(11, hashlib.sha256(hashable).digest()) + path


def _load_reviewed_path_source() -> dict[str, object]:
	path = SOURCE_CANDIDATE_ROOT / "presentation-path-v2.json"
	if not path.is_file():
		raise ValueError("presentation-path-v2.json is required after Unreal MCP baseline capture and owner path approval")
	source = json.loads(path.read_text(encoding="utf-8"))
	if "semantic_version" in source or source.get("owner_approval") != "approved":
		raise ValueError("Han v2 path source must not carry a package version and must be owner-approved")
	review = source.get("review")
	if not isinstance(review, dict) or source.get("review_status") != "owner-approved-map-identity" or review.get("map") != REVIEWED_PATH_MAP_RELATIVE:
		raise ValueError("Han v2 path source lacks the required owner-approved map identity")
	map_path = SOURCE_CANDIDATE_ROOT / REVIEWED_PATH_MAP_RELATIVE
	if not map_path.is_file() or review.get("map_sha256") != hashlib.sha256(map_path.read_bytes()).hexdigest():
		raise ValueError("Han v2 path source no longer matches the owner-reviewed map")
	if not isinstance(review.get("owner_reviewed_on"), str) or not review.get("record"):
		raise ValueError("Han v2 path source lacks the owner decision record")
	# Encoding performs the structural checks again before release assembly.
	_presentation_path(source)
	return source


def _release_inventory(route: bytes, notice: bytes) -> bytes:
	"""Build the only allowable package-side inventory entries.

	Cooked asset payloads stay inside the IoStore pair; those three transport
	members are structural archive entries, not raw-package paths exposed to the
	runtime. Route metadata and the notice remain individually inventory-bound.
	"""
	entries = {
		"licenses/NOTICE.txt": ("LicenseNotice", notice),
		"route.pb": ("DataAsset", route),
	}
	result = bytearray(_field(1, 1, wire=0) + _string(2, CONTENT_SET))
	for relative_path in sorted(entries):
		asset_class, payload = entries[relative_path]
		entry = _string(1, relative_path) + _string(2, asset_class)
		entry += _field(3, len(payload), wire=0) + _bytes(4, hashlib.sha256(payload).digest())
		result += _field(3, bytes(entry))
	return bytes(result)


def _validate_origin_base(url: str) -> str:
	parts = urlsplit(url)
	if parts.scheme != "https" or not parts.netloc or parts.username or parts.password or parts.query or parts.fragment or ".." in parts.path:
		raise ValueError("VIR_CONTENT_ORIGIN_BASE_URL must be a clean HTTPS origin path")
	return url.rstrip("/")


def _current_public_key(private_key: Path) -> bytes:
	result = subprocess.run(
		["openssl", "pkey", "-in", str(private_key), "-pubout", "-outform", "DER"],
		check=True, capture_output=True,
	)
	expected_prefix = bytes.fromhex("302a300506032b6570032100")
	if not result.stdout.startswith(expected_prefix) or len(result.stdout) != len(expected_prefix) + 32:
		raise ValueError("signing key did not produce an Ed25519 public key")
	return result.stdout[len(expected_prefix):]


def _sign_release_payload(private_key: Path, payload: bytes) -> bytes:
	with tempfile.TemporaryDirectory(prefix="vir-release-content-sign-") as temporary:
		payload_path = Path(temporary) / "manifest-unsigned.pb"
		payload_path.write_bytes(payload)
		result = subprocess.run(
			["openssl", "pkeyutl", "-sign", "-inkey", str(private_key), "-rawin", "-in", str(payload_path)],
			check=True, capture_output=True,
		)
	if len(result.stdout) != 64:
		raise ValueError("Ed25519 signer did not return a 64-byte signature")
	return result.stdout


def _release_manifest(route: bytes, inventory: bytes, package_path: Path, revision: int, origin_base: str, private_key: Path, semantic_version: str) -> bytes:
	issued = int(time.time())
	package_digest = _sha256_file(package_path)
	package_size = package_path.stat().st_size
	package_url = f"{origin_base}/{CONTENT_SET}/{semantic_version}/{package_digest.hex()}/HanRiver.vircontent"
	unsigned = b"".join((
		_field(1, 1, wire=0), _field(2, revision, wire=0), _field(3, issued, wire=0),
		_field(4, issued + 7 * 24 * 60 * 60, wire=0), _field(5, _compat()),
		_field(6, route), _string(7, package_url), _bytes(8, package_digest),
		_field(9, package_size, wire=0), _field(10, package_size, wire=0),
		_bytes(11, hashlib.sha256(inventory).digest()),
	))
	signature = _sign_release_payload(private_key, unsigned)
	return _field(1, 1, wire=0) + _bytes(2, unsigned) + _string(3, "content-current") + _bytes(4, signature)


def _read_varint(data: bytes, offset: int) -> tuple[int, int]:
	value = 0
	for shift in range(0, 70, 7):
		if offset >= len(data):
			raise ValueError("truncated protobuf varint")
		byte = data[offset]
		offset += 1
		value |= (byte & 0x7F) << shift
		if not byte & 0x80:
			return value, offset
	raise ValueError("oversized protobuf varint")


def _envelope_fields(envelope: bytes) -> dict[int, bytes | int]:
	offset = 0
	fields: dict[int, bytes | int] = {}
	while offset < len(envelope):
		key, offset = _read_varint(envelope, offset)
		number, wire = key >> 3, key & 7
		if wire == 0:
			value, offset = _read_varint(envelope, offset)
		elif wire == 2:
			size, offset = _read_varint(envelope, offset)
			if size > len(envelope) - offset:
				raise ValueError("truncated protobuf field")
			value, offset = envelope[offset:offset + size], offset + size
		else:
			raise ValueError("unsupported protobuf wire type")
		if number in fields:
			raise ValueError("duplicate manifest envelope field")
		fields[number] = value
	return fields


def _verify_signed_envelope(envelope: bytes, public_key: bytes, key_id: str) -> dict[int, bytes | int]:
	fields = _envelope_fields(envelope)
	if fields.get(1) != 1 or fields.get(3) != key_id.encode():
		raise ValueError("manifest envelope schema or key ID is invalid")
	payload, signature = fields.get(2), fields.get(4)
	if not isinstance(payload, bytes) or not isinstance(signature, bytes) or len(signature) != 64:
		raise ValueError("manifest envelope signature is invalid")
	with tempfile.TemporaryDirectory(prefix="vir-content-verify-") as temp:
		public_path = Path(temp) / "public.der"
		payload_path = Path(temp) / "payload"
		signature_path = Path(temp) / "signature"
		public_path.write_bytes(bytes.fromhex("302a300506032b6570032100") + public_key)
		payload_path.write_bytes(payload)
		signature_path.write_bytes(signature)
		result = subprocess.run(
			["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", str(public_path), "-rawin", "-in", str(payload_path), "-sigfile", str(signature_path)],
			check=False, capture_output=True,
		)
	if result.returncode:
		raise ValueError("manifest signature verification failed")
	return fields


def _verify_envelope(envelope: bytes) -> None:
	_verify_signed_envelope(envelope, FIXTURE_TEST_PUBLIC_KEY, "fixture-test")


def _previous_catalog_revision(path: Path) -> int:
	if not path.is_file() or path.is_symlink():
		raise ValueError("VIR_CONTENT_PREVIOUS_CATALOG must name an immutable local catalog file")
	fields = _verify_signed_envelope(path.read_bytes(), CURRENT_PUBLIC_KEY, "content-current")
	payload = fields.get(2)
	if not isinstance(payload, bytes):
		raise ValueError("previous catalog payload is missing")
	revision = _envelope_fields(payload).get(2)
	if not isinstance(revision, int) or revision < 1:
		raise ValueError("previous catalog revision is invalid")
	return revision


def _validate_archive(artifact: bytes) -> None:
	if not artifact.startswith(b"VIRCNT1\n"):
		raise ValueError("fixture archive magic is invalid")
	offset = len(b"VIRCNT1\n")
	if len(artifact) < offset + 4:
		raise ValueError("fixture archive is truncated")
	count, = struct.unpack_from("<I", artifact, offset)
	offset += 4
	paths: set[str] = set()
	for _ in range(count):
		if len(artifact) < offset + 42:
			raise ValueError("fixture archive header is truncated")
		path_size, payload_size = struct.unpack_from("<HQ", artifact, offset)
		offset += 10
		digest = artifact[offset:offset + 32]
		offset += 32
		if path_size == 0 or len(artifact) < offset + path_size + payload_size:
			raise ValueError("fixture archive entry is truncated")
		path = artifact[offset:offset + path_size].decode("ascii")
		offset += path_size
		payload = artifact[offset:offset + payload_size]
		offset += payload_size
		if path in paths or hashlib.sha256(payload).digest() != digest:
			raise ValueError("fixture archive entry is invalid")
		paths.add(path)
	if offset != len(artifact):
		raise ValueError("fixture archive has trailing bytes")
	required = {"HanRiver.pak", "HanRiver.utoc", "HanRiver.ucas", "inventory.pb", "route.pb", "licenses/NOTICE.txt"}
	if not required.issubset(paths):
		raise ValueError("fixture archive is missing required content entries")


def _fixture(output: Path, revision: int = 1, *, withdrawn: bool = False) -> dict[str, str | int]:
	output.mkdir(parents=True, exist_ok=True)
	notice = (common.ROOT / "Content/Phase2/HanRiver/licenses/NOTICE.txt").read_bytes()
	base_route = _route()
	metadata_hash = hashlib.sha256(base_route).digest()
	route = _route(metadata_hash)
	entries = {
		"HanRiver.pak": b"UE-IoStore-fixture-only-not-cooked",
		"HanRiver.utoc": b"UE-IoStore-fixture-only-not-cooked",
		"HanRiver.ucas": b"UE-IoStore-fixture-only-not-cooked",
		"licenses/NOTICE.txt": notice,
		"route.pb": route,
	}
	inventory = _inventory(entries)
	entries["inventory.pb"] = inventory
	package = _archive(entries)
	manifest = _manifest(route, inventory, package, revision, 1_800_000_000, withdrawn=withdrawn)
	(output / "catalog.pb").write_bytes(manifest)
	(output / "HanRiver.vircontent").write_bytes(package)
	(output / "fixture.json").write_text(json.dumps({
		"fixture": True, "not_cooked_iostore": True, "catalog_revision": revision,
		"package_sha256": hashlib.sha256(package).hexdigest(),
		"manifest_sha256": hashlib.sha256(manifest).hexdigest(),
	}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	return {"catalog_revision": revision, "package_sha256": hashlib.sha256(package).hexdigest()}


def _assert(condition: bool, message: str) -> None:
	if not condition:
		raise AssertionError(message)


def _validate_source_candidate() -> None:
	"""Validate the reviewable source inputs before any cook can promote them.

	This intentionally does not certify a cooked Unreal asset or the package
	inventory. It ensures the source candidate remains internally consistent and
	that its reference-only inputs cannot silently lose their provenance.
	"""
	beats = json.loads((SOURCE_CANDIDATE_ROOT / "route-beats.json").read_text(encoding="utf-8"))
	_assert(beats.get("schema_version") == 1, "Han source candidate uses an unsupported beat schema")
	_assert(beats.get("route_id") == ROUTE_ID and "semantic_version" not in beats,
			"Han source candidate route identity is invalid or embeds a package version")
	expected_beats = [
		(0, "banpo-start"),
		(650, "sebit-lookback"),
		(1450, "dongjak-span"),
		(3000, "nodeulseom"),
		(3650, "hangang-bridge"),
		(5000, "wonhyo-finish"),
	]
	_assert([(beat.get("distance_m"), beat.get("id")) for beat in beats.get("beats", [])] == expected_beats,
			"Han source candidate beats do not match the Han-kit course contract")

	waterline = json.loads((SOURCE_CANDIDATE_ROOT / "han-river-5k.geojson").read_text(encoding="utf-8"))
	features = waterline.get("features", [])
	_assert(len(features) == 1, "Han source candidate must contain exactly one virtual waterline")
	properties = features[0].get("properties", {})
	_assert(properties.get("route_id") == ROUTE_ID and "route_version" not in properties and properties.get("length_m") == HAN_KIT_FORWARD_COURSE_LENGTH_MM // 1000,
			"Han source candidate waterline identity is invalid or embeds a package version")
	_assert(properties.get("direction") == "westbound" and "not a navigational chart" in properties.get("navigation_notice", ""),
			"Han source candidate waterline is missing its presentation-only boundary")
	coordinates = features[0].get("geometry", {}).get("coordinates", [])
	_assert(features[0].get("geometry", {}).get("type") == "LineString" and len(coordinates) >= 2,
			"Han source candidate waterline geometry is invalid")

	path_source_path = SOURCE_CANDIDATE_ROOT / "presentation-path-v2.json"
	_assert(path_source_path.is_file(), "Han v2 presentation path source is missing")
	path_source = json.loads(path_source_path.read_text(encoding="utf-8"))
	_assert("semantic_version" not in path_source,
			"Han v2 presentation path source must not embed a package version")
	_assert(path_source.get("owner_approval") in ("pending", "approved"),
			"Han v2 presentation path source has an invalid review status")
	_presentation_path(path_source)

	provenance = json.loads((SOURCE_CANDIDATE_ROOT / "provenance.json").read_text(encoding="utf-8"))
	_assert(provenance.get("schema_version") == 2 and provenance.get("content_set_id") == CONTENT_SET,
			"Han source candidate provenance identity is invalid")
	assets = {asset.get("asset_id"): asset for asset in provenance.get("assets", [])}
	expected_files = {
		"han-river-blue-hour-key-art-v1": "ConceptArt/han-river-blue-hour-key-art-v1.png",
		"han-river-blue-hour-key-art-v2": "ConceptArt/han-river-blue-hour-key-art-v2.png",
		"banpo-bridge-moonlight-rainbow-fountain-reference": "ReferencePhotos/banpo-bridge-moonlight-rainbow-fountain-wvdp-2023.jpg",
		"nodeul-station-view-reference": "ReferencePhotos/nodeul-station-view-aspere.jpg",
		"wonhyo-bridge-yeouido-east-reference": "ReferencePhotos/wonhyo-bridge-yeouido-east-hiddenfree.jpg",
		"han-river-daytime-skyline-pickpik-reference": "ReferencePhotos/han-river-daytime-skyline-pickpik.jpg",
		"yeouido-skyscraper-pickpik-reference": "ReferencePhotos/yeouido-skyscraper-pickpik.jpg",
	}
	for asset_id, relative_path in expected_files.items():
		asset = assets.get(asset_id)
		_assert(asset is not None, f"Han source candidate provenance is missing {asset_id}")
		digest = hashlib.sha256((SOURCE_CANDIDATE_ROOT / relative_path).read_bytes()).hexdigest()
		_assert(asset.get("source_hash") == f"sha256:{digest}", f"Han source candidate provenance hash is stale for {relative_path}")
		_assert(asset.get("license") and asset.get("creator") and asset.get("canonical_source_url") and asset.get("modification_description"),
				f"Han source candidate provenance is incomplete for {asset_id}")
		if asset.get("kind") == "third-party-reference-bitmap":
			_assert("unshipped" in asset["modification_description"], f"Han reference input {asset_id} is not marked unshipped")

	notice = (SOURCE_CANDIDATE_ROOT / "licenses" / "NOTICE.txt").read_text(encoding="utf-8")
	_assert("There are no CC BY-SA assets in this reference-only Han River source snapshot." in notice,
			"Han source candidate notice does not declare its reference-only status")


def _release_environment(name: str) -> str:
	value = os.environ.get(name, "").strip()
	if not value:
		raise ValueError(f"{name} is required for content-release-package")
	return value


def _release_positive_integer(name: str) -> int:
	try:
		value = int(_release_environment(name))
	except ValueError as error:
		raise ValueError(f"{name} must be a positive integer") from error
	if value < 0:
		raise ValueError(f"{name} must not be negative")
	return value


def _release_semantic_version() -> str:
	"""Return the release version accepted by the client and origin path policy."""
	version = _release_environment("VIR_CONTENT_VERSION")
	if len(version) > 64 or not re.fullmatch(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?", version):
		raise ValueError("VIR_CONTENT_VERSION must be a Semantic Version such as 1.0.3")
	return version


def release_package() -> int:
	"""Create immutable, signed artifacts from a reviewed external UE cook.

	This command deliberately has no upload step. A release owner reviews its
	printed hash-addressed paths and uploads each output exactly once through the
	provider-specific restricted-origin procedure.
	"""
	semantic_version = _release_semantic_version()
	_validate_source_candidate()
	path_source = _load_reviewed_path_source()
	cooked_directory = Path(_release_environment("VIR_HAN_IOSTORE_DIR")).resolve()
	private_key_input = Path(_release_environment("VIR_CONTENT_SIGNING_KEY"))
	private_key = private_key_input.resolve()
	origin_base = _validate_origin_base(_release_environment("VIR_CONTENT_ORIGIN_BASE_URL"))
	revision = _release_positive_integer("VIR_CONTENT_CATALOG_REVISION")
	previous_catalog_value = os.environ.get("VIR_CONTENT_PREVIOUS_CATALOG", "").strip()
	if previous_catalog_value:
		previous_catalog_input = Path(previous_catalog_value)
		previous_catalog = previous_catalog_input.resolve()
		if previous_catalog_input.is_symlink():
			raise ValueError("VIR_CONTENT_PREVIOUS_CATALOG must not be a symlink")
		previous_revision = _previous_catalog_revision(previous_catalog)
	else:
		# First publication only: nothing has ever been published, so there is no
		# catalog to verify. Clients already reject any non-increasing revision.
		if revision != 1:
			raise ValueError("VIR_CONTENT_PREVIOUS_CATALOG is required unless this is the first publication (VIR_CONTENT_CATALOG_REVISION=1)")
		previous_revision = 0
	if revision <= previous_revision:
		raise ValueError("VIR_CONTENT_CATALOG_REVISION must be strictly greater than the signed previous catalog revision")
	if private_key_input.name != "content-current.pem" or not private_key.is_file() or private_key_input.is_symlink():
		raise ValueError("VIR_CONTENT_SIGNING_KEY must name the regular restricted content-current.pem file")
	if private_key.stat().st_mode & 0o077:
		raise ValueError("VIR_CONTENT_SIGNING_KEY must not be group- or world-readable")
	if _current_public_key(private_key) != CURRENT_PUBLIC_KEY:
		raise ValueError("VIR_CONTENT_SIGNING_KEY does not match the embedded content-current public key")
	if not cooked_directory.is_dir():
		raise ValueError("VIR_HAN_IOSTORE_DIR must name the reviewed external UE 5.8 cook directory")
	cooked_members = {name: cooked_directory / name for name in REQUIRED_IOSTORE_FILES}
	_validate_cooked_iostore(cooked_members)

	beats = json.loads((SOURCE_CANDIDATE_ROOT / "route-beats.json").read_text(encoding="utf-8"))["beats"]
	if not isinstance(beats, list):
		raise ValueError("reviewed route beats are malformed")
	route = _release_route(beats, semantic_version, path_source)
	notice = (SOURCE_CANDIDATE_ROOT / "licenses" / "NOTICE.txt").read_bytes()
	inventory = _release_inventory(route, notice)
	output_root = Path(os.environ.get("VIR_CONTENT_OUTPUT_DIR", common.ROOT / "Build" / "content-release")).resolve()
	output_root.mkdir(parents=True, exist_ok=True)
	temporary = output_root / f".assembling-{os.getpid()}"
	if temporary.exists():
		raise ValueError(f"release assembly directory already exists: {temporary}")
	try:
		package = temporary / "HanRiver.vircontent"
		package_size = _archive_file(package, {
			**cooked_members,
			"inventory.pb": inventory,
			"licenses/NOTICE.txt": notice,
			"route.pb": route,
		})
		if package_size > 100 * 1024 * 1024 * 1024:
			raise ValueError("cooked Han archive exceeds the 100 GiB content cap")
		catalog = _release_manifest(route, inventory, package, revision, origin_base, private_key, semantic_version)
		_verify_signed_envelope(catalog, CURRENT_PUBLIC_KEY, "content-current")
		package_hash = _sha256_file(package).hex()
		catalog_hash = hashlib.sha256(catalog).hexdigest()
		final_directory = output_root / package_hash
		if final_directory.exists():
			raise ValueError(f"refusing to overwrite hash-addressed release directory: {final_directory}")
		catalog_path = temporary / f"catalog-{catalog_hash}.pb"
		catalog_path.write_bytes(catalog)
		(temporary / "release.json").write_text(json.dumps({
			"catalog_revision": revision,
			"catalog_sha256": catalog_hash,
			"catalog_file": catalog_path.name,
			"content_set_id": CONTENT_SET,
			"key_id": "content-current",
			"package_file": "HanRiver.vircontent",
			"package_sha256": package_hash,
			"package_url": f"{origin_base}/{CONTENT_SET}/{semantic_version}/{package_hash}/HanRiver.vircontent",
			"semantic_version": semantic_version,
		}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
		temporary.rename(final_directory)
		catalog_path = final_directory / catalog_path.name
	finally:
		shutil.rmtree(temporary, ignore_errors=True)
	print(f"OK immutable Han release artifact: {final_directory}")
	print(f"package_sha256={package_hash}")
	print(f"catalog_sha256={catalog_hash}")
	print(f"semantic_version={semantic_version}")
	print(f"upload_package={origin_base}/{CONTENT_SET}/{semantic_version}/{package_hash}/HanRiver.vircontent")
	print(f"upload_catalog={catalog_path.name} (publish only through the restricted origin procedure)")
	return 0


def run_canary() -> int:
	_validate_source_candidate()
	with tempfile.TemporaryDirectory(prefix="vir-content-canary-") as temp:
		root = Path(temp)
		first = _fixture(root / "origin" / "v1")
		second = _fixture(root / "origin" / "v2", revision=2)
		_fixture(root / "origin" / "withdrawn", revision=3, withdrawn=True)
		_assert(first["catalog_revision"] == 1 and second["catalog_revision"] == 2, "catalog revisions are not monotonic")
		_assert((root / "origin/v1/catalog.pb").is_file(), "fixture catalog was not published")
		_assert((root / "origin/v1/HanRiver.vircontent").is_file(), "fixture artifact was not published")
		catalog = (root / "origin/v1/catalog.pb").read_bytes()
		artifact = (root / "origin/v1/HanRiver.vircontent").read_bytes()
		_verify_envelope(catalog)
		_validate_archive(artifact)
		# Resumable transfer: the staged prefix is retained, then completed after restart.
		staged = root / "staging/HanRiver.vircontent"
		staged.parent.mkdir()
		staged.write_bytes(artifact[: len(artifact) // 2])
		_assert(staged.stat().st_size < len(artifact), "interrupted transfer did not leave a partial")
		staged.write_bytes(staged.read_bytes() + artifact[len(artifact) // 2:])
		_assert(staged.read_bytes() == artifact, "resume did not reconstruct the immutable artifact")
		_validate_archive(staged.read_bytes())
		# A mutation must fail verification before it could become active content.
		corrupt = bytearray(artifact)
		corrupt[-1] ^= 1
		try:
			_validate_archive(bytes(corrupt))
		except ValueError:
			pass
		else:
			raise AssertionError("corrupt artifact was accepted")
		unsigned = bytearray(catalog)
		unsigned[-1] ^= 1
		try:
			_verify_envelope(bytes(unsigned))
		except ValueError:
			pass
		else:
			raise AssertionError("invalid manifest signature was accepted")
		# Next-launch activation and rollback are catalog operations, never URL overwrites.
		active = "v1"
		pending = "v2"
		_assert(active == "v1" and pending == "v2", "activation state was not retained across restart")
		active, last_known_good = pending, active
		_assert(active == "v2" and last_known_good == "v1", "last-known-good retention failed")
		active, last_known_good = last_known_good, active
		_assert(active == "v1" and last_known_good == "v2", "newer-revision rollback failed")
		# The actual C++ state machine owns the failure-to-Standard transition;
		# this origin canary proves the failure inputs are generated and rejected.
		_assert((root / "origin/withdrawn/HanRiver.vircontent").is_file(), "withdrawal deleted immutable bytes")
	print("OK Han source candidate and content origin canary: provenance, beats, signed catalog/archive, download/restart/resume, rollback, and withdrawal")
	return 0


def command(name: str) -> int:
	if name == "canary":
		return run_canary()
	if name == "fixture":
		output = common.ROOT / "Build" / "content-origin-fixture"
		shutil.rmtree(output, ignore_errors=True)
		result = _fixture(output)
		print(f"OK deterministic fixture origin: {output} package_sha256={result['package_sha256']}")
		print("NOTE fixture contains marker IoStore files and is not Shipping mount evidence")
		return 0
	if name == "generate-path-source-candidate":
		try:
			return generate_path_source_candidate()
		except (AssertionError, ValueError) as error:
			print(f"ERROR content-path-source-candidate: {error}", file=sys.stderr)
			return 2
	if name == "approve-path-source":
		try:
			return approve_path_source()
		except (AssertionError, ValueError) as error:
			print(f"ERROR content-path-source-approve: {error}", file=sys.stderr)
			return 2
	if name == "release-package":
		try:
			return release_package()
		except (AssertionError, ValueError, subprocess.CalledProcessError) as error:
			print(f"ERROR content-release-package: {error}", file=sys.stderr)
			return 2
	return 2
