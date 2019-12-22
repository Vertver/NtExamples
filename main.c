#define PHNT_VERSION PHNT_19H1 // Windows 10 1903
#include "phnt_windows.h"
#include "phnt.h"
#include <stdio.h>
#include <process.h>
#pragma comment(lib, "ntdll.lib")

#define _100NSTOMS(x) ((float)x / 10000.f)
#define MSTO100NS(x) ((ULONG)(x * 10000.f))
#define TEST_MODE 3

typedef BOOL(WINAPI* PROCESSPROC)(HANDLE hProcess);
#define IsInvalidHandle(x) (x == INVALID_HANDLE_VALUE || !x)

ULONG g_ulCurRes = 0;
int TestTimerResolution();
int GetProcessorPerCoreLoad(float** pFloatLoad);
void PrintProcessorLoad();
void TestFunc();

int
main()
{
#if TEST_MODE == 1
	if (!!TestTimerResolution()) return -1;
#elif TEST_MODE == 2
	for (size_t i = 0; i < 100; i++) {
		PrintProcessorLoad();
		Sleep(1000);
	}
#elif TEST_MODE == 3
	TestFunc();
#endif
	system("pause");
	return 0;
}

int
TestTimerResolution()
{
	ULONG ulMinRes = 0;
	ULONG ulMaxRes = 0;

	if (NtQueryTimerResolution(&ulMinRes, &ulMaxRes, &g_ulCurRes) != 0) {
		printf("NtQueryTimerResolution() function failed, aborting...\n");
		return -1;
	}

	printf("NtQueryTimerResolution(): \nMinimal resolution: %f\nMaximal resolution: %f\nCurrent resolution: %f\n", _100NSTOMS(ulMinRes), _100NSTOMS(ulMaxRes), _100NSTOMS(g_ulCurRes));
	return 0;
}

typedef enum 
{
	eNoneType = 0,
	eGamingType,
	eDesktopType,
	eHighResolutionType
} TIMER_MODE;

int
SetOptimalTimerResolution(int mode)
{
	BOOL bTrue = TRUE;
	ULONG ulSetRes = 0;
	ULONG ulCurRes = 0;
	ULONG ulMinRes = 0;
	ULONG ulMaxRes = 0;

	if (NtQueryTimerResolution(&ulMinRes, &ulMaxRes, &ulCurRes) != 0) {
		printf("NtQueryTimerResolution() function failed, aborting...\n");
		return -1;
	}

	switch (mode) {
	case eGamingType:
		ulSetRes = MSTO100NS(1);		// optimal for gaming and audio, timeBeginPeriod(1) analog
	case eDesktopType:
		ulSetRes = ulMinRes;			// optimal for office, browsing and other simple desktop things
	case eHighResolutionType:
		ulSetRes = ulMaxRes;			// optimal for IDK what
	case eNoneType:
	default:
		bTrue = FALSE;					// if we don't know what we want to set - don't set anything
		break;
	}

	if (NtSetTimerResolution(ulSetRes, bTrue, &ulCurRes) != 0) {
		printf("NtSetTimerResolution() function failed, aborting...\n");
		return -1;
	}

	return 0;
}

int
SetStartupTimerResolution()
{
	ULONG ulCurRes = 0;

	if (NtSetTimerResolution(g_ulCurRes, TRUE, &ulCurRes) != 0) {		// g_ulCurRes must be setted on startup
		printf("NtSetTimerResolution() function failed, aborting...\n");
		return -1;
	}

	printf("NtQueryTimerResolution(): \nNew resolution: %f\n", _100NSTOMS(g_ulCurRes));
	return 0;
}

void
PrintProcessorLoad()
{
	float* pLoad = NULL;
	int cpuCount = GetProcessorPerCoreLoad(&pLoad);
	cpuCount = cpuCount == -1 ? 0 : cpuCount;

	if (!pLoad) return;

	for (int i = 0; i < cpuCount; i++) {
		printf("Load of %i thread: %f\n", i, pLoad[i]);
	}
	printf("\n");
}

int
GetProcessorPerCoreLoad(float** pFloatLoad)
{
	int ret = -1;
	unsigned long long TickCount = 0;
	static unsigned long long GlobalTicks = 0;
	static unsigned long LocalCPUCount = 0;
	static float* pLoadArray = NULL;
	static PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION pCurrentUsages = NULL;
	static PLARGE_INTEGER pIdleTime = NULL;

	if (!LocalCPUCount) {
		SYSTEM_INFO sysInfo;
		GetNativeSystemInfo(&sysInfo);
		LocalCPUCount = sysInfo.dwNumberOfProcessors;
	}

	if (!pCurrentUsages) {
		pCurrentUsages = (PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)malloc(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * LocalCPUCount);
		if (!pCurrentUsages) {
			return ret;
		}
	}

	if (!pIdleTime) {
		pIdleTime = (PLARGE_INTEGER)malloc(sizeof(LARGE_INTEGER) * LocalCPUCount);
		if (!pIdleTime) {
			free(pCurrentUsages);
			pCurrentUsages = NULL;
			return ret;
		}
	}

	if (!pLoadArray) {
		pLoadArray = (float*)malloc(sizeof(float) * LocalCPUCount);
		if (!pLoadArray) {
			free(pIdleTime);
			free(pCurrentUsages);
			pIdleTime = NULL;
			pCurrentUsages = NULL;
			return ret;
		}
	}

	TickCount = GetTickCount64();
	if (!GlobalTicks) GlobalTicks = TickCount;
	GlobalTicks++;

	if (NtQuerySystemInformation(8, pCurrentUsages, sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * (ULONG)LocalCPUCount, NULL) != 0) {
		free(pLoadArray);
		free(pIdleTime);
		free(pCurrentUsages);
		pLoadArray = NULL;
		pIdleTime = NULL;
		pCurrentUsages = NULL;
		return ret;
	}

	for (int i = 0; i < LocalCPUCount; i++) {
		PSYSTEM_PROCESSOR_PERFORMANCE_INFORMATION pCpuPerformanceInfo = &pCurrentUsages[i];
		pLoadArray[i] = (float)(100.0f - 0.01f * (float)(pCpuPerformanceInfo->IdleTime.QuadPart - pIdleTime[i].QuadPart) / (float)(TickCount - GlobalTicks));
		memcpy(&pIdleTime[i], &pCpuPerformanceInfo->IdleTime, sizeof(LARGE_INTEGER));
	}

	*pFloatLoad = pLoadArray;
	GlobalTicks = TickCount;
	++GlobalTicks;
	return LocalCPUCount;
}

BOOL
SuspendProcess(
	HANDLE hProcess
)
{
	static PROCESSPROC pSuspendProcess = NULL;
	HMODULE hModule = GetModuleHandleA("ntdll");

	if (!IsInvalidHandle(hModule) && !IsInvalidHandle(hProcess)) {
		if (!pSuspendProcess) {
			pSuspendProcess = (PROCESSPROC)GetProcAddress(hModule, "NtSuspendProcess");
			if (!pSuspendProcess) return FALSE;
		}

		if (pSuspendProcess) return pSuspendProcess(hProcess) == 0;
	}
	return FALSE;
}

BOOL
ResumeProcess(
	HANDLE hProcess
)
{
	static PROCESSPROC pResumeProcess = NULL;
	HMODULE hModule = GetModuleHandleA("ntdll");

	if (!IsInvalidHandle(hModule) && !IsInvalidHandle(hProcess)) {
		if (!pResumeProcess) {
			pResumeProcess = (PROCESSPROC)GetProcAddress(hModule, "NtResumeProcess");
			if (!pResumeProcess) return FALSE;
		}

		if (pResumeProcess) return pResumeProcess(hProcess) == 0;
	}
	return FALSE;
}

void
TestFunc()
{
	HANDLE hProcess = GetCurrentProcess();
	HANDLE hNewProcess = NULL;
	if (!DuplicateHandle(hProcess, hProcess, hProcess, &hNewProcess, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		OutputDebugStringW(L"WARNING: Can't duplicate process handle\n");
		return;
	}

	if (GetProcessId(hNewProcess) == GetCurrentProcessId()) {
		OutputDebugStringW(L"The process handles have same ID\n");
	}
}

#define RELEASEKEY(Key) if (Key) RegCloseKey(Key);

/*
	export 138 - uxtheme.dll, Windows 10.18362.535
*/
BOOL
IsDarkTheme()
{
	HKEY hKey = NULL;
	DWORD Value1 = 0;
	DWORD Value2 = 0;
	DWORD size = 0;

	if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey)) return FALSE;
	if (RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&Value1, &size)) {
		if (RegQueryValueExA(hKey, "SystemUsesLightTheme", NULL, NULL, (LPBYTE)&Value2, &size)) {
			RELEASEKEY(hKey);
			return FALSE;
		}
	}

	RELEASEKEY(hKey);
	return !(Value1 || Value2);

	return FALSE;
}
