#!/usr/bin/env python3
"""Behavioral tests for the restricted Han River release packager."""

from __future__ import annotations

import json
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

	def tearDown(self) -> None:
		self.temp.cleanup()

	def write_previous_catalog(self, revision: int) -> Path:
		route = content._release_route(json.loads((content.SOURCE_CANDIDATE_ROOT / "route-beats.json").read_text(encoding="utf-8"))["beats"])
		notice = (content.SOURCE_CANDIDATE_ROOT / "licenses" / "NOTICE.txt").read_bytes()
		inventory = content._release_inventory(route, notice)
		package = self.root / "previous.vircontent"
		package.write_bytes(b"previous-immutable-package")
		catalog = self.root / "previous-catalog.pb"
		catalog.write_bytes(content._release_manifest(route, inventory, package, revision, "https://origin.example.invalid/content", self.signing_key))
		return catalog

	def environment(self, previous_catalog: Path, revision: int) -> dict[str, str]:
		return {
			"VIR_HAN_IOSTORE_DIR": str(self.cooked),
			"VIR_CONTENT_SIGNING_KEY": str(self.signing_key),
			"VIR_CONTENT_ORIGIN_BASE_URL": "https://origin.example.invalid/content",
			"VIR_CONTENT_CATALOG_REVISION": str(revision),
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


if __name__ == "__main__":
	unittest.main()
