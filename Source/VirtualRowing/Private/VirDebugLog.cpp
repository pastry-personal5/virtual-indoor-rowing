#include "VirDebugLog.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace
{
	const char *GVirDebugLogPath = "/Volumes/Unreal_Engine_Volume/work/virtual-indoor-rowing/Logs/vir-startup-debug/trace.log";
} // namespace

void VirDebugLog(const FString &Message)
{
	// Raw POSIX stdio only: no FFileHelper, no IFileManager, no UE_LOG. This must work
	// regardless of any UE engine subsystem's readiness, so a silent failure here would
	// mean the calling code itself never ran, not that some engine subsystem wasn't ready.
	fprintf(stderr, "VIR_DEBUG: %s\n", TCHAR_TO_ANSI(*Message));
	fflush(stderr);

	if (FILE *File = fopen(GVirDebugLogPath, "a"))
	{
		fprintf(File, "%s\n", TCHAR_TO_ANSI(*Message));
		fclose(File);
	}
	else
	{
		fprintf(stderr, "VIR_DEBUG: fopen(%s) failed: %s\n", GVirDebugLogPath, strerror(errno));
		fflush(stderr);
	}
}
