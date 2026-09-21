#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ContentRuntime
{
	// Runtime selection is by stable route ID. The signed package never names the
	// level to open: the client owns this fixed table, so a hostile package cannot
	// steer the loader to an arbitrary package path.
	std::optional<std::string> CourseLevelAssetPathForRoute(std::string_view RouteId) noexcept;

	// A downloaded level is data only. Only these native engine actor classes may
	// appear in it; a Blueprint-generated class, level Blueprint, or project class
	// is rejected before the level is made visible. Takes a class path name such
	// as "/Script/Engine.StaticMeshActor".
	bool IsCourseLevelActorClassAllowed(std::string_view ClassPathName) noexcept;
} // namespace ContentRuntime
