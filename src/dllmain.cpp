#define enableChatRestoration
#define enableGamestateRestoration

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <fstream>
#include <stack>
#include <thread>
#include <unordered_map>
#include <Windows.h>
#include "utils.h"

int rewindTime = 1000;
int rewindOneButton = VK_RCONTROL;
int rewindAllButton = VK_RMENU;
int ejectDllButton = VK_RSHIFT;
std::string cod4x = "cod4x_019.dll"; // it doesn't matter what this is set to, but choosing the correct one is more efficient

typedef int(_cdecl* FS_Read)(void* buffer, int len, int f);
FS_Read FS_ReadOrg;

std::ifstream file;
std::stack<customSnapshot> frameData;
std::array<int, 20> serverTimes;
filestreamState fileStreamMode = notInitiated;
rewindState rewindMode = notRewinding;
int snapshotCount = 0;
int countBuffer = 0;

void CL_FirstSnapshotWrapper()
{
	int clientNum = *reinterpret_cast<int*>(0x74E338);
	DWORD CL_FirstSnapshot = *(reinterpret_cast<DWORD*>(0x45C49A + 0x1)) + 0x45C49A + 0x5;

	Utils::WriteBytes(0x45C2A7, "90 90 90 90 90", true); // to avoid some division by 0 bug on CoD4; not necessary for CoD4X

	_asm 
	{
		pushad
		mov eax, clientNum
		call CL_FirstSnapshot
		popad
	}

	Utils::Re_StoreBytesWrapper(0x45C2A7, 0, restoreOneAddress);
}

void ResetServerTimes(int serverTime)
{
	*reinterpret_cast<int*>(0xC5F940) = serverTime;	// cl.snap.serverTime
	*reinterpret_cast<int*>(0xC628D0) = 0;			// cl.serverTime
	*reinterpret_cast<int*>(0xC628D4) = 0;			// cl.oldServerTime
	*reinterpret_cast<int*>(0xC628DC) = 0;			// cl.serverTimeDelta

	*reinterpret_cast<int*>(0x934E8C) = 0;			// clc.timeDemoFrames
	*reinterpret_cast<int*>(0x934E90) = 0;			// clc.timeDemoStart
	*reinterpret_cast<int*>(0x934E94) = 0;			// clc.timeDemoBaseTime
	*reinterpret_cast<int*>(0x956E98) = 0;			// cls.realtime
	*reinterpret_cast<int*>(0x956E9C) = 0;			// cls.realFrametime

	*reinterpret_cast<int*>(0x74A920) = 0;
	*reinterpret_cast<int*>(0x74E350) = 0;			// cg_t -> latestSnapshotNum
	*reinterpret_cast<int*>(0x74E354) = 0;			// cg_t -> latestSnapshotTime
	*reinterpret_cast<int*>(0x74E358) = 0;			// cg_t -> snap
	*reinterpret_cast<int*>(0x74E35C) = 0;			// cg_t -> nextSnap
}

void RestoreOldGamestate()
{
	if (frameData.size() > 1) {
		frameData.pop();

		if (rewindMode == rewindAll) {
			while (frameData.size() > 1)
				frameData.pop();
		}

		rewindMode = notRewinding;
		serverTimes = std::array<int, 20>();
		countBuffer = frameData.top().fileOffset;
		file.seekg(countBuffer);

		*reinterpret_cast<int*>(0x8EEB50) = 0;										// clean mini console / kill feed
		memset(reinterpret_cast<char*>(0x742B20), 0, 64 * 48);						// uav data; 64 players, 48 bytes per player

		ResetServerTimes(frameData.top().serverTime);
		CL_FirstSnapshotWrapper();

		*reinterpret_cast<int*>(0x7975F8) = frameData.top().landTime;				// needed to prevent the player's viewmodel from disappearing off screen 
		*reinterpret_cast<int*>(0x9562AC) = frameData.top().clientConfigNumCoD4X;	// only needed for CoD4X demos, client config data parseNum

		*reinterpret_cast<int*>(0xC84F58) = frameData.top().parseEntitiesNum;
		*reinterpret_cast<int*>(0xC84F5C) = frameData.top().parseClientsNum;
		memcpy(reinterpret_cast<char*>(0xCC9180), &frameData.top().snapshots, sizeof(customSnapshot{}.snapshots));
		memcpy(reinterpret_cast<char*>(0xD65400), &frameData.top().entities, sizeof(customSnapshot{}.entities));
		memcpy(reinterpret_cast<char*>(0xDE5EA4), &frameData.top().clients, sizeof(customSnapshot{}.clients));

#ifdef enableChatRestoration
		*reinterpret_cast<int*>(0x79DBE8) = frameData.top().axisScore;
		*reinterpret_cast<int*>(0x79DBEC) = frameData.top().alliesScore;
		*reinterpret_cast<int*>(0x7713D8) = frameData.top().serverCommandNum;
		*reinterpret_cast<int*>(0x74A91C) = frameData.top().serverCommandSequence1;
		*reinterpret_cast<int*>(0x914E20) = frameData.top().serverCommandSequence2;
		*reinterpret_cast<int*>(0x914E24) = frameData.top().serverCommandSequence3;
		memcpy(reinterpret_cast<char*>(0x74B798), &frameData.top().chat, sizeof(customSnapshot{}.chat));
#endif
#ifdef enableGamestateRestoration
		memcpy(reinterpret_cast<char*>(0xC628EC), &frameData.top().gameState, sizeof(customSnapshot{}.gameState));
#endif
	}
}

void StoreCurrentGamestate(int fps)
{
	// this function is executed in a hook that precedes snapshot parsing; so the data must be stored in the most recent entry on the std::stack / frameData
	if ((fps && snapshotCount > 4 && (snapshotCount - 1) % (rewindTime / fps) == 0) || snapshotCount == 4) {
		if (frameData.size() && frameData.top().fileOffset && !frameData.top().serverTime) {
			frameData.top().serverTime = *reinterpret_cast<int*>(0xC5F940);
			frameData.top().landTime = *reinterpret_cast<int*>(0x7975F8);
			frameData.top().clientConfigNumCoD4X = *reinterpret_cast<int*>(0x9562AC);

			frameData.top().parseEntitiesNum = *reinterpret_cast<int*>(0xC84F58);
			frameData.top().parseClientsNum = *reinterpret_cast<int*>(0xC84F5C);
			memcpy(&frameData.top().snapshots, reinterpret_cast<char*>(0xCC9180), sizeof(customSnapshot{}.snapshots));
			memcpy(&frameData.top().entities, reinterpret_cast<char*>(0xD65400), sizeof(customSnapshot{}.entities));
			memcpy(&frameData.top().clients, reinterpret_cast<char*>(0xDE5EA4), sizeof(customSnapshot{}.clients));

#ifdef enableChatRestoration
			frameData.top().axisScore = *reinterpret_cast<int*>(0x79DBE8);
			frameData.top().alliesScore = *reinterpret_cast<int*>(0x79DBEC);
			frameData.top().serverCommandNum = *reinterpret_cast<int*>(0x7713D8);
			frameData.top().serverCommandSequence1 = *reinterpret_cast<int*>(0x74A91C);
			frameData.top().serverCommandSequence2 = *reinterpret_cast<int*>(0x914E20);
			frameData.top().serverCommandSequence3 = *reinterpret_cast<int*>(0x914E24);
			memcpy(&frameData.top().chat, reinterpret_cast<char*>(0x74B798), sizeof(customSnapshot{}.chat));
#endif
#ifdef enableGamestateRestoration
			memcpy(&frameData.top().gameState, reinterpret_cast<char*>(0xC628EC), sizeof(customSnapshot{}.gameState));
#endif
		}
	}

	// don't start storing snapshots before the third one to avoid some glitch with the chat (some old chat would be restored with the first snapshot)
	if ((fps && snapshotCount > 3 && snapshotCount % (rewindTime / fps) == 0) || snapshotCount == 3) { 
		frameData.emplace();
		frameData.top().fileOffset = countBuffer - 9; // to account for message type (byte), server message index (int) and message size (int)
	}
}

int DetermineFramerate(int serverTime)
{
	// this function stores a small number of server times, and calculates the server framerate
	if (!serverTime) 
		return 0;

	if (serverTimes[(snapshotCount + serverTimes.size() - 1) % serverTimes.size()] > serverTime) {
		serverTimes = std::array<int, serverTimes.size()>();
		serverTimes[snapshotCount % serverTimes.size()] = serverTime;
	}
	else {
		serverTimes[snapshotCount % serverTimes.size()] = serverTime;
		std::unordered_map<int, int> frequency;

		for (int i = snapshotCount; i < static_cast<int>(snapshotCount + serverTimes.size()); ++i) {
			int temp = serverTimes[i % serverTimes.size()] - serverTimes[((i + serverTimes.size() - 1) % serverTimes.size())];

			if (temp >= 0 && temp <= 1000) {
				//if (temp % 50 == 0)
					frequency[temp]++;
			}
		}

		auto highestFrequency = std::max_element(
			std::begin(frequency), std::end(frequency), [](const auto& a, const auto& b) {
				return a.second < b.second;
			});

		return highestFrequency->first;
	}

	return 0;
}

bool SetupFileStream(char* fileName)
{
	std::string filePath = fileName;

	if (filePath[1] != ':') { // full path should be something like 'X:\'
		filePath = reinterpret_cast<char*>(0xCB1A9B8); // game base directory
		char* modName = reinterpret_cast<char*>(0xCB1989D);

		if (!*modName)
			filePath += "\\" + static_cast<std::string>(fileName);
		else
			filePath += "\\Mods\\" + static_cast<std::string>(modName) + "\\" + static_cast<std::string>(fileName);
	}

	file.open(filePath.c_str(), std::ios::binary);

	return file.is_open();
}

int hkFS_Read(void* buffer, int len, int f)
{
	fileHandleData_t fh = *reinterpret_cast<fileHandleData_t*>(0xCB1DCC8 + f * sizeof(fileHandleData_t));

	if (strstr(fh.name, ".dm_1")) {
		if (fileStreamMode == notInitiated) {
			if (!SetupFileStream(fh.name))
				fileStreamMode = failedInitiation;
			else
				fileStreamMode = fullyInitiated;
		}

		if (fileStreamMode != fullyInitiated)
			return FS_ReadOrg(buffer, len, f);

		if (len == 1 && rewindMode) // only reset when the game has just requested the one byte message type
			RestoreOldGamestate();
		else if (len > 12) { // to exclude client archives (and CoD4X protocol header)
			snapshotCount++;
			StoreCurrentGamestate(DetermineFramerate(*reinterpret_cast<int*>(0xD90BCF8)));
		}

		file.read(reinterpret_cast<char*>(buffer), len);
		countBuffer += len;

		assert(countBuffer == file.tellg());
		return len;	
	}
	else
		return FS_ReadOrg(buffer, len, f);
}

void WaitForInput()
{
	DWORD demoPlaying = 0x934E74;

	while (true) {
		if (!*reinterpret_cast<byte*>(demoPlaying) && fileStreamMode == fullyInitiated) {
			fileStreamMode = notInitiated;

			rewindMode = notRewinding;
			snapshotCount = 0;
			countBuffer = 0;
			serverTimes = std::array<int, serverTimes.size()>();
			frameData = std::stack<customSnapshot>();

			file.close();
		}

		if ((GetAsyncKeyState(rewindOneButton) & 0x01)) 
			rewindMode = rewindOne;
		else if ((GetAsyncKeyState(rewindAllButton) & 0x01))
			rewindMode = rewindAll;
		else if ((GetAsyncKeyState(ejectDllButton) & 0x01)) {
			MessageBeep(0);
			Utils::Re_StoreBytesWrapper(0, 0, restoreAllAddresses);

			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

DWORD APIENTRY MainThread(HMODULE hModule)
{
	Utils::WriteBytes(0x44E2C3, "EB", true); // bypass error: WARNING: CG_ReadNextSnapshot: way out of range, %i > %i\n
	Utils::WriteBytes(0x44E4B0, "EB", true); // bypass error: CG_ProcessSnapshots: Server time went backwards
	Utils::WriteBytes(0x45C511, "90 90", true); // bypass error: cl->snap.serverTime < cl->oldFrameServerTime

	DWORD FS_ReadAddress = 0x55C120;

	if (*reinterpret_cast<byte*>(FS_ReadAddress) == 0xE9) { // CoD4X
		FS_ReadAddress = *(reinterpret_cast<DWORD*>(FS_ReadAddress + 0x1)) + FS_ReadAddress + 0x5;
		FS_ReadOrg = static_cast<FS_Read>(Utils::TrampolineHook(reinterpret_cast<byte*>(FS_ReadAddress), reinterpret_cast<byte*>(&hkFS_Read), 6, true)); 

		if (Utils::FindCoD4xModule(cod4x)) {
			DWORD CL_CGameNeedsServerCommand = Utils::SignatureScanner(cod4x, "B8 E0 4C 8F 00 8B 80 40 01 02 00 ?? ?? ?? ?? ?? C7");

			if (CL_CGameNeedsServerCommand)
				Utils::WriteBytes(CL_CGameNeedsServerCommand + 14, "EB", true); // bypass error: CL_CGameNeedsServerCommand: requested a command not received
		}
	}
	else { // CoD4 v1.7
		FS_ReadOrg = static_cast<FS_Read>(Utils::TrampolineHook(reinterpret_cast<byte*>(FS_ReadAddress), reinterpret_cast<byte*>(&hkFS_Read), 7, true)); 

		Utils::WriteBytes(0x45B01A, "EB", true); // bypass error: CL_CGameNeedsServerCommand: EXE_ERR_NOT_RECEIVED
	}

	WaitForInput();

	FreeLibraryAndExitThread(hModule, 0);
	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(MainThread), hModule, 0, nullptr);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }

    return TRUE;
}