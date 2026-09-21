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
		static constexpr std::array<std::string_view, 10> Allowed = {
			"/Script/Engine.Actor",
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

	bool CourseLevelActorRequiresComponentCheck(std::string_view ClassPathName) noexcept
	{
		return ClassPathName == "/Script/Engine.Actor";
	}

	bool IsCourseLevelComponentClassAllowed(std::string_view ClassPathName) noexcept
	{
		static constexpr std::array<std::string_view, 4> Allowed = {
			"/Script/Engine.SceneComponent",
			"/Script/Engine.StaticMeshComponent",
			"/Script/Engine.InstancedStaticMeshComponent",
			"/Script/Engine.HierarchicalInstancedStaticMeshComponent"};
		for (std::string_view Name : Allowed)
		{
			if (Name == ClassPathName)
				return true;
		}
		return false;
	}
} // namespace ContentRuntime
