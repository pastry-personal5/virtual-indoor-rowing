#include "LocalData/PresentationPreferenceRepository.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
	std::filesystem::path TemporaryDatabase(std::string_view Name)
	{
		return std::filesystem::temp_directory_path() /
			   (std::string(Name) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".sqlite3");
	}
} // namespace

int main()
{
	const auto Database = TemporaryDatabase("rowing");
	{
		LocalData::FPresentationPreferenceRepository Repository(Database);
		assert(Repository.LoadCameraPreset() == LocalData::ECameraPreferencePreset::Medium);
		Repository.SaveCameraPreset(LocalData::ECameraPreferencePreset::Close);
		assert(Repository.LoadCameraPreset() == LocalData::ECameraPreferencePreset::Close);
	}
	{
		LocalData::FCoalescedCameraPreference Preference(Database);
		assert(Preference.Get() == LocalData::ECameraPreferencePreset::Close);
		Preference.Set(LocalData::ECameraPreferencePreset::Medium);
		Preference.Set(LocalData::ECameraPreferencePreset::Wide);
		Preference.Set(LocalData::ECameraPreferencePreset::Close);
		assert(Preference.Get() == LocalData::ECameraPreferencePreset::Close);
		Preference.WaitForIdleForTesting();
		assert(!Preference.HadPersistenceFailure());
	}
	{
		LocalData::FPresentationPreferenceRepository Repository(Database);
		assert(Repository.LoadCameraPreset() == LocalData::ECameraPreferencePreset::Close);
	}
	std::filesystem::remove(Database);

	const auto Journal = Database.parent_path() / "workout-journal.sqlite3";
	std::filesystem::remove(Journal);
	try
	{
		LocalData::FPresentationPreferenceRepository Forbidden(Journal);
		assert(false && "workout journal path was accepted");
	}
	catch (const std::invalid_argument &)
	{
	}
	assert(!std::filesystem::exists(Journal));
	std::cout << "presentation preference repository tests passed\n";
}
