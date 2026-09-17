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

# Phase 1 Milestone 1 (docs/phase-1/01-milestone-1-rowingcore-domain-contract.md)
# pins the Protobuf toolchain the same way cmake/ninja/git-lfs are pinned in
# Config/BuildVersions.json — an exact version checked by `make doctor`, not a
# floating dependency. Install with `brew install protobuf`.
find_package(Protobuf CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# Phase 0 Milestone 4 Spike B: bounded local-durability journal spike
# (docs/phase-0/07-milestone-4-spikes.md). Depends only on RowingCore.
vir_collect_sources(LOCAL_DATA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Private")
add_library(LocalData STATIC ${LOCAL_DATA_SOURCES})
add_library(local_data ALIAS LocalData)
target_include_directories(LocalData
	PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Public"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source/LocalData/Private" "${SQLite3_INCLUDE_DIR}"
)
target_link_libraries(LocalData PUBLIC RowingCore PRIVATE "${SQLite3_LIBRARY}")
target_compile_features(LocalData PUBLIC cxx_std_20)

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

vir_collect_sources(PM5_SIM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Tools/pm5-sim/src")
if(PM5_SIM_SOURCES)
	add_library(pm5-sim STATIC ${PM5_SIM_SOURCES})
	target_include_directories(pm5-sim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Tools/pm5-sim/include")
	target_link_libraries(pm5-sim PUBLIC RowingDevice RowingCore)
	target_compile_features(pm5-sim PUBLIC cxx_std_20)
endif()

# Phase 0 Milestone 4 Spike B kill/recover harness. Not built into the
# product; matches the Tools/pm5-tui, Tools/pm5-sim pattern.
vir_collect_sources(DURABILITY_SPIKE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Tools/durability-spike/src")
if(DURABILITY_SPIKE_SOURCES AND TARGET pm5-sim)
	add_executable(durability-spike ${DURABILITY_SPIKE_SOURCES})
	target_link_libraries(durability-spike PRIVATE LocalData RowingCore pm5-sim)
	target_compile_features(durability-spike PRIVATE cxx_std_20)
endif()

if(VIR_BUILD_PM5_TUI)
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
		if(TARGET pm5-sim)
			target_link_libraries(pm5-tui PRIVATE pm5-sim)
		endif()
		target_compile_features(pm5-tui PRIVATE cxx_std_20)
		target_compile_definitions(pm5-tui PRIVATE VIR_SOURCE_REVISION=\"${VIR_SOURCE_REVISION}\")
	endif()
endif()

if(VIR_BUILD_TESTS AND BUILD_TESTING)
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
		# Phase 1 Milestone 1: generated Protobuf contract seed
		# (Contracts/proto/rowing/v1/session.proto). Deliberately decoupled from
		# RowingCore in both directions — RowingCore stays free of any
		# serialization library dependency, and nothing depends on
		# RowingContracts yet besides this test; a later milestone adds the
		# domain<->wire mapping once a real consumer exists. Built only here,
		# not at top level, since a plain `make build` has no consumer for it.
		set(ROWING_CONTRACTS_PROTO "${CMAKE_CURRENT_SOURCE_DIR}/Contracts/proto/rowing/v1/session.proto")
		add_library(RowingContracts STATIC)
		add_library(rowing_contracts ALIAS RowingContracts)
		target_link_libraries(RowingContracts PUBLIC protobuf::libprotobuf)
		target_include_directories(RowingContracts PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated/contracts")
		target_compile_features(RowingContracts PUBLIC cxx_std_20)
		protobuf_generate(
			TARGET RowingContracts
			IMPORT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Contracts/proto"
			PROTOS "${ROWING_CONTRACTS_PROTO}"
			PROTOC_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/contracts"
		)

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
	if(TARGET pm5-sim)
		foreach(test_source IN ITEMS
			Tests/Contract/pm5_sim_contract_tests.cpp
			Tests/Integration/pm5_sim_integration_tests.cpp
		)
			get_filename_component(test_target "${test_source}" NAME_WE)
			add_executable("${test_target}" "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}")
			target_link_libraries("${test_target}" PRIVATE pm5-sim)
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
foreach(target RowingCore RowingDevice LocalData Concept2PMCore Concept2PMMac pm5-sim durability-spike)
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
