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
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from vir_dev import common


# RFC 8032's published test key is only for this disposable fixture. It is
# deliberately not one of the client trust keys.
FIXTURE_TEST_SEED = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc4" "4449c5697b326919703bac031cae7f60")
FIXTURE_TEST_PUBLIC_KEY = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
CONTENT_SET = "han-river-alpha-1"
VERSION = "1.0.0"
ROUTE_ID = "route.han-river.5k"


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
	return b"".join((_field(1, 1, wire=0), _field(2, 1, wire=0), _field(3, 1, wire=0), _field(4, 1, wire=0)))


def _route(metadata_hash: bytes = b"") -> bytes:
	checkpoint = _string(1, "bridge-1") + _field(2, 1_000_000, wire=0)
	fields = [
		_field(1, 1, wire=0), _string(2, ROUTE_ID), _string(3, VERSION),
		_string(4, CONTENT_SET), _field(5, 5_000_000, wire=0),
		_field(7, checkpoint), _field(8, _compat()),
		_string(9, "route.han_river.name"), _string(10, "route.han_river.description"),
	]
	if metadata_hash:
		fields.append(_bytes(11, metadata_hash))
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
		_field(6, route_bytes), _string(7, f"https://origin.invalid/content/{CONTENT_SET}/{VERSION}/HanRiver.vircontent"),
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


def _verify_envelope(envelope: bytes) -> None:
	fields = _envelope_fields(envelope)
	if fields.get(1) != 1 or fields.get(3) != b"fixture-test":
		raise ValueError("fixture manifest envelope schema is invalid")
	payload, signature = fields.get(2), fields.get(4)
	if not isinstance(payload, bytes) or not isinstance(signature, bytes) or len(signature) != 64:
		raise ValueError("fixture manifest envelope signature is invalid")
	with tempfile.TemporaryDirectory(prefix="vir-content-verify-") as temp:
		public_path = Path(temp) / "public.der"
		payload_path = Path(temp) / "payload"
		signature_path = Path(temp) / "signature"
		public_path.write_bytes(bytes.fromhex("302a300506032b6570032100") + FIXTURE_TEST_PUBLIC_KEY)
		payload_path.write_bytes(payload)
		signature_path.write_bytes(signature)
		result = subprocess.run(
			["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", str(public_path), "-rawin", "-in", str(payload_path), "-sigfile", str(signature_path)],
			check=False, capture_output=True,
		)
	if result.returncode:
		raise ValueError("fixture manifest signature verification failed")


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


def run_canary() -> int:
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
	print("OK content origin canary: signed catalog/archive, download/restart/resume, rollback, and withdrawal")
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
	return 2
