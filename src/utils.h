#ifndef __UTILS_H
#define __UTILS_H

#include <memory>
#include <string>
#include <vector>
#include <Windows.h>

#include <psapi.h>

typedef unsigned char byte;
typedef int qboolean;

enum filestreamState 
{
	notInitiated,
	failedInitiation,
	fullyInitiated
};

enum rewindState
{
	notRewinding,
	rewindOne,
	rewindAll
};

enum restoreState
{
	storeAddress,
	restoreAllAddresses,
	restoreOneAddress
};

class Utils
{
public:
	static bool Re_StoreBytesWrapper(uint32_t address, uint32_t size, restoreState mode);
	static void* TrampolineHook(byte* src, byte* dst, int len, bool stolenBytes);
	static bool WriteBytes(uint32_t address, std::string bytes, bool storeBytes);
	static DWORD SignatureScanner(const std::string& module, std::string signature);
	static bool FindCoD4xModule(std::string& module);

private:
	static bool Hook(byte* src, byte* dst, int size);
	static void ReplaceSubstring(std::string& str, const std::string& substr1, const std::string& substr2);
	static DWORD FindAddress(const MODULEINFO& Process, const std::unique_ptr<byte[]>& bytes, const std::vector<uint32_t>& maskPositions, uint32_t size);
	static bool RestoreBytes(std::tuple<uint32_t, std::unique_ptr<byte[]>, uint32_t>& tuple);
	static void StoreBytes(std::vector<std::tuple<uint32_t, std::unique_ptr<byte[]>, uint32_t>>& orgBytes, uint32_t address, uint32_t size);
};

typedef union qfile_gus 
{
	FILE* o;
	void* z;
} qfile_gut;

typedef struct qfile_us 
{
	qfile_gut file;
} qfile_ut;

typedef struct 
{
	qfile_ut handleFiles;
	qboolean handleSync;
	int	fileSize;
	int	zipFilePos;
	int	zipFileLen;
	qboolean zipFile;
	qboolean streamed;
	char name[256];
} fileHandleData_t;

struct customSnapshot
{
	int fileOffset = 0;
	int serverTime = 0;				// -> 0xC5F940
	int landTime = 0;				// -> 0x7975F8
	int clientConfigNumCoD4X = 0;	// -> 0x9562AC

	int parseEntitiesNum = 0;		// -> 0xC84F58 - global entity index
	int parseClientsNum = 0;		// -> 0xC84F5C - global client index
	char snapshots[12180 * 32]{};	// -> 0xCC9180 - 32 snapshots
	char entities[244 * 2048]{};	// -> 0xD65400 - 2048 entities
	char clients[100 * 2048]{};		// -> 0xDE5EA4 - 2048 clients

#ifdef enableChatRestoration
	int axisScore = 0;				// -> 0x79DBE8
	int alliesScore = 0;			// -> 0x79DBEC
	int serverCommandNum = 0;		// -> 0x7713D8
	int serverCommandSequence1 = 0;	// -> 0x74A91C
	int serverCommandSequence2 = 0;	// -> 0x914E20
	int serverCommandSequence3 = 0;	// -> 0x914E24
	char chat[1320]{};				// -> 0x74B798
#endif
#ifdef enableGamestateRestoration
	char gameState[140844]{};		// -> 0xC628EC
#endif
};

#endif