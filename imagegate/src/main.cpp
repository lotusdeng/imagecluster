#include <string>
#include <map>
#include <dokan/dokan.h>
#include <dokan/fileinfo.h>
#include <ewfreader/ewfreader.h>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <winbase.h>
#include <basecpp/Log.h>
#include <basecpp/StringConv.h>
#include <basecpp/HTTPClient.h>
#include "model/AppConf.h"
#include "ImageCenter.h"

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
basecpp::HTTPClient gHttpClient;

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
static WCHAR gMountPoint[DOKAN_MAX_PATH] = L"M:\\";
static WCHAR UNCName[DOKAN_MAX_PATH] = L"";

static std::wstring sourceEwfFilepath;
static std::wstring destDdFilePath;
static std::wstring destDdFileName;


struct FileHandle
{
	bool isRaw_;
	int64_t size_ = 0;
	int64_t sizeInVolume_ = 0;
	FILE* rawFd_ = nullptr;
	EwfHandle ewfFd_ = nullptr;
};

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
	LOG(info) << "Attempting to add SE_SECURITY_NAME privilege to process token";
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

	LOG(info) << (privAlreadyPresent ? "success: privilege already present" : "success: privilege added");
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

static NTSTATUS DOKAN_CALLBACK MirrorCreateFile(LPCWSTR FileName, PDOKAN_IO_SECURITY_CONTEXT SecurityContext, 
	ACCESS_MASK DesiredAccess, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorCreateFile FileName:" << basecpp::toUTF8(FileName);

	NTSTATUS status = STATUS_SUCCESS;
	//DokanFileInfo->Context = (uint64_t)ewfHandle;
	if (wcscmp(FileName, L"\\") != 0)
	{
		if (FileAttributes&FILE_ATTRIBUTE_DIRECTORY)
		{
		}
		else
		{
			ImageGetRep imageGetRep;
			listImageInImageCenter(basecpp::toUTF8(FileName), model::AppConfSingleton::get_const_instance().onlyShowActive_,
				imageGetRep);
			LOG(info) << "child file count:" << imageGetRep.items.size();
			if (imageGetRep.items.size() == 1)
			{
				Image image = imageGetRep.items.at(0);
				if (image.isOnline_ && !image.isUploading_)
				{
					FileHandle* fd = new FileHandle;
					fd->size_ = image.size_;
					fd->sizeInVolume_ = image.sizeInVolume_;
					std::string openPath = image.smbPathInImageServer_.find(model::AppConfSingleton::get_const_instance().ip_) != std::string::npos ?
						image.localPathInImageServer_ : image.smbPathInImageServer_;
					if (image.format_ == "raw")
					{
						fd->isRaw_ = true;
						fd->rawFd_ = fopen(openPath.c_str(), "rb");
						if (fd->rawFd_ == NULL)
						{
							LOG(warning) << "fopen fail, smb:" << openPath;
							delete fd;
							return STATUS_OPEN_FAILED;
						}
						else
						{
							LOG(info) << "fopen fd:" << fd->rawFd_ << ", smb:" << openPath;
						}
					}
					else
					{
						fd->isRaw_ = false;
						int ret = Ewf_Open(openPath.c_str(), &fd->ewfFd_);
						if (ret != 0)
						{
							LOG(debug) << "Ewf_Open fail, smb:" << openPath;
							delete fd;
							return STATUS_OPEN_FAILED;
						}
						else
						{
							LOG(info) << "Ewf_Open sucess fd:" << fd->ewfFd_ << ", smb:" << openPath;
						}
					}
					
					DokanFileInfo->Context = (uint64_t)fd;
					
				}
				else
				{
					LOG(warning) << "image not offline";
				}
			}
		}
	}
	return status;
}

#pragma warning(push)
#pragma warning(disable : 4305)

static void DOKAN_CALLBACK MirrorCloseFile(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorCloseFile FileName:" << basecpp::toUTF8(FileName) << ", context:" << DokanFileInfo->Context;
	
	if (DokanFileInfo->Context)
	{
		FileHandle* fd = (FileHandle*)DokanFileInfo->Context;
		if (fd->isRaw_)
		{
			LOG(info) << "fclose fd:" << fd->rawFd_;
			fclose(fd->rawFd_);
		}
		else
		{
			LOG(info) << "Ewf_Close fd:" << fd->ewfFd_;
			Ewf_Close(fd->ewfFd_);
		}
		delete fd;
		DokanFileInfo->Context = 0;
	}
}

static void DOKAN_CALLBACK MirrorCleanup(LPCWSTR FileName, PDOKAN_FILE_INFO DokanFileInfo)
{
	LOG(debug) << "MirrorCleanup FileName:" << basecpp::toUTF8(FileName) << ", context:" << DokanFileInfo->Context;
	return;
}

static NTSTATUS DOKAN_CALLBACK MirrorReadFile(LPCWSTR FileName, LPVOID Buffer, DWORD BufferLength,
	LPDWORD ReadLength, LONGLONG Offset, PDOKAN_FILE_INFO DokanFileInfo)
{
	*ReadLength = 0;
	LOG(debug) << "MirrorReadFile  FileName:" << basecpp::toUTF8(FileName) << ", Offset:" << Offset << ", Buffer:" << Buffer
		<< ", BufferLength:" << BufferLength << ", context:" << DokanFileInfo->Context;

	FileHandle* fd = (FileHandle*)DokanFileInfo->Context;
	if (fd != nullptr)
	{
		if (fd->isRaw_)
		{
			size_t ret = fread(Buffer, BufferLength, 1, fd->rawFd_);
			*ReadLength = ret;
		}
		else
		{
			int64_t mediaSize = 0;
			Ewf_GetMediaSize(fd->ewfFd_, &mediaSize);

			uint64_t offset = Offset;
			int64_t readedLen = 0;
			LOG(debug) << "MirrorReadFile mediaSize:" << mediaSize << ", offset:" << offset;
			int ret = Ewf_ReadMedia(fd->ewfFd_, offset, Buffer, BufferLength, &readedLen);
			if (ret != 0)
			{
				LOG(error) << "MirrorReadFile Ewf_ReadMedia fail";
				return STATUS_OPEN_FAILED;
			}
			LOG(debug) << "MirrorReadFile readedLen:" << readedLen;
			*ReadLength = readedLen;
		}
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
	
	return STATUS_SUCCESS;
}

static NTSTATUS DOKAN_CALLBACK MirrorFindFiles(LPCWSTR FileName, PFillFindData FillFindData, PDOKAN_FILE_INFO DokanFileInfo) {
	LOG(debug) << "MirrorFindFiles, FileName:" << basecpp::toUTF8(FileName) << ", MountPoint:" << DokanFileInfo->DokanOptions->MountPoint;
	ImageGetRep imageGetRep;
	listImageInImageCenter(basecpp::toUTF8(FileName), model::AppConfSingleton::get_const_instance().onlyShowActive_,
		imageGetRep);
	LOG(info) << "child file count:" << imageGetRep.items.size();
	BOOST_FOREACH(auto image, imageGetRep.items)
	{
		LOG(info) << "child file item, name:" << image.name_ << ", isDir:" << image.isDir_ << ", size:" << image.size_;
		WIN32_FIND_DATAW findData;
		memset(&findData, 0, sizeof(findData));
		findData.dwFileAttributes = FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
		if (image.isDir_)
		{
			findData.dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
		}
		else
		{
			findData.dwFileAttributes |= FILE_ATTRIBUTE_NORMAL;
		}
		wcscpy_s(findData.cFileName, MAX_PATH, basecpp::fromUTF8(image.name_).c_str());
		findData.nFileSizeHigh = image.size_ >> 32;
		findData.nFileSizeLow = image.size_ & 0xffffffff;
		FillFindData(&findData, DokanFileInfo);
	}
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
		DokanRemoveMountPoint(gMountPoint);
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

void initLog(const std::string& exePath)
{
	std::string logFileName = boost::filesystem::path(exePath).filename().string();
	boost::algorithm::replace_last(logFileName, ".exe", "");
	logFileName = "log/" + logFileName;
	basecpp::AddRotationFileSink(logFileName.c_str(), model::AppConfSingleton::get_const_instance().fileLogRotationMBSize_,
		model::AppConfSingleton::get_const_instance().fileLogAutoFlush_);

	basecpp::SetRotationFileSinkLogLevel(model::AppConfSingleton::get_const_instance().fileLogLevel_.c_str());
	basecpp::SetConsoleSinkLogLevel(model::AppConfSingleton::get_const_instance().consoleLogLevel_.c_str());
}


int __cdecl wmain(ULONG argc, PWCHAR argv[])
{
	basecpp::AddConsoleSink();
	try
	{
		std::string confFileName = boost::filesystem::path(argv[0]).filename().string();
		boost::algorithm::replace_last(confFileName, ".exe", "");
		confFileName.append(".xml");
		std::string confFilePath = boost::filesystem::path(argv[0]).parent_path().append("conf").append(confFileName).string();
		LOG(info) << "start load config file:" << confFilePath;
		model::AppConfSingleton::get_mutable_instance().LoadConf(confFilePath);
		LOG(info) << "start load config file success";
	}
	catch (std::exception& e)
	{
		LOG(error) << "load config file fail:" << e.what();
		LOG(error) << "App exit with:" << EXIT_FAILURE;
		return EXIT_FAILURE;
	}
	
	initLog(basecpp::toGBK(argv[0]));

	LOG(info) << "版本:" << "1.0.0" << ", 编译时间:" << __DATE__ << " " << __TIME__ << ", 进程ID:" << GetCurrentProcessId();
	int status;
	ULONG command;
	PDOKAN_OPERATIONS dokanOperations = (PDOKAN_OPERATIONS)malloc(sizeof(DOKAN_OPERATIONS));
	if (dokanOperations == NULL)
	{
		LOG(error) << "malloc PDOKAN_OPERATIONS fail";
		return EXIT_FAILURE;
	}
	PDOKAN_OPTIONS dokanOptions = (PDOKAN_OPTIONS)malloc(sizeof(DOKAN_OPTIONS));
	if (dokanOptions == NULL) 
	{
		free(dokanOperations);
		return EXIT_FAILURE;
	}

	g_DebugMode = TRUE;
	g_UseStdErr = TRUE;

	ZeroMemory(dokanOptions, sizeof(DOKAN_OPTIONS));
	dokanOptions->Version = DOKAN_VERSION;
	LOG(info) << "dokan option thread count:" << model::AppConfSingleton::get_const_instance().dokanOptionThreadCount_;
	dokanOptions->ThreadCount = model::AppConfSingleton::get_const_instance().dokanOptionThreadCount_; // use default
	std::wstring mountPoint = basecpp::fromUTF8(model::AppConfSingleton::get_const_instance().mountPoint_);
	wcscpy_s(gMountPoint, sizeof(gMountPoint) / sizeof(WCHAR), mountPoint.c_str());
	dokanOptions->MountPoint = gMountPoint;
	LOG(info) << "MountPoint:" << model::AppConfSingleton::get_const_instance().mountPoint_;

	if (!SetConsoleCtrlHandler(CtrlHandler, TRUE))
	{
		LOG(error) << "Control Handler is not set.";
	}

	// Add security name privilege. Required here to handle GetFileSecurity
	// properly.
	g_HasSeSecurityPrivilege = AddSeSecurityNamePrivilege();
	if (!g_HasSeSecurityPrivilege)
	{
		LOG(error) << "Failed to add security privilege to process";
		LOG(error) << "GetFileSecurity/SetFileSecurity may not work properly";
		LOG(error) << "Please restart mirror sample with administrator rights to fix it";
		LOG(error) << "App exit with:" << EXIT_FAILURE;
		return EXIT_FAILURE;
	}

	if (model::AppConfSingleton::get_const_instance().dokanOptionDebug_)
	{
		dokanOptions->Options |= DOKAN_OPTION_DEBUG;
	}
	if (model::AppConfSingleton::get_const_instance().dokanOptionStderr_)
	{
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

	LOG(info) << "DokanMain start";
	status = DokanMain(dokanOptions, dokanOperations);
	try
	{
		LOG(info) << "DokanMain return:" << status << ", msg:" << dokanMainRets.at(status);
	}
	catch (...)
	{
		LOG(info) << "DokanMain return:" << status;
	}




	free(dokanOptions);
	free(dokanOperations);
	return EXIT_SUCCESS;
}
