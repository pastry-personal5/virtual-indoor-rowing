cmake_minimum_required(VERSION 3.28)

project(VirtualRowing LANGUAGES CXX)

include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Phase 0 Milestone 4 Spike B (LocalData durability journal) links the
# macOS SDK's system SQLite rather than vendoring an amalgamation — the
# project targets Apple-silicon macOS only per ADR-0001/ADR-0006, and this
# matches the "stock toolchain" approach already used for Unreal/Xcode.
find_path(SQLite3_INCLUDE_DIR NAMES sqlite3.h)
find_library(SQLite3_LIBRARY NAMES sqlite3)
if(NOT SQLite3_INCLUDE_DIR OR NOT SQLite3_LIBRARY)
	message(FATAL_ERROR "System SQLite3 (sqlite3.h/libsqlite3) was not found; it ships with the macOS SDK on the ADR-0001 toolchain baseline.")
endif()
find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
find_library(ZSTD_LIBRARY NAMES zstd)
find_file(ZSTD_STATIC_LIBRARY NAMES libzstd.a PATHS /opt/homebrew/lib /usr/local/lib)
if(ZSTD_STATIC_LIBRARY)
	set(ZSTD_LIBRARY "${ZSTD_STATIC_LIBRARY}")
endif()
if(NOT ZSTD_INCLUDE_DIR OR NOT ZSTD_LIBRARY)
	message(FATAL_ERROR "Zstandard (zstd.h/libzstd) is required for the canonical SessionObject container.")
endif()

# Phase 1 Milestone 1 (docs/phase-1/01-milestone-1-rowingcore-domain-contract.md)
# pins the Protobuf toolchain the same way cmake/ninja/git-lfs are pinned in
# Config/BuildVersions.json — an exact version checked by `make doctor`, not a
# floating dependency. Install with `brew install protobuf`.
option(VIR_STATIC_PROTOBUF "Build protobuf and abseil statically from the sources pinned in Config/BuildVersions.json (used by the app-bound Build/native-app tree)" OFF)
option(VIR_APP_LIBS_ONLY "Build only the engine-independent libraries the Unreal app links, merged into one archive (Phase 1 Milestone 5)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(VIR_APP_LIBS_ONLY)
	# The archive is linked into an Unreal module image alongside engine-bundled
	# protobuf/abseil (different versions). Hidden visibility keeps our copies
	# out of the module's export table so dyld cannot coalesce weak/inline
	# symbols across images.
	set(CMAKE_CXX_VISIBILITY_PRESET hidden)
	set(CMAKE_OBJCXX_VISIBILITY_PRESET hidden)
	set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

if(VIR_STATIC_PROTOBUF)
	# Phase 1 Milestone 5: the Unreal app must not depend on Homebrew dylibs, so
	# the app-bound tree builds the pinned protobuf runtime and its abseil
	# dependency as static libraries. Sources are hash-verified downloads;
	# nothing is vendored. protoc stays the pinned Homebrew build (one version).
	include(FetchContent)
	file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Config/BuildVersions.json" VIR_BUILD_VERSIONS_JSON)
	string(JSON VIR_ABSL_URL GET "${VIR_BUILD_VERSIONS_JSON}" static_dependencies abseil url)
	string(JSON VIR_ABSL_SHA256 GET "${VIR_BUILD_VERSIONS_JSON}" static_dependencies abseil sha256)
	string(JSON VIR_PROTOBUF_URL GET "${VIR_BUILD_VERSIONS_JSON}" static_dependencies protobuf url)
	string(JSON VIR_PROTOBUF_SHA256 GET "${VIR_BUILD_VERSIONS_JSON}" static_dependencies protobuf sha256)
	string(JSON VIR_PROTOBUF_VERSION GET "${VIR_BUILD_VERSIONS_JSON}" static_dependencies protobuf version)

	find_program(VIR_PROTOC_EXECUTABLE protoc REQUIRED)
	execute_process(COMMAND "${VIR_PROTOC_EXECUTABLE}" --version OUTPUT_VARIABLE VIR_PROTOC_VERSION_OUTPUT OUTPUT_STRIP_TRAILING_WHITESPACE)
	if(NOT VIR_PROTOC_VERSION_OUTPUT MATCHES "libprotoc ${VIR_PROTOBUF_VERSION}$")
		message(FATAL_ERROR "protoc '${VIR_PROTOC_VERSION_OUTPUT}' does not match the pinned static protobuf runtime ${VIR_PROTOBUF_VERSION}; generated code and runtime must agree.")
	endif()

	set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
	set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
	set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
	set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
	set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
	set(protobuf_BUILD_LIBUPB OFF CACHE BOOL "" FORCE)
	set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
	set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE BOOL "" FORCE)
	set(BUILD_SHARED_LIBS OFF)

	FetchContent_Declare(absl URL "${VIR_ABSL_URL}" URL_HASH "SHA256=${VIR_ABSL_SHA256}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE SYSTEM)
	FetchContent_MakeAvailable(absl)
	FetchContent_Declare(protobuf URL "${VIR_PROTOBUF_URL}" URL_HASH "SHA256=${VIR_PROTOBUF_SHA256}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE SYSTEM)
	FetchContent_MakeAvailable(protobuf)
	include("${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake")
	set(VIR_PROTOBUF_GENERATE_EXTRA_ARGS PROTOC_EXE "${VIR_PROTOC_EXECUTABLE}")
else()
	find_package(Protobuf CONFIG REQUIRED)
	set(VIR_PROTOBUF_GENERATE_EXTRA_ARGS)
endif()

option(VIR_BUILD_TESTS "Build native unit, contract, and integration tests" ON)
option(VIR_BUILD_PM5_TUI "Build the Concept2 diagnostic terminal application" ON)

if(NOT DEFINED VIR_APP_VERSION OR NOT DEFINED VIR_APP_BUILD OR NOT DEFINED VIR_SOURCE_REVISION)
	message(FATAL_ERROR "Use `make configure` so the application version, build number, and source revision are stamped into the build.")
endif()

if(NOT VIR_APP_BUILD MATCHES "^[1-9][0-9]*$")
	message(FATAL_ERROR "VIR_APP_BUILD must be a positive integer for CFBundleVersion; got '${VIR_APP_BUILD}'.")
endif()

set(PM5_CAPABILITY_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/Config/PM5Capabilities.json")
set(PM5_CAPABILITY_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/generate_pm5_capabilities.py")
set(PM5_CAPABILITY_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/PM5CapabilityProfiles.generated.h")
execute_process(
	COMMAND "${Python3_EXECUTABLE}" "${PM5_CAPABILITY_GENERATOR}"
		--input "${PM5_CAPABILITY_MANIFEST}"
		--output "${PM5_CAPABILITY_HEADER}"
	RESULT_VARIABLE PM5_CAPABILITY_CONFIGURE_RESULT
	OUTPUT_VARIABLE PM5_CAPABILITY_CONFIGURE_OUTPUT
	ERROR_VARIABLE PM5_CAPABILITY_CONFIGURE_ERROR
)
if(NOT PM5_CAPABILITY_CONFIGURE_RESULT EQUAL 0)
	message(FATAL_ERROR
		"PM5 capability manifest validation failed during configure:\n"
		"${PM5_CAPABILITY_CONFIGURE_OUTPUT}${PM5_CAPABILITY_CONFIGURE_ERROR}")
endif()
add_custom_command(
	OUTPUT "${PM5_CAPABILITY_HEADER}"
	COMMAND "${Python3_EXECUTABLE}" "${PM5_CAPABILITY_GENERATOR}"
		--input "${PM5_CAPABILITY_MANIFEST}"
		--output "${PM5_CAPABILITY_HEADER}"
	DEPENDS "${PM5_CAPABILITY_GENERATOR}" "${PM5_CAPABILITY_MANIFEST}"
	COMMENT "Validating PM5 capability manifest and generating C++ allow-list"
	VERBATIM
)
add_custom_target(pm5_capability_profiles DEPENDS "${PM5_CAPABILITY_HEADER}")
if(BUILD_TESTING)
	add_test(
		NAME pm5_capability_manifest_tests
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/test_pm5_capabilities.py"
	)
	add_test(
		NAME public_header_dependency_check
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/check_public_header_dependencies.py"
	)
	add_test(
		NAME unreal_shipping_command_and_package_verifier_tests
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/test_unreal_packaging.py"
	)
	add_test(
		NAME content_origin_canary
		COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/Scripts/content_canary.py" canary
	)
endif()

function(vir_collect_sources out_var directory)
	if(EXISTS "${directory}")
		file(GLOB_RECURSE sources CONFIGURE_DEPENDS
			"${directory}/*.cpp"
			"${directory}/*.cc"
			"${directory}/*.cxx"
		)
		set(${out_var} "${sources}" PARENT_SCOPE)
	else()
		set(${out_var} "" PARENT_SCOPE)
	endif()
endfunction()

# Keep module boundaries visible in the build graph. Private implementation
# directories are never added to downstream module include paths.
vir_collect_sources(ROWING_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Private")
add_library(RowingCore STATIC ${ROWING_CORE_SOURCES})
add_library(rowing_core ALIAS RowingCore)
target_include_directories(RowingCore
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Public"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Private"
)
target_compile_features(RowingCore PUBLIC cxx_std_20)

vir_collect_sources(ROWING_DEVICE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingDevice/Private")
if(ROWING_DEVICE_SOURCES)
	add_library(RowingDevice STATIC ${ROWING_DEVICE_SOURCES})
	target_include_directories(RowingDevice PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingDevice/Public")
	target_link_libraries(RowingDevice PUBLIC RowingCore)
	target_compile_features(RowingDevice PUBLIC cxx_std_20)
else()
	add_library(RowingDevice INTERFACE)
	target_include_directories(RowingDevice INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingDevice/Public")
	target_link_libraries(RowingDevice INTERFACE RowingCore)
	target_compile_features(RowingDevice INTERFACE cxx_std_20)
endif()
add_library(rowing_device ALIAS RowingDevice)

# Phase 1 Milestones 1 and 3: generated Protobuf contracts
# (Contracts/proto/rowing/v1/). RowingCore stays free of any serialization
# dependency; only LocalData's private domain<->wire mapper and tests link this.
add_library(RowingContracts STATIC)
add_library(rowing_contracts ALIAS RowingContracts)
target_link_libraries(RowingContracts PUBLIC protobuf::libprotobuf)
target_include_directories(RowingContracts PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated/contracts")
target_compile_features(RowingContracts PUBLIC cxx_std_20)
protobuf_generate(
	TARGET RowingContracts
	IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Contracts/proto"
	PROTOS
		"${CMAKE_CURRENT_SOURCE_DIR}/Contracts/proto/rowing/v1/session.proto"
		"${CMAKE_CURRENT_SOURCE_DIR}/Contracts/proto/rowing/v1/telemetry.proto"
		"${CMAKE_CURRENT_SOURCE_DIR}/Contracts/proto/rowing/v1/session_summary.proto"
		"${CMAKE_CURRENT_SOURCE_DIR}/Contracts/proto/rowing/v1/session_object.proto"
		"${CMAKE_CURRENT_SOURCE_DIR}/Contracts/proto/rowing/v1/content.proto"
	PROTOC_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/contracts"
	${VIR_PROTOBUF_GENERATE_EXTRA_ARGS}
)

# Phase 2 Milestone 1: engine-independent signed content and route contracts.
# Protobuf stays private to the implementation; public consumers see stable
# domain values and a platform-neutral signature-verifier interface.
vir_collect_sources(CONTENT_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/ContentRuntime/Private")
add_library(ContentRuntime STATIC ${CONTENT_RUNTIME_SOURCES})
add_library(content_runtime ALIAS ContentRuntime)
target_include_directories(ContentRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/ContentRuntime/Public")
target_link_libraries(ContentRuntime PRIVATE RowingContracts)
target_compile_features(ContentRuntime PUBLIC cxx_std_20)

if(APPLE)
	add_library(ContentRuntimeMacCryptoKitShim STATIC Source/ContentRuntimeMac/Private/Ed25519CryptoKit.swift)
	if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_ARCHITECTURES)
		target_compile_options(ContentRuntimeMacCryptoKitShim PRIVATE -target "${CMAKE_OSX_ARCHITECTURES}-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET}")
	endif()
	add_library(ContentRuntimeMac STATIC Source/ContentRuntimeMac/Private/CryptoKitContentVerifier.cpp)
	target_include_directories(ContentRuntimeMac PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/ContentRuntimeMac/Public")
	target_link_libraries(ContentRuntimeMac PUBLIC ContentRuntime PRIVATE ContentRuntimeMacCryptoKitShim)
	target_compile_features(ContentRuntimeMac PUBLIC cxx_std_20)
	set_target_properties(ContentRuntimeMac PROPERTIES LINKER_LANGUAGE CXX)
endif()

# Phase 0 Milestone 4 Spike B: bounded local-durability journal spike
# (docs/phase-0/07-milestone-4-spikes.md). Depends only on RowingCore.
vir_collect_sources(LOCAL_DATA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Private")
add_library(LocalData STATIC ${LOCAL_DATA_SOURCES})
add_library(local_data ALIAS LocalData)
target_include_directories(LocalData
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Public"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Private" "${SQLite3_INCLUDE_DIR}" "${ZSTD_INCLUDE_DIR}"
)
target_link_libraries(LocalData PUBLIC RowingCore RowingDevice ContentRuntime PRIVATE "${SQLite3_LIBRARY}" "${ZSTD_LIBRARY}" RowingContracts)
target_compile_features(LocalData PUBLIC cxx_std_20)

# Phase 1 Milestone 3: Apple-only LocalData cipher adapter (CryptoKit AES-256-GCM
# via a small Swift shim, Keychain-held data key). Mirrors the Concept2PMCore /
# Concept2PMMac split: LocalData stays free of Apple types and only sees
# IBlobCipher. CryptoKit's AES.GCM is Swift-only, so the shim is its own
# Swift-only library exposing a C ABI, linked into the C++ adapter.
if(APPLE)
	enable_language(Swift)
	add_library(LocalDataMacCryptoKitShim STATIC Source/LocalDataMac/Private/AesGcmCryptoKit.swift)
	# swiftc does not read CMAKE_OSX_DEPLOYMENT_TARGET and would otherwise stamp the
	# host OS version into the object, which the app archive (linked at the
	# deployment target) then warns about and could not run below.
	if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_ARCHITECTURES)
		target_compile_options(LocalDataMacCryptoKitShim PRIVATE -target "${CMAKE_OSX_ARCHITECTURES}-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET}")
	endif()
	add_library(LocalDataMac STATIC Source/LocalDataMac/Private/CryptoKitBlobCipher.cpp)
	target_include_directories(LocalDataMac PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalDataMac/Public")
	target_link_libraries(LocalDataMac PUBLIC LocalData PRIVATE LocalDataMacCryptoKitShim "-framework Security" "-framework CoreFoundation")
	target_compile_features(LocalDataMac PUBLIC cxx_std_20)
	# Linking the Swift shim would otherwise select swiftc as the archiver.
	set_target_properties(LocalDataMac PROPERTIES LINKER_LANGUAGE CXX)
endif()

set(CONCEPT2_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Plugins/Concept2PM")
vir_collect_sources(CONCEPT2_CORE_SOURCES "${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMCore")
if(CONCEPT2_CORE_SOURCES)
	add_library(Concept2PMCore STATIC ${CONCEPT2_CORE_SOURCES})
	add_library(concept2_pm_core ALIAS Concept2PMCore)
	target_include_directories(Concept2PMCore PUBLIC
		"${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMCore/Public"
		"${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Public"
		"${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingDevice/Public"
	)
	target_link_libraries(Concept2PMCore PUBLIC RowingDevice RowingCore)
	target_compile_features(Concept2PMCore PUBLIC cxx_std_20)

	if(APPLE)
		enable_language(OBJCXX)
		file(GLOB_RECURSE CONCEPT2_MAC_SOURCES CONFIGURE_DEPENDS
			"${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMMac/*.cpp"
			"${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMMac/*.mm"
		)
	else()
		file(GLOB_RECURSE CONCEPT2_MAC_SOURCES CONFIGURE_DEPENDS
			"${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMMac/*.cpp"
		)
	endif()
	if(CONCEPT2_MAC_SOURCES)
		add_library(Concept2PMMac STATIC ${CONCEPT2_MAC_SOURCES})
		target_include_directories(Concept2PMMac PUBLIC
			"${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMMac/Public"
			"${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMCore/Public"
			"${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Public"
			"${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingDevice/Public"
		)
		target_include_directories(Concept2PMMac PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
		target_link_libraries(Concept2PMMac PUBLIC Concept2PMCore)
		target_compile_features(Concept2PMMac PUBLIC cxx_std_20)
		add_dependencies(Concept2PMMac pm5_capability_profiles)
		if(APPLE)
			target_link_libraries(Concept2PMMac PRIVATE
				"-framework CoreBluetooth"
				"-framework Foundation"
			)
		endif()
	endif()
endif()

# Phase 1 Milestone 4: engine-independent Just Row session orchestrator
# (docs/phase-1/04-milestone-4-workout-runtime.md). Depends on RowingCore,
# RowingDevice, and LocalData; its private wire mapper links RowingContracts.
vir_collect_sources(WORKOUT_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Private")
add_library(WorkoutRuntime STATIC ${WORKOUT_RUNTIME_SOURCES})
add_library(workout_runtime ALIAS WorkoutRuntime)
target_include_directories(WorkoutRuntime
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Public"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Private"
)
target_link_libraries(WorkoutRuntime PUBLIC RowingCore RowingDevice LocalData PRIVATE RowingContracts)
target_compile_features(WorkoutRuntime PUBLIC cxx_std_20)

vir_collect_sources(ONLINE_CLIENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/OnlineClient/Private")
add_library(OnlineClient STATIC ${ONLINE_CLIENT_SOURCES})
target_include_directories(OnlineClient PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/OnlineClient/Public")
target_link_libraries(OnlineClient PUBLIC LocalData)
target_compile_features(OnlineClient PUBLIC cxx_std_20)
if(APPLE)
	 target_sources(OnlineClient PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/OnlineClient/Private/MacHttpTransport.mm")
	 target_link_libraries(OnlineClient PRIVATE "-framework Foundation")
endif()

# Phase 1 Milestone 8: deterministic, engine-independent course and rower
# presentation. It consumes normalized domain facts and never writes them back.
vir_collect_sources(COURSE_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/CourseRuntime/Private")
add_library(CourseRuntime STATIC ${COURSE_RUNTIME_SOURCES})
add_library(course_runtime ALIAS CourseRuntime)
target_include_directories(CourseRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/CourseRuntime/Public")
target_link_libraries(CourseRuntime PUBLIC RowingCore ContentRuntime)
target_compile_features(CourseRuntime PUBLIC cxx_std_20)

vir_collect_sources(ROWING_SIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingSim/src")
if(ROWING_SIM_SOURCES)
	add_library(RowingSim STATIC ${ROWING_SIM_SOURCES})
	target_include_directories(RowingSim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingSim/include")
	target_link_libraries(RowingSim PUBLIC RowingDevice RowingCore)
	target_compile_features(RowingSim PUBLIC cxx_std_20)
endif()

# Phase 0 Milestone 4 Spike B kill/recover harness. Not built into the
# product; matches the Tools/pm5-tui pattern.
vir_collect_sources(DURABILITY_SPIKE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Tools/durability-spike/src")
if(DURABILITY_SPIKE_SOURCES AND TARGET RowingSim AND NOT VIR_APP_LIBS_ONLY)
	add_executable(durability-spike ${DURABILITY_SPIKE_SOURCES})
	target_link_libraries(durability-spike PRIVATE LocalData RowingCore RowingSim)
	target_compile_features(durability-spike PRIVATE cxx_std_20)
endif()

if(VIR_BUILD_PM5_TUI AND NOT VIR_APP_LIBS_ONLY)
	add_subdirectory(external/ftxui)
	if(APPLE)
		configure_file(
			"${CMAKE_CURRENT_SOURCE_DIR}/Build/PM5Diagnostic-Info.plist.in"
			"${CMAKE_CURRENT_BINARY_DIR}/PM5Diagnostic-Info.plist"
			@ONLY
		)
	endif()

	vir_collect_sources(PM5_TUI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Tools/pm5-tui/src")
	if(PM5_TUI_SOURCES)
		if(APPLE)
			add_executable(pm5-tui MACOSX_BUNDLE ${PM5_TUI_SOURCES})
			set_target_properties(pm5-tui PROPERTIES
				MACOSX_BUNDLE_BUNDLE_NAME "PM5 Diagnostic"
				MACOSX_BUNDLE_BUNDLE_IDENTIFIER "dev.virtualrowing.pm5-diagnostic"
				MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/PM5Diagnostic-Info.plist"
				XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
				XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
			)
			add_custom_command(TARGET pm5-tui POST_BUILD
				COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:pm5-tui>"
				COMMENT "Ad-hoc signing the PM5 diagnostic bundle"
				VERBATIM
			)
		else()
			add_executable(pm5-tui ${PM5_TUI_SOURCES})
		endif()
		target_link_libraries(pm5-tui PRIVATE
			RowingDevice
			ftxui::component
		)
		if(TARGET Concept2PMMac)
			target_link_libraries(pm5-tui PRIVATE Concept2PMMac)
		endif()
		# Phase 1 Milestone 4: opt-in sealed workout journal (--journal). Needs
		# the Apple-only Keychain/CryptoKit cipher, so it exists only where
		# LocalDataMac does.
		if(TARGET LocalDataMac)
			target_link_libraries(pm5-tui PRIVATE WorkoutRuntime LocalDataMac)
			target_compile_definitions(pm5-tui PRIVATE VIR_PM5_TUI_JOURNAL=1)
			# The Swift shim would otherwise select swiftc as the linker driver.
			set_target_properties(pm5-tui PROPERTIES LINKER_LANGUAGE CXX)
		endif()
		if(TARGET RowingSim)
			target_link_libraries(pm5-tui PRIVATE RowingSim)
		endif()
		target_compile_features(pm5-tui PRIVATE cxx_std_20)
		target_compile_definitions(pm5-tui PRIVATE VIR_SOURCE_REVISION=\"${VIR_SOURCE_REVISION}\")
	endif()
endif()

if(VIR_BUILD_TESTS AND BUILD_TESTING AND NOT VIR_APP_LIBS_ONLY)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Tests/RowingTelemetryUtilitiesTests.cpp")
		add_executable(rowing_core_tests Source/RowingCore/Tests/RowingTelemetryUtilitiesTests.cpp)
		target_include_directories(rowing_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Private")
		target_link_libraries(rowing_core_tests PRIVATE RowingCore)
		add_test(NAME rowing_core_tests COMMAND rowing_core_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Tests/RowingSessionStateMachineTests.cpp")
		add_executable(rowing_session_state_machine_tests Source/RowingCore/Tests/RowingSessionStateMachineTests.cpp)
		target_link_libraries(rowing_session_state_machine_tests PRIVATE RowingCore)
		add_test(NAME rowing_session_state_machine_tests COMMAND rowing_session_state_machine_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Contract/RowingSessionContractTests.cpp")
		add_executable(rowing_session_contract_tests Tests/Contract/RowingSessionContractTests.cpp)
		target_link_libraries(rowing_session_contract_tests PRIVATE RowingContracts)
		add_test(NAME rowing_session_contract_tests COMMAND rowing_session_contract_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Tests/LocalDataJournalTests.cpp")
		add_executable(local_data_tests Source/LocalData/Tests/LocalDataJournalTests.cpp)
		target_include_directories(local_data_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Private" "${SQLite3_INCLUDE_DIR}")
		target_link_libraries(local_data_tests PRIVATE LocalData "${SQLite3_LIBRARY}")
		add_test(NAME local_data_tests COMMAND local_data_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Tests/TelemetryWireMappingTests.cpp")
		add_executable(local_data_wire_mapping_tests Source/LocalData/Tests/TelemetryWireMappingTests.cpp)
		target_include_directories(local_data_wire_mapping_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Private")
		target_link_libraries(local_data_wire_mapping_tests PRIVATE LocalData RowingDevice)
		add_test(NAME local_data_wire_mapping_tests COMMAND local_data_wire_mapping_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Tests/ContentRepositoryTests.cpp")
		add_executable(content_repository_tests Source/LocalData/Tests/ContentRepositoryTests.cpp)
		target_link_libraries(content_repository_tests PRIVATE LocalData ContentRuntime)
		add_test(NAME content_repository_tests COMMAND content_repository_tests)
	endif()
	if(TARGET OnlineClient AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/OnlineClient/Tests/OnlineClientTests.cpp")
		add_executable(online_client_tests Source/OnlineClient/Tests/OnlineClientTests.cpp)
		target_link_libraries(online_client_tests PRIVATE OnlineClient LocalData)
		add_test(NAME online_client_tests COMMAND online_client_tests)
	endif()
	if(TARGET LocalDataMac AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalDataMac/Tests/CryptoKitBlobCipherTests.cpp")
		add_executable(local_data_mac_tests Source/LocalDataMac/Tests/CryptoKitBlobCipherTests.cpp)
		target_link_libraries(local_data_mac_tests PRIVATE LocalDataMac)
		# The Swift shim makes CMake pick swiftc as the linker driver, which
		# rejects clang's -Wl,-framework forms; link with the C++ driver instead.
		set_target_properties(local_data_mac_tests PROPERTIES LINKER_LANGUAGE CXX)
		add_test(NAME local_data_mac_tests COMMAND local_data_mac_tests)
		set_tests_properties(local_data_mac_tests PROPERTIES SKIP_RETURN_CODE 77)
	endif()
	if(TARGET RowingSim AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Tests/WorkoutSessionTests.cpp")
		add_executable(workout_runtime_tests Source/WorkoutRuntime/Tests/WorkoutSessionTests.cpp)
		target_include_directories(workout_runtime_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Private")
		target_link_libraries(workout_runtime_tests PRIVATE WorkoutRuntime RowingContracts RowingSim)
		add_test(NAME workout_runtime_tests COMMAND workout_runtime_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Tests/DeviceConnectorTests.cpp")
		add_executable(device_connector_tests Source/WorkoutRuntime/Tests/DeviceConnectorTests.cpp)
		target_link_libraries(device_connector_tests PRIVATE WorkoutRuntime)
		add_test(NAME device_connector_tests COMMAND device_connector_tests)
	endif()
	if(TARGET RowingSim AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Tests/RealDeviceControllerTests.cpp")
		add_executable(real_device_controller_tests Source/WorkoutRuntime/Tests/RealDeviceControllerTests.cpp)
		target_link_libraries(real_device_controller_tests PRIVATE WorkoutRuntime RowingContracts RowingSim)
		add_test(NAME real_device_controller_tests COMMAND real_device_controller_tests)
	endif()
	if(TARGET RowingSim AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Tests/AppJournalTests.cpp")
		add_executable(app_journal_tests Source/WorkoutRuntime/Tests/AppJournalTests.cpp)
		target_link_libraries(app_journal_tests PRIVATE WorkoutRuntime RowingContracts RowingSim)
		add_test(NAME app_journal_tests COMMAND app_journal_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/WorkoutRuntime/Tests/WorkoutDisplayTests.cpp")
		add_executable(workout_display_tests Source/WorkoutRuntime/Tests/WorkoutDisplayTests.cpp)
		target_link_libraries(workout_display_tests PRIVATE WorkoutRuntime)
		add_test(NAME workout_display_tests COMMAND workout_display_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/CourseRuntime/Tests/CoursePresentationTests.cpp")
		add_executable(course_runtime_tests Source/CourseRuntime/Tests/CoursePresentationTests.cpp)
		target_link_libraries(course_runtime_tests PRIVATE CourseRuntime)
		add_test(NAME course_runtime_tests COMMAND course_runtime_tests)
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/ContentRuntime/Tests/ContentRuntimeTests.cpp")
		add_executable(content_runtime_tests Source/ContentRuntime/Tests/ContentRuntimeTests.cpp)
		target_link_libraries(content_runtime_tests PRIVATE ContentRuntime RowingContracts)
		add_test(NAME content_runtime_tests COMMAND content_runtime_tests)
	endif()
	if(TARGET ContentRuntimeMac AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/ContentRuntimeMac/Tests/CryptoKitContentVerifierTests.cpp")
		add_executable(content_runtime_mac_tests Source/ContentRuntimeMac/Tests/CryptoKitContentVerifierTests.cpp)
		target_link_libraries(content_runtime_mac_tests PRIVATE ContentRuntimeMac)
		set_target_properties(content_runtime_mac_tests PROPERTIES LINKER_LANGUAGE CXX)
		add_test(NAME content_runtime_mac_tests COMMAND content_runtime_mac_tests)
	endif()
	if(TARGET Concept2PMCore)
		file(GLOB_RECURSE CONCEPT2_TEST_SOURCES CONFIGURE_DEPENDS
			"${CONCEPT2_PLUGIN_DIR}/Tests/*.cpp"
		)
		foreach(test_source IN LISTS CONCEPT2_TEST_SOURCES)
			get_filename_component(test_name "${test_source}" NAME_WE)
			set(test_target "concept2_${test_name}")
			add_executable("${test_target}" "${test_source}")
			target_link_libraries("${test_target}" PRIVATE Concept2PMCore)
			target_include_directories("${test_target}" PRIVATE "${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMCore/Private")
			add_test(NAME "${test_target}" COMMAND "${test_target}")
		endforeach()
	endif()
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/Contract/pm5_reviewed_profile_tests.cpp" AND TARGET Concept2PMCore)
		# Phase 1 Milestone 2: exercises the actual generated, reviewed
		# non-diagnostic profile set (Config/PM5Capabilities.json via
		# pm5_capability_profiles) rather than a synthetic one, so it needs
		# the generated header's include dir and build dependency.
		add_executable(pm5_reviewed_profile_tests Tests/Contract/pm5_reviewed_profile_tests.cpp)
		target_link_libraries(pm5_reviewed_profile_tests PRIVATE Concept2PMCore)
		target_include_directories(pm5_reviewed_profile_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
		add_dependencies(pm5_reviewed_profile_tests pm5_capability_profiles)
		add_test(NAME pm5_reviewed_profile_tests COMMAND pm5_reviewed_profile_tests)
	endif()
	if(TARGET RowingSim)
		foreach(test_source IN ITEMS
			Tests/Contract/rowing_sim_contract_tests.cpp
			Tests/Integration/rowing_sim_integration_tests.cpp
		)
			get_filename_component(test_target "${test_source}" NAME_WE)
			add_executable("${test_target}" "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}")
			target_link_libraries("${test_target}" PRIVATE RowingSim)
			add_test(NAME "${test_target}" COMMAND "${test_target}")
		endforeach()
	endif()
	if(TARGET durability-spike)
		add_test(
			NAME durability_spike_kill_recover
			COMMAND "${Python3_EXECUTABLE}"
				"${CMAKE_CURRENT_SOURCE_DIR}/Tests/Integration/durability_spike_kill_recover.py"
				"$<TARGET_FILE:durability-spike>"
		)
	endif()
	if(TARGET pm5-tui)
		add_test(
			NAME pm5_tui_status_diagnostics
			COMMAND "${Python3_EXECUTABLE}"
				"${CMAKE_CURRENT_SOURCE_DIR}/Tools/pm5-tui/tests/StatusDiagnosticsSmoke.py"
				"$<TARGET_FILE:pm5-tui>"
		)

		add_executable(
			pm5_tui_logger_tests
			Tools/pm5-tui/tests/RotatingFileLoggerTests.cpp
			Tools/pm5-tui/src/RotatingFileLogger.cpp
		)
		target_include_directories(pm5_tui_logger_tests PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/Tools/pm5-tui/src"
		)
		target_compile_features(pm5_tui_logger_tests PRIVATE cxx_std_20)
		add_test(NAME pm5_tui_logger_tests COMMAND pm5_tui_logger_tests)

		add_executable(
			pm5_tui_metrics_writer_tests
			Tools/pm5-tui/tests/RunMetricsWriterTests.cpp
			Tools/pm5-tui/src/RunMetricsWriter.cpp
			Tools/pm5-tui/src/RunMetricsJson.cpp
			Tools/pm5-tui/src/RunMetricsNames.cpp
		)
		target_include_directories(pm5_tui_metrics_writer_tests PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/Tools/pm5-tui/src"
			"${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMCore/Public"
			"${CONCEPT2_PLUGIN_DIR}/Source/Concept2PMMac/Public"
			"${CMAKE_CURRENT_SOURCE_DIR}/Source/RowingCore/Public"
		)
		target_link_libraries(pm5_tui_metrics_writer_tests PRIVATE Concept2PMCore)
		target_compile_features(pm5_tui_metrics_writer_tests PRIVATE cxx_std_20)
		add_test(NAME pm5_tui_metrics_writer_tests COMMAND pm5_tui_metrics_writer_tests)

		add_test(
			NAME pm5_tui_smoke
			COMMAND $<TARGET_FILE:pm5-tui> --script status connect select 1 quit
		)
		add_test(
			NAME pm5_tui_invalid_selection
			COMMAND $<TARGET_FILE:pm5-tui> --script "select 0" quit
		)
		set_tests_properties(pm5_tui_invalid_selection PROPERTIES
			PASS_REGULAR_EXPRESSION "candidate numbers start at 1"
		)
		add_test(
			NAME pm5_tui_ignores_blank_commands
			COMMAND $<TARGET_FILE:pm5-tui> --script "  " status quit
		)
		set_tests_properties(pm5_tui_ignores_blank_commands PROPERTIES
			PASS_REGULAR_EXPRESSION "discovery candidates=0"
			FAIL_REGULAR_EXPRESSION "unknown command"
		)
	endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
foreach(target RowingCore RowingDevice ContentRuntime ContentRuntimeMac LocalData LocalDataMac WorkoutRuntime CourseRuntime Concept2PMCore Concept2PMMac RowingSim durability-spike)
	if(TARGET "${target}")
		get_target_property(target_type "${target}" TYPE)
		if(target_type STREQUAL "INTERFACE_LIBRARY")
			target_compile_options("${target}" INTERFACE -Wall -Wextra -Wpedantic)
		else()
			target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
		endif()
	endif()
endforeach()
endif()

# Phase 1 Milestone 5: merge every static archive the Unreal app needs into one
# library so the UBT module has a single PublicAdditionalLibraries entry. The
# closure is computed from the target graph (static protobuf/abseil included).
# SQLite remains a system library declared in VirtualRowing.Build.cs; Zstandard
# is explicitly merged because LocalData consumes it through a private CMake
# dependency, which static libraries do not propagate to UBT.
if(VIR_APP_LIBS_ONLY)
	set(VIR_APP_LIB_ROOTS WorkoutRuntime CourseRuntime ContentRuntime)
	# Phase 1 Milestone 10: the opt-in development coordinator is linked into the
	# app archive; it remains dormant until its process environment enables it.
	if(TARGET OnlineClient)
		list(APPEND VIR_APP_LIB_ROOTS OnlineClient)
	endif()
	if(TARGET RowingSim)
		list(APPEND VIR_APP_LIB_ROOTS RowingSim)
	endif()
	# Phase 1 Milestone 7: the real-device path. The CoreBluetooth adapter and the
	# Keychain-sealed CryptoKit cipher (with its Swift shim) join the archive.
	foreach(vir_real_device_target Concept2PMMac LocalDataMac)
		if(TARGET ${vir_real_device_target})
			list(APPEND VIR_APP_LIB_ROOTS ${vir_real_device_target})
		endif()
	endforeach()
	if(TARGET ContentRuntimeMac)
		list(APPEND VIR_APP_LIB_ROOTS ContentRuntimeMac)
	endif()

	function(vir_collect_static_archives target visited_var archives_var)
		get_target_property(aliased "${target}" ALIASED_TARGET)
		if(aliased)
			set(target "${aliased}")
		endif()
		set(visited "${${visited_var}}")
		if(target IN_LIST visited)
			return()
		endif()
		list(APPEND visited "${target}")
		set(archives "${${archives_var}}")
		get_target_property(target_type "${target}" TYPE)
		if(target_type STREQUAL "STATIC_LIBRARY")
			list(APPEND archives "$<TARGET_FILE:${target}>")
		endif()
		get_target_property(deps "${target}" INTERFACE_LINK_LIBRARIES)
		if(deps)
			foreach(dep IN LISTS deps)
				# Unwrap $<LINK_ONLY:...>/$<BUILD_INTERFACE:...>; skip other genexes and file paths.
				if(dep MATCHES "^\\$<(LINK_ONLY|BUILD_INTERFACE):([^>]+)>$")
					set(dep "${CMAKE_MATCH_2}")
				endif()
				if(TARGET "${dep}")
					vir_collect_static_archives("${dep}" visited archives)
				endif()
			endforeach()
		endif()
		set(${visited_var} "${visited}" PARENT_SCOPE)
		set(${archives_var} "${archives}" PARENT_SCOPE)
	endfunction()

	set(VIR_APP_VISITED "")
	set(VIR_APP_ARCHIVES "")
	foreach(root IN LISTS VIR_APP_LIB_ROOTS)
		vir_collect_static_archives("${root}" VIR_APP_VISITED VIR_APP_ARCHIVES)
	endforeach()
	list(REMOVE_DUPLICATES VIR_APP_ARCHIVES)

	set(VIR_APP_MERGED_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/lib/libVirRowingApp.a")
	add_custom_command(
		OUTPUT "${VIR_APP_MERGED_ARCHIVE}"
		COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/lib"
		COMMAND xcrun libtool -static -o "${VIR_APP_MERGED_ARCHIVE}" ${VIR_APP_ARCHIVES} "${ZSTD_LIBRARY}"
		DEPENDS ${VIR_APP_VISITED} "${ZSTD_LIBRARY}"
		COMMAND_EXPAND_LISTS
		VERBATIM
		COMMENT "Merging app-bound static archives into libVirRowingApp.a"
	)
	add_custom_target(vir_app_libs ALL DEPENDS "${VIR_APP_MERGED_ARCHIVE}")
endif()
