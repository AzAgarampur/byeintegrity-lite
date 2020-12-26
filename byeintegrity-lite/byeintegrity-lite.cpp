#include <Windows.h>
#include <ShlObj.h>
#include <iostream>
#include <string>

using UserAssocSetInternalPtr = HRESULT(WINAPI*)(void* unused0, PCWCHAR fileType, PCWCHAR progId, int unknown0);

const BYTE SIGNATURE_NT10[] = {
	0x48, 0x8B, 0xC4, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0xA0,
	0x00, 0x00, 0x00, 0x48, 0xC7, 0x45, 0xEF, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x20
};

template <typename T>
T LocateSignature(const BYTE signature[], const int signatureSize, const char* sectionName, const HMODULE moduleHandle)
{
	auto* headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<PUCHAR>(moduleHandle) + reinterpret_cast<
		PIMAGE_DOS_HEADER>(moduleHandle)->e_lfanew);
	auto* sectionHeader = IMAGE_FIRST_SECTION(headers);

	while (std::strcmp(sectionName, reinterpret_cast<char*>(sectionHeader->Name)) != 0)
		sectionHeader++;

	for (auto* i = reinterpret_cast<PUCHAR>(moduleHandle) + sectionHeader->PointerToRawData; i != reinterpret_cast<
		PUCHAR>(moduleHandle) + sectionHeader->PointerToRawData + sectionHeader->SizeOfRawData - signatureSize; i++
		)
	{
		if (std::memcmp(signature, i, signatureSize) == 0)
			return reinterpret_cast<T>(i);
	}

	return reinterpret_cast<T>(nullptr);
}

struct RegistryEntry
{
	explicit RegistryEntry(const wchar_t* path, const wchar_t* deletePath) : DeletePath(deletePath)
	{
		Status = RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, REG_OPTION_NON_VOLATILE,
			KEY_SET_VALUE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | DELETE, nullptr,
			&Handle, nullptr);
	}
	~RegistryEntry()
	{
		RegCloseKey(Handle);
		RegDeleteTreeW(HKEY_CURRENT_USER, DeletePath);
	}
	LSTATUS SetValue(const wchar_t* valueName, const PVOID valueData, const DWORD valueSize) const
	{
		return RegSetValueExW(Handle, valueName, 0, REG_SZ, static_cast<const BYTE*>(valueData), valueSize);
	}
	LSTATUS GetStatus() const
	{
		return Status;
	}
private:
	HKEY Handle{};
	LSTATUS Status;
	const wchar_t* DeletePath;
};

int main()
{
	PWSTR systemPath;
	auto hr = SHGetKnownFolderPath(FOLDERID_System, 0, nullptr, &systemPath);
	if (FAILED(hr))
	{
		std::wcout << L"SHGetKnownFolderPath() failed. HRESULT: 0x" << std::hex << hr << std::endl;
		return EXIT_FAILURE;
	}

	std::wstring cmdLoc{ systemPath };
	cmdLoc += L"\\cmd.exe";

	const auto UserAssocSetInternal = LocateSignature<UserAssocSetInternalPtr>(
		SIGNATURE_NT10, sizeof SIGNATURE_NT10, ".text",
		LoadLibraryExW(L"SystemSettings.Handlers.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
	if (!UserAssocSetInternal)
	{
		std::wcout << L"SystemSettings.Handlers.dll!UserAssocSet->\"Internal\" not found.\n";
		return EXIT_FAILURE;
	}

	const RegistryEntry progId{
		L"SOFTWARE\\Classes\\byeintegrity-lite\\shell\\open\\command", L"SOFTWARE\\Classes\\byeintegrity-lite"
	};
	if (progId.GetStatus())
	{
		std::wcout << L"RegCreateKeyExW() failed. LSTATUS: " << progId.GetStatus() << std::endl;
		return EXIT_FAILURE;
	}

	const auto status = progId.SetValue(nullptr, const_cast<wchar_t*>(cmdLoc.c_str()),
	                                    cmdLoc.size() * sizeof WCHAR + sizeof(L'\0'));
	if (status)
	{
		std::wcout << L"RegSetValueExW() failed. LSTATUS: " << status << std::endl;
		return EXIT_FAILURE;
	}

	hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE | COINIT_SPEED_OVER_MEMORY);
	if (FAILED(hr))
	{
		std::wcout << L"CoInitializeEx() failed. HRESULT: 0x" << std::hex << hr << std::endl;
		return EXIT_FAILURE;
	}
	
	UserAssocSetInternal(nullptr, L"ms-settings", L"byeintegrity-lite", 1);
	CoUninitialize();

	const auto shellResult = reinterpret_cast<int>(ShellExecuteW(nullptr, L"open", L"fodhelper.exe", nullptr, nullptr,
	                                                             SW_NORMAL));
	if (shellResult <= 32)
	{
		std::wcout << L"ShellExecuteW() failed. Return value: " << shellResult << std::endl;
		return EXIT_FAILURE;
	}

	return 0;
}