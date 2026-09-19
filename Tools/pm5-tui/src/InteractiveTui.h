#pragma once

namespace PM5Tui
{
	// Runs the interactive FTXUI diagnostic UI until the user quits. Returns the process exit code.
	int RunInteractiveTui(bool HardwareProbeEnabled, bool JournalEnabled);
} // namespace PM5Tui
