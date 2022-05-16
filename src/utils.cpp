#include "utils.h"

#include <algorithm>
#include <tuple>

bool Utils::RestoreBytes(const std::tuple<uint32_t, std::unique_ptr<byte[]>, uint32_t>& tuple)
{
	DWORD curProtection;

	if (!VirtualProtect(reinterpret_cast<LPVOID>(std::get<0>(tuple)), std::get<2>(tuple), PAGE_EXECUTE_READWRITE, &curProtection))
		return false;

	if (!memcpy(reinterpret_cast<byte*>(std::get<0>(tuple)), std::get<1>(tuple).get(), std::get<2>(tuple)))
		return false;

	if (!VirtualProtect(reinterpret_cast<LPVOID>(std::get<0>(tuple)), std::get<2>(tuple), curProtection, &curProtection))
		return false;

	return true;
}

void Utils::StoreBytes(std::vector<std::tuple<uint32_t, std::unique_ptr<byte[]>, uint32_t>>& orgBytes, uint32_t address, uint32_t size)
{
	std::unique_ptr<byte[]> bytes = std::make_unique<byte[]>(size);
	memcpy(bytes.get(), reinterpret_cast<byte*>(address), size);

	orgBytes.push_back({ address, std::move(bytes), size });
}

bool Utils::Re_StoreBytesWrapper(uint32_t address, uint32_t size, uint32_t mode)
{
	static std::vector<std::tuple<uint32_t, std::unique_ptr<byte[]>, uint32_t>> orgBytes;

	if (mode == storeAddress)
		StoreBytes(orgBytes, address, size);
	else if (mode == restoreAllAddresses) {
		for (const auto& tuple : orgBytes) 
			RestoreBytes(tuple);

		orgBytes.clear();
	}
	else {
		for (uint32_t i = 0; i < orgBytes.size(); ++i) {
			if (address == std::get<0>(orgBytes[i])) {
				RestoreBytes(orgBytes[i]);

				orgBytes.erase(orgBytes.begin() + i);
				return true;
			}
		}

		return false;
	}

	return true;
}

bool Utils::WriteBytes(uint32_t address, std::string byteString, bool storeBytes)
{
	DWORD curProtection;

	byteString.erase(std::remove(byteString.begin(), byteString.end(), ' '), byteString.end());
	uint32_t size = byteString.length() / 2;

	if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE, &curProtection))
		return false;

	if (storeBytes) {
		if (!Re_StoreBytesWrapper(address, size, storeAddress))
			return false;
	}

	std::unique_ptr<byte[]> bytes = std::make_unique<byte[]>(size);

	for (uint32_t i = 0, count = 0; i < byteString.length(); ++count, i += 2)
		bytes.get()[count] = static_cast<byte>(strtol(byteString.substr(i, 2).c_str(), nullptr, 16));

	if (!memcpy(reinterpret_cast<byte*>(address), bytes.get(), size))
		return false;

	if (!VirtualProtect(reinterpret_cast<LPVOID>(address), size, curProtection, &curProtection))
		return false;

	return true;
}

bool Utils::Hook(byte* src, byte* dst, int size)
{
	DWORD curProtection;

	if (!VirtualProtect(src, size, PAGE_EXECUTE_READWRITE, reinterpret_cast<PDWORD>(&curProtection)))
		return false;

	DWORD relativeAddress = (reinterpret_cast<DWORD>(dst) - reinterpret_cast<DWORD>(src)) - 5;
	*src = 0xE9;
	*reinterpret_cast<DWORD*>(reinterpret_cast<DWORD>(src) + 1) = relativeAddress;

	if (!VirtualProtect(src, size, curProtection, reinterpret_cast<PDWORD>(&curProtection)))
		return false;

	return true;
}

void* Utils::TrampolineHook(byte* src, byte* dst, int size, bool stolenBytes)
{
	if (size < 5) 
		return 0;

	int offset1 = 0;
	int offset2 = 0;
	void* gateway = VirtualAlloc(0, size + 8, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

	if (!gateway || !Re_StoreBytesWrapper(reinterpret_cast<uint32_t>(src), size, storeAddress))
		return 0;

	if (stolenBytes) {
		memcpy(gateway, src, size);

		if (*src == 0xE8 || *src == 0xE9) {
			void* gatewayTemp = static_cast<char*>(gateway) + 1;
			DWORD relativeAddress = *reinterpret_cast<DWORD*>(src + 1) + reinterpret_cast<DWORD>(src) + 5;
			relativeAddress -= (reinterpret_cast<int>(gatewayTemp) - 1 + 5);

			memcpy(gatewayTemp, &relativeAddress, sizeof(DWORD));
		}

		offset1 = size;
		offset2 = 5;
	}

	DWORD gatewayRelativeAddress = (reinterpret_cast<DWORD>(src) - reinterpret_cast<DWORD>(gateway)) - offset2;
	*reinterpret_cast<byte*>(reinterpret_cast<DWORD>(gateway) + offset1) = 0xE9;
	*reinterpret_cast<DWORD*>(reinterpret_cast<DWORD>(gateway) + offset1 + 1) = gatewayRelativeAddress;

	if (!Hook(src, dst, size))
		return 0;

	return gateway;
}

DWORD Utils::FindAddress(const MODULEINFO& Process, const std::unique_ptr<byte[]>& bytes, const std::vector<uint32_t>& maskPositions, uint32_t size)
{
	for (uint32_t i = reinterpret_cast<uint32_t>(Process.lpBaseOfDll); i < reinterpret_cast<uint32_t>(Process.lpBaseOfDll) + Process.SizeOfImage; ++i) {
		if (*reinterpret_cast<byte*>(i) == bytes.get()[0]) {
			DWORD Output = i;

			for (uint32_t j = 1; j < size; ++j) {
				for (uint32_t k = 0; k < maskPositions.size(); ++k) {
					if (j + 1 < size && j == maskPositions[k])
						j++;
				}

				if (*reinterpret_cast<byte*>(i + j) != bytes.get()[j])
					break;

				if (j + 1 == size)
					return Output;
			}
		}
	}

	return 0;
}

DWORD Utils::SignatureScanner(const std::string& module, std::string signature)
{
	MODULEINFO Process{};

	if (!GetModuleInformation(GetCurrentProcess(), GetModuleHandle(module.c_str()), &Process, sizeof(Process)) || !Process.lpBaseOfDll)
		return 0;

	ReplaceSubstring(signature, " ? ", " ?? ");
	ReplaceSubstring(signature, " ", "");
	ReplaceSubstring(signature, ",", "");
	ReplaceSubstring(signature, "\\", "");
	ReplaceSubstring(signature, "0x", "");
	
	while (signature.length() && signature.back() == '?')
		signature.resize(signature.length() - 1);

	uint32_t size = signature.length() / 2;

	std::unique_ptr<byte[]> bytes = std::make_unique<byte[]>(size);
	std::vector<uint32_t> maskPositions;

	for (uint32_t count = 0, i = 0; i < signature.length(); ++count, i += 2) {
		bytes.get()[count] = static_cast<byte>(strtol(signature.substr(i, 2).c_str(), nullptr, 16));

		if (signature.substr(i, 2) == "??") 
			maskPositions.push_back(i / 2);
	}

	return FindAddress(Process, bytes, maskPositions, size);
}

bool Utils::FindCoD4xModule(std::string& module)
{
	MODULEINFO Process{};

	for (int i = 0; i < 1000; ++i) {
		if (!GetModuleInformation(GetCurrentProcess(), GetModuleHandle(module.c_str()), &Process, sizeof(Process)) || Process.lpBaseOfDll)
			break;

		if (i < 10)
			module = "cod4x_00" + std::to_string(i) + ".dll";
		else if (i < 100)
			module = "cod4x_0" + std::to_string(i) + ".dll";
		else
			module = "cod4x_" + std::to_string(i) + ".dll";
	}

	return Process.lpBaseOfDll;
}

void Utils::ReplaceSubstring(std::string& str, const std::string& substr1, const std::string& substr2)
{
	for (size_t index = str.find(substr1, 0); index != std::string::npos && substr1.length(); index = str.find(substr1, index + substr2.length()))
		str.replace(index, substr1.length(), substr2);
}
