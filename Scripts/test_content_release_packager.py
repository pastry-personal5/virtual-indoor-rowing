#!/usr/bin/env python3
"""Behavioral tests for the restricted Han River release packager."""

from __future__ import annotations

import json
import math
import os
import sys
import tempfile
import unittest
from base64 import b64encode
from pathlib import Path
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parent))

from vir_dev import content  # noqa: E402


PRIVATE_DER_PREFIX = bytes.fromhex("302e020100300506032b657004220420")


def pem_for(seed: bytes) -> bytes:
	encoded = b64encode(PRIVATE_DER_PREFIX + seed).decode()
	return f"-----BEGIN PRIVATE KEY-----\n{encoded}\n-----END PRIVATE KEY-----\n".encode()


class ContentReleasePackagerTests(unittest.TestCase):
	def setUp(self) -> None:
		# A release owner's shell may export the real VIR_* release variables; the
		# tests must not pick them up.
		isolated = patch.dict(os.environ)
		isolated.start()
		self.addCleanup(isolated.stop)
		for name in [name for name in os.environ if name.startswith("VIR_CONTENT_") or name == "VIR_HAN_IOSTORE_DIR"]:
			del os.environ[name]
		self.temp = tempfile.TemporaryDirectory()
		self.root = Path(self.temp.name)
		self.cooked = self.root / "cooked"
		self.cooked.mkdir()
		for name in content.REQUIRED_IOSTORE_FILES:
			payload = f"reviewed-cook-{name}".encode()
			if name == "HanRiver.utoc":
				payload = content.IOSTORE_TOC_MAGIC + payload
			(self.cooked / name).write_bytes(payload)
		self.signing_key = self.root / "content-current.pem"
		self.signing_key.write_bytes(pem_for(content.FIXTURE_TEST_SEED))
		self.signing_key.chmod(0o600)
		path_source = patch.object(content, "_load_reviewed_path_source", return_value=content._fixture_path_source())
		path_source.start()
		self.addCleanup(path_source.stop)

	def tearDown(self) -> None:
		self.temp.cleanup()

	def write_previous_catalog(self, revision: int) -> Path:
		version = "2.0.2"
		route = content._release_route(json.loads((content.SOURCE_CANDIDATE_ROOT / "route-beats.json").read_text(encoding="utf-8"))["beats"], version)
		notice = (content.SOURCE_CANDIDATE_ROOT / "licenses" / "NOTICE.txt").read_bytes()
		inventory = content._release_inventory(route, notice)
		package = self.root / "previous.vircontent"
		package.write_bytes(b"previous-immutable-package")
		catalog = self.root / "previous-catalog.pb"
		catalog.write_bytes(content._release_manifest(route, inventory, package, revision, "https://origin.example.invalid/content", self.signing_key, version))
		return catalog

	def environment(self, previous_catalog: Path, revision: int) -> dict[str, str]:
		return {
			"VIR_HAN_IOSTORE_DIR": str(self.cooked),
			"VIR_CONTENT_SIGNING_KEY": str(self.signing_key),
			"VIR_CONTENT_ORIGIN_BASE_URL": "https://origin.example.invalid/content",
			"VIR_CONTENT_CATALOG_REVISION": str(revision),
			"VIR_CONTENT_VERSION": "2.0.2",
			"VIR_CONTENT_PREVIOUS_CATALOG": str(previous_catalog),
			"VIR_CONTENT_OUTPUT_DIR": str(self.root / "release"),
		}

	def test_packages_real_named_iostore_members_and_emits_signed_hash_addressed_catalog(self) -> None:
		with patch.object(content, "CURRENT_PUBLIC_KEY", content.FIXTURE_TEST_PUBLIC_KEY):
			previous_catalog = self.write_previous_catalog(7)
			with patch.dict(os.environ, self.environment(previous_catalog, 8), clear=False):
				self.assertEqual(content.release_package(), 0)

		release_root = self.root / "release"
		directories = [path for path in release_root.iterdir() if path.is_dir()]
		self.assertEqual(len(directories), 1)
		release = directories[0]
		package = release / "HanRiver.vircontent"
		self.assertTrue(package.is_file())
		self.assertEqual(release.name, content._sha256_file(package).hex())
		content._validate_archive(package.read_bytes())
		catalogs = list(release.glob("catalog-*.pb"))
		self.assertEqual(len(catalogs), 1)
		fields = content._verify_signed_envelope(catalogs[0].read_bytes(), content.FIXTURE_TEST_PUBLIC_KEY, "content-current")
		self.assertIsInstance(fields[2], bytes)
		unsigned = content._envelope_fields(fields[2])
		self.assertEqual(unsigned[2], 8)
		self.assertEqual(unsigned[4] - unsigned[3], 7 * 24 * 60 * 60)
		metadata = json.loads((release / "release.json").read_text(encoding="utf-8"))
		self.assertEqual(metadata["catalog_file"], catalogs[0].name)
		self.assertEqual(metadata["package_sha256"], release.name)
		self.assertIn(f"/{release.name}/HanRiver.vircontent", metadata["package_url"])
		self.assertEqual(metadata["semantic_version"], "2.0.2")

	def test_uses_environment_release_version_in_signed_route_and_package_url(self) -> None:
		with patch.object(content, "CURRENT_PUBLIC_KEY", content.FIXTURE_TEST_PUBLIC_KEY):
			previous_catalog = self.write_previous_catalog(7)
			environment = self.environment(previous_catalog, 8)
			environment["VIR_CONTENT_VERSION"] = "2.0.3"
			with patch.dict(os.environ, environment, clear=False), \
				patch.object(content, "_validate_source_candidate") as validate_source:
				self.assertEqual(content.release_package(), 0)

		validate_source.assert_called_once_with()
		release = next((self.root / "release").iterdir())
		metadata = json.loads((release / "release.json").read_text(encoding="utf-8"))
		self.assertEqual(metadata["semantic_version"], "2.0.3")
		self.assertIn("/han-river-alpha-1/2.0.3/", metadata["package_url"])
		catalog = next(release.glob("catalog-*.pb"))
		fields = content._verify_signed_envelope(catalog.read_bytes(), content.FIXTURE_TEST_PUBLIC_KEY, "content-current")
		unsigned = content._envelope_fields(fields[2])
		# Route definitions contain repeated checkpoint fields, so inspect the
		# canonical semantic-version field rather than use the envelope-only
		# duplicate-field parser.
		self.assertIn(content._string(3, "2.0.3"), unsigned[6])

	def test_refuses_missing_or_invalid_release_version(self) -> None:
		with patch.object(content, "CURRENT_PUBLIC_KEY", content.FIXTURE_TEST_PUBLIC_KEY):
			previous_catalog = self.write_previous_catalog(7)
			environment = self.environment(previous_catalog, 8)
			del environment["VIR_CONTENT_VERSION"]
			with patch.dict(os.environ, environment, clear=False):
				with self.assertRaisesRegex(ValueError, "VIR_CONTENT_VERSION is required"):
					content.release_package()
			environment["VIR_CONTENT_VERSION"] = "1.03"
			with patch.dict(os.environ, environment, clear=False):
				with self.assertRaisesRegex(ValueError, "Semantic Version"):
					content.release_package()

	def test_first_publication_needs_no_previous_catalog_but_only_at_revision_one(self) -> None:
		with patch.object(content, "CURRENT_PUBLIC_KEY", content.FIXTURE_TEST_PUBLIC_KEY):
			environment = self.environment(Path("unused"), 2)
			del environment["VIR_CONTENT_PREVIOUS_CATALOG"]
			with patch.dict(os.environ, environment, clear=False):
				with self.assertRaisesRegex(ValueError, "first publication"):
					content.release_package()
			environment["VIR_CONTENT_CATALOG_REVISION"] = "1"
			with patch.dict(os.environ, environment, clear=False):
				self.assertEqual(content.release_package(), 0)

	def test_refuses_a_non_increasing_revision_and_wrong_signing_key(self) -> None:
		with patch.object(content, "CURRENT_PUBLIC_KEY", content.FIXTURE_TEST_PUBLIC_KEY):
			previous_catalog = self.write_previous_catalog(7)
			with patch.dict(os.environ, self.environment(previous_catalog, 7), clear=False):
				with self.assertRaisesRegex(ValueError, "strictly greater"):
					content.release_package()
			other_key = self.root / "other" / "content-current.pem"
			other_key.parent.mkdir()
			other_key.write_bytes(pem_for(bytes.fromhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb")))
			other_key.chmod(0o600)
			environment = self.environment(previous_catalog, 8)
			environment["VIR_CONTENT_SIGNING_KEY"] = str(other_key)
			with patch.dict(os.environ, environment, clear=False):
				with self.assertRaisesRegex(ValueError, "does not match"):
					content.release_package()

	def test_refuses_non_iostore_toc_and_leaves_no_partial_release_after_a_signing_failure(self) -> None:
		with patch.object(content, "CURRENT_PUBLIC_KEY", content.FIXTURE_TEST_PUBLIC_KEY):
			previous_catalog = self.write_previous_catalog(7)
			(self.cooked / "HanRiver.utoc").write_bytes(b"fixture-marker-not-an-iostore-toc")
			with patch.dict(os.environ, self.environment(previous_catalog, 8), clear=False):
				with self.assertRaisesRegex(ValueError, "TOC header"):
					content.release_package()

			(self.cooked / "HanRiver.utoc").write_bytes(content.IOSTORE_TOC_MAGIC + b"reviewed-utoc")
			with patch.dict(os.environ, self.environment(previous_catalog, 8), clear=False), \
				patch.object(content, "_sign_release_payload", side_effect=ValueError("signer unavailable")):
				with self.assertRaisesRegex(ValueError, "signer unavailable"):
					content.release_package()
			release_root = self.root / "release"
			self.assertTrue(release_root.is_dir())
			self.assertEqual(list(release_root.iterdir()), [])


class ContentPathSourceCandidateTests(unittest.TestCase):
	def test_repository_source_is_a_structurally_valid_owner_approved_source(self) -> None:
		path = content.SOURCE_CANDIDATE_ROOT / "presentation-path-v2.json"
		self.assertTrue(path.is_file())
		self.assertEqual(path.read_bytes(), content._owner_approved_path_source_bytes())
		source = json.loads(path.read_text(encoding="utf-8"))
		self.assertEqual(source["owner_approval"], "approved")
		self.assertEqual(source["review_status"], "owner-approved-map-identity")
		self.assertTrue(content._presentation_path(source))

	def test_path_encoder_omits_proto3_default_scalars(self) -> None:
		path = content._presentation_path(content._candidate_path_source())
		# The start control point, first lookup entry, and zero yaw are all
		# present structurally but must not serialize scalar zero defaults.
		self.assertNotIn(b"\x08\x00", path)
		self.assertNotIn(b"\x10\x00", path)
		self.assertNotIn(b"\x18\x00", path)

	def test_han_candidate_is_a_westbound_twenty_one_point_authored_gate_path_not_the_fixture_line(self) -> None:
		source = content._candidate_path_source()
		points = source["control_points"]
		self.assertEqual([point["id"] for point in points], [f"han-route-gate-{index:02d}" for index in range(21)])
		self.assertEqual(points[0]["position_mm"], [0, 0, 0])
		self.assertEqual(
			source["route_local_origin_mm"],
			list(content.HAN_RIVER_ROUTE_LOCAL_ORIGIN_MM),
		)
		self.assertEqual(source["route_local_yaw_microradians"], 3_141_593)
		self.assertGreater(max(abs(point["position_mm"][1]) for point in points), 900_000)
		yaw = source["route_local_yaw_microradians"] / 1_000_000
		start_position = points[0]["position_mm"]
		final_position = points[-1]["position_mm"]
		origin = source["route_local_origin_mm"]
		actual_start_unreal_cm = (
			(math.cos(yaw) * start_position[0] - math.sin(yaw) * start_position[1] + origin[0]) / 10,
			(math.sin(yaw) * start_position[0] + math.cos(yaw) * start_position[1] + origin[1]) / 10,
			(start_position[2] + origin[2]) / 10,
		)
		actual_final_unreal_cm = (
			(math.cos(yaw) * final_position[0] - math.sin(yaw) * final_position[1] + origin[0]) / 10,
			(math.sin(yaw) * final_position[0] + math.cos(yaw) * final_position[1] + origin[1]) / 10,
			(final_position[2] + origin[2]) / 10,
		)
		for actual, target in zip(actual_start_unreal_cm, content.HAN_RIVER_START_UNREAL_CM):
			self.assertAlmostEqual(actual, target, delta=0.05)
		self.assertEqual(points[19]["position_mm"], [4_278_481, 904_259, 0])
		self.assertEqual(points[20]["position_mm"], [4_674_076, 1_141_462, 0])
		self.assertLess(actual_final_unreal_cm[0], actual_start_unreal_cm[0] - 400_000)
		lookup = source["arc_length_lookup"]
		self.assertEqual((lookup[0]["route_distance_mm"], lookup[-1]["route_distance_mm"]), (0, content.HAN_KIT_FORWARD_COURSE_LENGTH_MM))
		maximum_curve_x = max(
			content._hermite_point(
				tuple(points[index]["position_mm"][:2]),
				tuple(points[index]["leave_tangent_mm"][:2]),
				tuple(points[index + 1]["position_mm"][:2]),
				tuple(points[index + 1]["arrive_tangent_mm"][:2]),
				step / 512,
			)[0]
			for index in range(len(points) - 1)
			for step in range(513)
		)
		self.assertGreater(maximum_curve_x, 4_000_000)

	def test_candidate_generator_refuses_to_overwrite_different_review_data(self) -> None:
		with tempfile.TemporaryDirectory() as temporary:
			path = Path(temporary) / "presentation-path-v2.json"
			self.assertEqual(content.generate_path_source_candidate(path), 0)
			self.assertEqual(content.generate_path_source_candidate(path), 0)
			path.write_text("{}\n", encoding="utf-8")
			with self.assertRaisesRegex(ValueError, "differs from its deterministic candidate generator"):
				content.generate_path_source_candidate(path)

	def test_approval_requires_the_unmodified_generated_candidate(self) -> None:
		with tempfile.TemporaryDirectory() as temporary:
			path = Path(temporary) / "presentation-path-v2.json"
			self.assertEqual(content.generate_path_source_candidate(path), 0)
			self.assertEqual(content.approve_path_source(path), 0)
			self.assertEqual(path.read_bytes(), content._owner_approved_path_source_bytes())
			self.assertEqual(content.generate_path_source_candidate(path), 0)

	def test_approval_refuses_modified_candidates(self) -> None:
		with tempfile.TemporaryDirectory() as temporary:
			path = Path(temporary) / "presentation-path-v2.json"
			path.write_text("{}\n", encoding="utf-8")
			with self.assertRaisesRegex(ValueError, "cannot be approved"):
				content.approve_path_source(path)


if __name__ == "__main__":
	unittest.main()
