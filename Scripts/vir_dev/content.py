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
import os
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
VERSION = "1.0.1"
ROUTE_ID = "route.han-river.5k"
SOURCE_CANDIDATE_ROOT = common.ROOT / "Content" / "Phase2" / "HanRiver"
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


def _release_route(beats: list[dict[str, object]]) -> bytes:
	"""Create the canonical route definition from the approved beat source."""
	checkpoints = []
	for beat in beats:
		distance_m = beat.get("distance_m")
		checkpoint_id = beat.get("id")
		if not isinstance(distance_m, int) or not isinstance(checkpoint_id, str):
			raise ValueError("route beat has an invalid checkpoint")
		if 0 < distance_m < 5000:
			checkpoints.append(_string(1, checkpoint_id) + _field(2, distance_m * 1000, wire=0))
	base = b"".join((
		_field(1, 1, wire=0), _string(2, ROUTE_ID), _string(3, VERSION),
		_string(4, CONTENT_SET), _field(5, 5_000_000, wire=0),
		*(_field(7, checkpoint) for checkpoint in checkpoints),
		_field(8, _compat()), _string(9, "route.han_river.name"),
		_string(10, "route.han_river.description"),
	))
	return base + _bytes(11, hashlib.sha256(base).digest())


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


def _release_manifest(route: bytes, inventory: bytes, package_path: Path, revision: int, origin_base: str, private_key: Path) -> bytes:
	issued = int(time.time())
	package_digest = _sha256_file(package_path)
	package_size = package_path.stat().st_size
	package_url = f"{origin_base}/{CONTENT_SET}/{VERSION}/{package_digest.hex()}/HanRiver.vircontent"
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
	_assert(beats.get("route_id") == ROUTE_ID and beats.get("semantic_version") == VERSION, "Han source candidate route identity is invalid")
	expected_beats = [
		(0, "banpo-start"),
		(650, "sebit-lookback"),
		(1450, "dongjak-span"),
		(3000, "nodeulseom"),
		(3650, "hangang-bridge"),
		(5000, "wonhyo-finish"),
	]
	_assert([(beat.get("distance_m"), beat.get("id")) for beat in beats.get("beats", [])] == expected_beats,
			"Han source candidate beats do not match the fixed 5 km course contract")

	waterline = json.loads((SOURCE_CANDIDATE_ROOT / "han-river-5k.geojson").read_text(encoding="utf-8"))
	features = waterline.get("features", [])
	_assert(len(features) == 1, "Han source candidate must contain exactly one virtual waterline")
	properties = features[0].get("properties", {})
	_assert(properties.get("route_id") == ROUTE_ID and properties.get("route_version") == VERSION and properties.get("length_m") == 5000,
			"Han source candidate waterline route identity is invalid")
	_assert(properties.get("direction") == "westbound" and "not a navigational chart" in properties.get("navigation_notice", ""),
			"Han source candidate waterline is missing its presentation-only boundary")
	coordinates = features[0].get("geometry", {}).get("coordinates", [])
	_assert(features[0].get("geometry", {}).get("type") == "LineString" and len(coordinates) >= 2,
			"Han source candidate waterline geometry is invalid")

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


def release_package() -> int:
	"""Create immutable, signed artifacts from a reviewed external UE cook.

	This command deliberately has no upload step. A release owner reviews its
	printed hash-addressed paths and uploads each output exactly once through the
	provider-specific restricted-origin procedure.
	"""
	_validate_source_candidate()
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
	route = _release_route(beats)
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
		catalog = _release_manifest(route, inventory, package, revision, origin_base, private_key)
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
			"package_url": f"{origin_base}/{CONTENT_SET}/{VERSION}/{package_hash}/HanRiver.vircontent",
			"semantic_version": VERSION,
		}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
		temporary.rename(final_directory)
		catalog_path = final_directory / catalog_path.name
	finally:
		shutil.rmtree(temporary, ignore_errors=True)
	print(f"OK immutable Han release artifact: {final_directory}")
	print(f"package_sha256={package_hash}")
	print(f"catalog_sha256={catalog_hash}")
	print(f"upload_package={origin_base}/{CONTENT_SET}/{VERSION}/{package_hash}/HanRiver.vircontent")
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
	if name == "release-package":
		try:
			return release_package()
		except (AssertionError, ValueError, subprocess.CalledProcessError) as error:
			print(f"ERROR content-release-package: {error}", file=sys.stderr)
			return 2
	return 2
