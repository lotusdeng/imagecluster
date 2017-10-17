#include <string>
#include <map>
#include <dokan/dokan.h>
#include <dokan/fileinfo.h>
#include <ewfreader/ewfreader.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <winbase.h>
#include <basecpp/Log.h>
#include <basecpp/StringConv.h>
#include <basecpp/HTTPClient.h>

//#define WIN10_ENABLE_LONG_PATH
#ifdef WIN10_ENABLE_LONG_PATH
//dirty but should be enough
#define DOKAN_MAX_PATH 32768
#else
#define DOKAN_MAX_PATH MAX_PATH
#endif // DEBUG

BOOL g_UseStdErr;
BOOL g_DebugMode;
BOOL g_HasSeSecurityPrivilege;

static void DbgPrint(LPCWSTR format, ...)
{
	if (g_DebugMode) {
		const WCHAR *outputString;
		WCHAR *buffer = NULL;
		size_t length;
		va_list argp;

		va_start(argp, format);
		length = _vscwprintf(format, argp) + 1;
		buffer = (WCHAR*)_malloca(length * sizeof(WCHAR));
		if (buffer) {
			vswprintf_s(buffer, length, format, argp);
			outputString = buffer;
		}
		else {
			outputString = format;
		}
		if (g_UseStdErr)
			fputws(outputString, stderr);
		else
			OutputDebugStringW(outputString);
		if (buffer)
			_freea(buffer);
		va_end(argp);
		if (g_UseStdErr)
			fflush(stderr);
	}
}

static WCHAR RootDirectory[DOKAN_MAX_PATH] = L"C:";
static WCHAR MountPoint[DOKAN_MAX_PATH] = L"M:\\";
static WCHAR UNCName[DOKAN_MAX_PATH] = L"";

static std::wstring sourceEwfFilepath;
static std::wstring destDdFilePath;
static std::wstring destDdFileName;

static void GetFilePath(PWCHAR filePath, ULONG numberOfElements,
	LPCWSTR FileName)
{
	wcsncpy_s(filePath, numberOfElements, RootDirectory, wcslen(RootDirectory));
	size_t unclen = wcslen(UNCName);
	if (unclen > 0 && _wcsnicmp(FileName, UNCName, unclen) == 0) {
		if (_wcsnicmp(FileName + unclen, L".", 1) != 0) {
			wcsncat_s(filePath, numberOfElements, FileName + unclen,
				wcslen(FileName) - unclen);
		}
	}
	else {
		wcsncat_s(filePath, numberOfElements, FileName, wcslen(FileName));
	}
}

static void PrintUserName(PDOKAN_FILE_INFO DokanFileInfo)
{
	HANDLE handle;
	UCHAR buffer[1024];
	DWORD returnLength;
	WCHAR accountName[256];
	WCHAR domainName[256];
	DWORD accountLength = sizeof(accountName) / sizeof(WCHAR);
	DWORD domainLength = sizeof(domainName) / sizeof(WCHAR);
	PTOKEN_USER tokenUser;
	SID_NAME_USE snu;

	handle = DokanOpenRequestorToken(DokanFileInfo);
	if (handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"  DokanOpenRequestorToken failed\n");
		return;
	}

	if (!GetTokenInformation(handle, TokenUser, buffer, sizeof(buffer),
		&returnLength)) {
		DbgPrint(L"  GetTokenInformaiton failed: %d\n", GetLastError());
		CloseHandle(handle);
		return;
	}

	CloseHandle(handle);

	tokenUser = (PTOKEN_USER)buffer;
	if (!LookupAccountSid(NULL, tokenUser->User.Sid, accountName, &accountLength,
		domainName, &domainLength, &snu)) {
		DbgPrint(L"  LookupAccountSid failed: %d\n", GetLastError());
		return;
	}

	DbgPrint(L"  AccountName: %s, DomainName: %s\n", accountName, domainName);
}

static BOOL AddSeSecurityNamePrivilege()
{
	LOG(debug) << "Attempting to add SE_SECURITY_NAME privilege to process token";
	HANDLE token = 0;

	DWORD err;
	LUID luid;
	if (!LookupPrivilegeValue(0, SE_SECURITY_NAME, &luid))
	{
		err = GetLastError();
		if (err != ERROR_SUCCESS)
		{
			LOG(error) << "failed: Unable to lookup privilege value. error:" << GetLastError();
			return FALSE;
		}
	}

	LUID_AND_ATTRIBUTES attr;
	attr.Attributes = SE_PRIVILEGE_ENABLED;
	attr.Luid = luid;

	TOKEN_PRIVILEGES priv;
	priv.PrivilegeCount = 1;
	priv.Privileges[0] = attr;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
	{
		err = GetLastError();
		if (err != ERROR_SUCCESS)
		{
			LOG(error) << "failed: Unable obtain process token. error:" << err;
			return FALSE;
		}
	}

	TOKEN_PRIVILEGES oldPriv;
	DWORD retSize;
	AdjustTokenPrivileges(token, FALSE, &priv, sizeof(TOKEN_PRIVILEGES), &oldPriv, &retSize);
	err = GetLastError();
	if (err != ERROR_SUCCESS)
	{
		LOG(error) << "failed: Unable to adjust token privileges, error:" << err;
		CloseHandle(token);
		return FALSE;
	}

	BOOL privAlreadyPresent = FALSE;
	for (unsigned int i = 0; i < oldPriv.PrivilegeCount; i++)
	{
		if (oldPriv.Privileges[i].Luid.HighPart == luid.HighPart &&
			oldPriv.Privileges[i].Luid.LowPart == luid.LowPart)
		{
			privAlreadyPresent = TRUE;
			break;
		}
	}

	LOG(debug) << (privAlreadyPresent ? "success: privilege already present" : "success: privilege added");
	if (token)
	{
		CloseHandle(token);
	}
	return TRUE;
}

#define MirrorCheckFlag(val, flag)                                             \
  if (val & flag) {                                                            \
    DbgPrint(L"\t" L#flag L"\n");                                              \
        }

static NTSTATUS DOKAN_CALLBACK MirrorCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext, ACCESS_MASK DesiredAccess,
	ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorCreateFile FileName:" << basecpp::toUTF8(FileName);

	NTSTATUS status = STATUS_SUCCESS;
	if (L"\\" + destDdFileName == FileName)
	{

		EwfHandle ewfHandle = nullptr;

		int ret = Ewf_Open(basecpp::toUTF8(sourceEwfFilepath).c_str(), &ewfHandle);
		if (ret != 0)
		{
			LOG(debug) << "Ewf_Open fail, filePath:" << basecpp::toUTF8(sourceEwfFilepath);
			return STATUS_OPEN_FAILED;
		}
		DokanFileInfo->Context = (uint64_t)ewfHandle;
		LOG(debug) << "MirrorCreateFile success, context:" << DokanFileInfo->Context;
	}

	return status;
}

#pragma warning(push)
#pragma warning(disable : 4305)

static void DOKAN_CALLBACK MirrorCloseFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorCloseFile FileName:" << basecpp::toUTF8(FileName) << ", context:" << DokanFileInfo->Context;
	if (L"\\" + destDdFileName != FileName)
	{
		LOG(error) << "MirrorCloseFile FileName is not equal destFileName";
		return;
	}
	if (DokanFileInfo->Context)
	{
		Ewf_Close((EwfHandle)DokanFileInfo->Context);
		DokanFileInfo->Context = 0;
	}
}

static void DOKAN_CALLBACK MirrorCleanup(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorCleanup FileName:" << basecpp::toUTF8(FileName) << ", context:" << DokanFileInfo->Context;
	return;
	if (DokanFileInfo->Context)
	{
		Ewf_Close((EwfHandle)DokanFileInfo->Context);
		DokanFileInfo->Context = 0;
	}
}

static NTSTATUS DOKAN_CALLBACK MirrorReadFile(LPCWSTR FileName, LPVOID Buffer, DWORD BufferLength,
	LPDWORD ReadLength, LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo)
{
	*ReadLength = 0;
	LOG(debug) << "MirrorReadFile  FileName:" << basecpp::toUTF8(FileName) << ", Offset:" << Offset << ", Buffer:" << Buffer
		<< ", BufferLength:" << BufferLength << ", context:" << DokanFileInfo->Context;
	if (L"\\" + destDdFileName != FileName)
	{
		LOG(error) << "MirrorReadFile FileName is not equal destFileName:" << basecpp::toUTF8(L"\\" + destDdFileName);
		return STATUS_OPEN_FAILED;
	}
	EwfHandle handle = (EwfHandle)DokanFileInfo->Context;
	if (handle != nullptr)
	{
		int64_t mediaSize = 0;
		Ewf_GetMediaSize(handle, &mediaSize);

		uint64_t offset = Offset;
		int64_t readedLen = 0;
		LOG(debug) << "MirrorReadFile mediaSize:" << mediaSize << ", offset:" << offset;
		int ret = Ewf_ReadMedia(handle, offset, Buffer, BufferLength, &readedLen);
		if (ret != 0)
		{
			LOG(error) << "MirrorReadFile Ewf_ReadMedia fail";
			return STATUS_OPEN_FAILED;
		}
		LOG(info) << "MirrorReadFile readedLen:" << readedLen;
		*ReadLength = readedLen;
	}

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorWriteFile(LPCWSTR FileName, LPCVOID Buffer, DWORD NumberOfBytesToWrite,
	LPDWORD NumberOfBytesWritten, LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorWriteFile FileName:" << basecpp::toUTF8(FileName);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorFlushFileBuffers(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorFlushFileBuffers FileName:" << basecpp::toUTF8(FileName);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorGetFileInformation(LPCWSTR FileName, LPBY_HANDLE_FILE_INFORMATION HandleFileInformation, PDOKAN_FILE_INFO DokanFileInfo) {
	LOG(debug) << "MirrorGetFileInformation FileName:" << basecpp::toUTF8(FileName) << ", context:" << DokanFileInfo->Context;
	if (L"\\" + destDdFileName != FileName)
	{
		LOG(error) << "MirrorGetFileInformation FileName is not equal destFileName:" << basecpp::toUTF8(L"\\" + destDdFileName);
		return STATUS_OPEN_FAILED;
	}
	LARGE_INTEGER fileSize;
	HANDLE srcFd = CreateFile(sourceEwfFilepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (srcFd)
	{
		GetFileSizeEx(srcFd, &fileSize);
		if (!GetFileInformationByHandle(srcFd, HandleFileInformation))
		{
			LOG(error) << "GetFileInformationByHandle fail, windows error:" << GetLastError();
		}
		CloseHandle(srcFd);
		HandleFileInformation->nFileSizeHigh = fileSize.HighPart;
		HandleFileInformation->nFileSizeLow = fileSize.LowPart;
	}
	else
	{
		LOG(error) << "MirrorGetFileInformation CreateFile fail, path:" << basecpp::toUTF8(sourceEwfFilepath);
	}

	EwfHandle handle = (EwfHandle)DokanFileInfo->Context;
	if (handle == nullptr)
	{
		return STATUS_SUCCESS;
	}
	int64_t destFileSize = 0;
	Ewf_GetMediaSize(handle, &destFileSize);
	HandleFileInformation->nFileSizeHigh = destFileSize >> 32;
	HandleFileInformation->nFileSizeLow = destFileSize & 0xffffffff;
	LOG(info) << "MirrorGetFileInformation fileSize:" << destFileSize;
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorFindFiles(LPCWSTR FileName, PFillFindData FillFindData, PDOKAN_FILE_INFO DokanFileInfo) {
	LOG(debug) << "MirrorFindFiles, FileName:" << basecpp::toUTF8(FileName) << ", MountPoint:" << DokanFileInfo->DokanOptions->MountPoint;

	WIN32_FIND_DATAW findData;

	HANDLE hFind = FindFirstFile(sourceEwfFilepath.c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		LOG(error) << "MirrorFindFiles FindFirstFile fail, error:" << GetLastError();
	}
	else
	{
		CloseHandle(hFind);
	}

	wcscpy_s(findData.cFileName, MAX_PATH, destDdFileName.c_str());
	//findData.nFileSizeHigh = 
	findData.dwFileAttributes = 33;
	int64_t destFileSize = 0;
	Ewf_GetMediaSize(basecpp::toUTF8(sourceEwfFilepath).c_str(), &destFileSize);
	findData.nFileSizeHigh = destFileSize >> 32;
	findData.nFileSizeLow = destFileSize & 0xffffffff;
	FillFindData(&findData, DokanFileInfo);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorDeleteFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorDeleteFile FileName:" << basecpp::toUTF8(FileName);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorDeleteDirectory(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorDeleteDirectory FileName:" << basecpp::toUTF8(FileName);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorMoveFile(LPCWSTR ExistFileName, LPCWSTR NewFileName, BOOL ReplaceIfExisting,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorMoveFile ExistFileName:" << basecpp::toUTF8(ExistFileName) << ", NewFileName:" << basecpp::toUTF8(ExistFileName);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorLockFile(LPCWSTR FileName, LONGLONG ByteOffset, LONGLONG Length,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorLockFile, FileName:" << basecpp::toUTF8(FileName);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetEndOfFile(LPCWSTR FileName, LONGLONG ByteOffset, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorSetEndOfFile FileName:" << basecpp::toUTF8(FileName) << ", ByteOffset:" << ByteOffset;
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetAllocationSize(LPCWSTR FileName, LONGLONG AllocSize, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorSetAllocationSize FileName:" << basecpp::toUTF8(FileName) << ", AllocSize:" << AllocSize;
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetFileAttributes(LPCWSTR FileName, DWORD FileAttributes, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorSetFileAttributes FileName:" << basecpp::toUTF8(FileName) << ", FileAttributes:" << FileAttributes;
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetFileTime(LPCWSTR FileName, CONST FILETIME *CreationTime,
	CONST FILETIME *LastAccessTime, CONST FILETIME *LastWriteTime, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorSetFileTime FileName:" << basecpp::toUTF8(FileName);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorUnlockFile(LPCWSTR FileName, LONGLONG ByteOffset,
	LONGLONG Length, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorUnlockFile FileName:" << basecpp::toUTF8(FileName);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorGetFileSecurity(LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
	PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG BufferLength, PULONG LengthNeeded, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorGetFileSecurity, FileName:" << basecpp::toUTF8(FileName);

	BOOLEAN requestingSaclInfo;

	UNREFERENCED_PARAMETER(DokanFileInfo);

	MirrorCheckFlag(*SecurityInformation, FILE_SHARE_READ);
	MirrorCheckFlag(*SecurityInformation, OWNER_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, GROUP_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, DACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, SACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, LABEL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, ATTRIBUTE_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, SCOPE_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation,
		PROCESS_TRUST_LABEL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, BACKUP_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, PROTECTED_DACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, PROTECTED_SACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, UNPROTECTED_DACL_SECURITY_INFORMATION);
	MirrorCheckFlag(*SecurityInformation, UNPROTECTED_SACL_SECURITY_INFORMATION);

	requestingSaclInfo = ((*SecurityInformation & SACL_SECURITY_INFORMATION) ||
		(*SecurityInformation & BACKUP_SECURITY_INFORMATION));

	if (!g_HasSeSecurityPrivilege) {
		*SecurityInformation &= ~SACL_SECURITY_INFORMATION;
		*SecurityInformation &= ~BACKUP_SECURITY_INFORMATION;
	}

	DbgPrint(L"  Opening new handle with READ_CONTROL access\n");
	HANDLE handle = CreateFile(
		sourceEwfFilepath.c_str(), READ_CONTROL | ((requestingSaclInfo && g_HasSeSecurityPrivilege)
		? ACCESS_SYSTEM_SECURITY
		: 0),
		FILE_SHARE_WRITE | FILE_SHARE_READ | FILE_SHARE_DELETE,
		NULL, // security attribute
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, // |FILE_FLAG_NO_BUFFERING,
		NULL);

	if (!handle || handle == INVALID_HANDLE_VALUE) {
		DbgPrint(L"\tinvalid handle\n\n");
		int error = GetLastError();
		return DokanNtStatusFromWin32(error);
	}

	if (!GetUserObjectSecurity(handle, SecurityInformation, SecurityDescriptor,
		BufferLength, LengthNeeded)) {
		int error = GetLastError();
		if (error == ERROR_INSUFFICIENT_BUFFER) {
			DbgPrint(L"  GetUserObjectSecurity error: ERROR_INSUFFICIENT_BUFFER\n");
			CloseHandle(handle);
			return STATUS_BUFFER_OVERFLOW;
		}
		else {
			DbgPrint(L"  GetUserObjectSecurity error: %d\n", error);
			CloseHandle(handle);
			return DokanNtStatusFromWin32(error);
		}
	}
	CloseHandle(handle);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorSetFileSecurity(LPCWSTR FileName, PSECURITY_INFORMATION SecurityInformation,
	PSECURITY_DESCRIPTOR SecurityDescriptor, ULONG SecurityDescriptorLength, PDOKAN_FILE_INFO DokanFileInfo) {
	LOG(debug) << "MirrorSetFileSecurity, FileName:" << basecpp::toUTF8(FileName);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorGetVolumeInformation(LPWSTR VolumeNameBuffer, DWORD VolumeNameSize, LPDWORD VolumeSerialNumber,
	LPDWORD MaximumComponentLength, LPDWORD FileSystemFlags, LPWSTR FileSystemNameBuffer, DWORD FileSystemNameSize,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorGetVolumeInformation";
	UNREFERENCED_PARAMETER(DokanFileInfo);

	wcscpy_s(VolumeNameBuffer, VolumeNameSize, L"DOKAN");
	*VolumeSerialNumber = 0x19831116;
	*MaximumComponentLength = 256;
	*FileSystemFlags = FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES |
		FILE_SUPPORTS_REMOTE_STORAGE | FILE_UNICODE_ON_DISK |
		FILE_PERSISTENT_ACLS;

	// File system name could be anything up to 10 characters.
	// But Windows check few feature availability based on file system name.
	// For this, it is recommended to set NTFS or FAT here.
	wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, L"NTFS");

	return STATUS_SUCCESS;
}

/*
//Uncomment for personalize disk space
static NTSTATUS DOKAN_CALLBACK MirrorDokanGetDiskFreeSpace(
PULONGLONG FreeBytesAvailable, PULONGLONG TotalNumberOfBytes,
PULONGLONG TotalNumberOfFreeBytes, PDOKAN_FILE_INFO DokanFileInfo) {
UNREFERENCED_PARAMETER(DokanFileInfo);

*FreeBytesAvailable = (ULONGLONG)(512 * 1024 * 1024);
*TotalNumberOfBytes = 9223372036854775807;
*TotalNumberOfFreeBytes = 9223372036854775807;

return STATUS_SUCCESS;
}
*/

/**
* Avoid #include <winternl.h> which as conflict with FILE_INFORMATION_CLASS
* definition.
* This only for MirrorFindStreams. Link with ntdll.lib still required.
*
* Not needed if you're not using NtQueryInformationFile!
*
* BEGIN
*/
#pragma warning(push)
#pragma warning(disable : 4201)
typedef struct _IO_STATUS_BLOCK {
	union {
		NTSTATUS Status;
		PVOID Pointer;
	} DUMMYUNIONNAME;

	ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
#pragma warning(pop)

NTSYSCALLAPI NTSTATUS NTAPI NtQueryInformationFile(
	_In_ HANDLE FileHandle, _Out_ PIO_STATUS_BLOCK IoStatusBlock,
	_Out_writes_bytes_(Length) PVOID FileInformation, _In_ ULONG Length,
	_In_ FILE_INFORMATION_CLASS FileInformationClass);
/**
* END
*/

NTSTATUS DOKAN_CALLBACK MirrorFindStreams(LPCWSTR FileName, PFillFindStreamData FillFindStreamData, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorFindStreams FileName:" << basecpp::toUTF8(FileName);
	WCHAR filePath[DOKAN_MAX_PATH];
	HANDLE hFind;
	WIN32_FIND_STREAM_DATA findData;
	DWORD error;
	int count = 0;

	GetFilePath(filePath, DOKAN_MAX_PATH, FileName);

	DbgPrint(L"FindStreams :%s\n", filePath);

	hFind = FindFirstStreamW(filePath, FindStreamInfoStandard, &findData, 0);

	if (hFind == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		DbgPrint(L"\tinvalid file handle. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	FillFindStreamData(&findData, DokanFileInfo);
	count++;

	while (FindNextStreamW(hFind, &findData) != 0) {
		FillFindStreamData(&findData, DokanFileInfo);
		count++;
	}

	error = GetLastError();
	FindClose(hFind);

	if (error != ERROR_HANDLE_EOF) {
		DbgPrint(L"\tFindNextStreamW error. Error is %u\n\n", error);
		return DokanNtStatusFromWin32(error);
	}

	DbgPrint(L"\tFindStreams return %d entries in %s\n\n", count, filePath);

	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorMounted(PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorMounted";
	UNREFERENCED_PARAMETER(DokanFileInfo);
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorUnmounted(PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorUnmounted";
	UNREFERENCED_PARAMETER(DokanFileInfo);
	return STATUS_SUCCESS;
}

#pragma warning(pop)

BOOL WINAPI CtrlHandler(DWORD dwCtrlType)
{
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		SetConsoleCtrlHandler(CtrlHandler, FALSE);
		DokanRemoveMountPoint(MountPoint);
		return TRUE;
	default:
		return FALSE;
	}
}

void ShowUsage() {
	// clang-format off
	fprintf(stderr, "mirror.exe\n"
		"  /r RootDirectory (ex. /r c:\\test)\t\t Directory source to mirror.\n"
		"  /l MountPoint (ex. /l m)\t\t\t Mount point. Can be M:\\ (drive letter) or empty NTFS folder C:\\mount\\dokan .\n"
		"  /t ThreadCount (ex. /t 5)\t\t\t Number of threads to be used internally by Dokan library.\n\t\t\t\t\t\t More threads will handle more event at the same time.\n"
		"  /d (enable debug output)\t\t\t Enable debug output to an attached debugger.\n"
		"  /s (use stderr for output)\t\t\t Enable debug output to stderr.\n"
		"  /n (use network drive)\t\t\t Show device as network device.\n"
		"  /m (use removable drive)\t\t\t Show device as removable media.\n"
		"  /w (write-protect drive)\t\t\t Read only filesystem.\n"
		"  /o (use mount manager)\t\t\t Register device to Windows mount manager.\n\t\t\t\t\t\t This enables advanced Windows features like recycle bin and more...\n"
		"  /c (mount for current session only)\t\t Device only visible for current user session.\n"
		"  /u (UNC provider name ex. \\localhost\\myfs)\t UNC name used for network volume.\n"
		"  /a Allocation unit size (ex. /a 512)\t\t Allocation Unit Size of the volume. This will behave on the disk file size.\n"
		"  /k Sector size (ex. /k 512)\t\t\t Sector Size of the volume. This will behave on the disk file size.\n"
		"  /f User mode Lock\t\t\t\t Enable Lockfile/Unlockfile operations. Otherwise Dokan will take care of it.\n"
		"  /i (Timeout in Milliseconds ex. /i 30000)\t Timeout until a running operation is aborted and the device is unmounted.\n\n"
		"Examples:\n"
		"\tmirror.exe /r C:\\Users /l M:\t\t\t# Mirror C:\\Users as RootDirectory into a drive of letter M:\\.\n"
		"\tmirror.exe /r C:\\Users /l C:\\mount\\dokan\t# Mirror C:\\Users as RootDirectory into NTFS folder C:\\mount\\dokan.\n"
		"\tmirror.exe /r C:\\Users /l M: /n /u \\myfs\\myfs1\t# Mirror C:\\Users as RootDirectory into a network drive M:\\. with UNC \\\\myfs\\myfs1\n\n"
		"Unmount the drive with CTRL + C in the console or alternatively via \"dokanctl /u MountPoint\".\n");
	// clang-format on
}

void initLog(const std::string& exePath, int port)
{
	std::string logFileName = boost::filesystem::path(exePath).filename().string();
	boost::algorithm::replace_last(logFileName, ".exe", "");
	logFileName = "log/" + logFileName + "_" + std::to_string(port);
	basecpp::AddRotationFileSink(logFileName.c_str(), model::AppConfSingleton::get_const_instance().fileLogRotationMBSize_,
		model::AppConfSingleton::get_const_instance().fileLogAutoFlush_);

	basecpp::SetRotationFileSinkLogLevel(model::AppConfSingleton::get_const_instance().fileLogLevel_.c_str());
	basecpp::SetConsoleSinkLogLevel(model::AppConfSingleton::get_const_instance().consoleLogLevel_.c_str());
}

int __cdecl wmain(ULONG argc, PWCHAR argv[])
{
	std::vector<std::string> args;
	for (int i = 0; i < argc; ++i)
	{
		args.push_back(basecpp::toUTF8(argv[i]));
	}

	std::string logFilePath = "log/ewf2dd.txt";
	boostlog::SetCoreLogLevel("debug");
	boostlog::AddConsoleSink();
	boostlog::AddFileSink(logFilePath.c_str());
	LOG(debug) << "ewf2dd, 版本:" << "1.0.0" << ", 编译时间:" << __DATE__ << " " << __TIME__ << ", 进程ID:" << GetCurrentProcessId();
	LOG(debug) << boost::algorithm::join(args, " ");
	int status;
	ULONG command;
	PDOKAN_OPERATIONS dokanOperations = (PDOKAN_OPERATIONS)malloc(sizeof(DOKAN_OPERATIONS));
	if (dokanOperations == NULL) {
		return EXIT_FAILURE;
	}
	PDOKAN_OPTIONS dokanOptions = (PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS));
	if (dokanOptions == NULL) {
		free(dokanOperations);
		return EXIT_FAILURE;
	}

	if (argc < 3) {
		ShowUsage();
		free(dokanOperations);
		free(dokanOptions);
		return EXIT_FAILURE;
	}

	g_DebugMode = FALSE;
	g_UseStdErr = FALSE;

	ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));
	dokanOptions->Version = DOKAN_VERSION;
	dokanOptions->ThreadCount = 0; // use default

	sourceEwfFilepath = argv[1];
	destDdFilePath = argv[2];
	std::wstring destDdDirPath = boost::filesystem::path(destDdFilePath).parent_path().wstring();
	destDdFileName = boost::filesystem::path(destDdFilePath).filename().wstring();
	boost::algorithm::to_upper(destDdFileName);
	wcscpy_s(MountPoint, sizeof(MountPoint) / sizeof(WCHAR), destDdDirPath.c_str());
	dokanOptions->MountPoint = MountPoint;
	LOG(debug) << "MountPoint:" << basecpp::toUTF8(destDdDirPath);

	if (!SetConsoleCtrlHandler(CtrlHandler, TRUE))
	{
		LOG(error) << "Control Handler is not set.";
	}

	// Add security name privilege. Required here to handle GetFileSecurity
	// properly.
	g_HasSeSecurityPrivilege = AddSeSecurityNamePrivilege();
	if (!g_HasSeSecurityPrivilege)
	{
		fwprintf(stderr, L"Failed to add security privilege to process\n");
		fwprintf(stderr, L"\t=> GetFileSecurity/SetFileSecurity may not work properly\n");
		fwprintf(stderr, L"\t=> Please restart mirror sample with administrator rights to fix it\n");
	}

	if (g_DebugMode) {
		dokanOptions->Options |= DOKAN_OPTION_DEBUG;
	}
	if (g_UseStdErr) {
		dokanOptions->Options |= DOKAN_OPTION_STDERR;
	}

	dokanOptions->Options |= DOKAN_OPTION_ALT_STREAM;

	ZeroMemory(dokanOperations, sizeof(DOKAN_OPERATIONS));
	dokanOperations->ZwCreateFile = MirrorCreateFile;
	dokanOperations->Cleanup = MirrorCleanup;
	dokanOperations->CloseFile = MirrorCloseFile;
	dokanOperations->ReadFile = MirrorReadFile;
	dokanOperations->WriteFile = MirrorWriteFile;
	dokanOperations->FlushFileBuffers = MirrorFlushFileBuffers;
	dokanOperations->GetFileInformation = MirrorGetFileInformation;
	dokanOperations->FindFiles = MirrorFindFiles;
	dokanOperations->FindFilesWithPattern = NULL;
	dokanOperations->SetFileAttributes = MirrorSetFileAttributes;
	dokanOperations->SetFileTime = MirrorSetFileTime;
	dokanOperations->DeleteFile = MirrorDeleteFile;
	dokanOperations->DeleteDirectory = MirrorDeleteDirectory;
	dokanOperations->MoveFile = MirrorMoveFile;
	dokanOperations->SetEndOfFile = MirrorSetEndOfFile;
	dokanOperations->SetAllocationSize = MirrorSetAllocationSize;
	dokanOperations->LockFile = MirrorLockFile;
	dokanOperations->UnlockFile = MirrorUnlockFile;
	dokanOperations->GetFileSecurity = MirrorGetFileSecurity;
	dokanOperations->SetFileSecurity = MirrorSetFileSecurity;
	dokanOperations->GetDiskFreeSpace = NULL; // MirrorDokanGetDiskFreeSpace;
	dokanOperations->GetVolumeInformation = MirrorGetVolumeInformation;
	dokanOperations->Unmounted = MirrorUnmounted;
	dokanOperations->FindStreams = MirrorFindStreams;
	dokanOperations->Mounted = MirrorMounted;

	std::map<int, std::string> dokanMainRets = {
		{ DOKAN_SUCCESS, "DOKAN_SUCCESS" },
		{ DOKAN_ERROR, "DOKAN_ERROR" },
		{ DOKAN_DRIVE_LETTER_ERROR, "DOKAN_DRIVE_LETTER_ERROR" },
		{ DOKAN_DRIVER_INSTALL_ERROR, "DOKAN_DRIVER_INSTALL_ERROR" },
		{ DOKAN_START_ERROR, "DOKAN_START_ERROR" },
		{ DOKAN_MOUNT_ERROR, "DOKAN_MOUNT_ERROR" },
		{ DOKAN_MOUNT_POINT_ERROR, "DOKAN_MOUNT_POINT_ERROR" },
		{ DOKAN_VERSION_ERROR, "DOKAN_VERSION_ERROR" }
	};

	status = DokanMain(dokanOptions, dokanOperations);
	try
	{
		LOG(debug) << "DokanMain return:" << status << ", msg:" << dokanMainRets.at(status);
	}
	catch (...)
	{
		LOG(debug) << "DokanMain return:" << status;
	}




	free(dokanOptions);
	free(dokanOperations);
	return EXIT_SUCCESS;
}
