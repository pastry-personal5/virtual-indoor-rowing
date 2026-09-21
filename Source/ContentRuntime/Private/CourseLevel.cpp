#include "ContentRuntime/CourseLevel.h"

#include <array>

namespace ContentRuntime
{
	std::optional<std::string> CourseLevelAssetPathForRoute(std::string_view RouteId) noexcept
	{
		if (RouteId == "route.han-river.5k")
			return std::string("/Game/Phase2/HanRiver/Maps/L_HanRiver_BlueHour");
		return std::nullopt;
	}

	bool IsCourseLevelActorClassAllowed(std::string_view ClassPathName) noexcept
	{
		static constexpr std::array<std::string_view, 9> Allowed = {
			"/Script/Engine.StaticMeshActor",
			"/Script/Engine.DirectionalLight",
			"/Script/Engine.SkyLight",
			"/Script/Engine.SkyAtmosphere",
			"/Script/Engine.ExponentialHeightFog",
			"/Script/Engine.PostProcessVolume",
			"/Script/Engine.WorldSettings",
			"/Script/Engine.LevelScriptActor",
			"/Script/Engine.Brush"};
		for (std::string_view Name : Allowed)
		{
			if (Name == ClassPathName)
				return true;
		}
		return false;
	}
} // namespace ContentRuntime
