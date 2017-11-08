module imagecommon.file;
import std.string;
import core.stdc.string;
import std.stdio;
import std.experimental.logger;

version (Windows)
{
    import core.sys.windows.windef;
    import core.sys.windows.winbase;
}

struct Volume
{
    string volumeId;
    string volumeName;
    string labelName;
    string pathName;
}

version (Windows)
{
    Volume[] listAllVolume()
    {
        Volume[] volumes;
        char[260] volumeName = void;
        HANDLE findHandle = FindFirstVolumeA(volumeName.ptr, volumeName.length);
        if (findHandle == INVALID_HANDLE_VALUE)
        {
            error("FindFirstVolumeA fail");
            return volumes;
        }
        scope (exit)
        {
            FindVolumeClose(findHandle);
        }

        while (true)
        {
            size_t volumeNameLen = strlen(volumeName.ptr);
            //volumeName = volumeName[0..len];
            trace("volume:", volumeName[0..volumeNameLen]);
            if (volumeName[0] != '\\' || volumeName[1] != '\\'
                    || volumeName[2] != '?' || volumeName[3] != '\\'
                    || volumeName[volumeNameLen - 1] != '\\')
            {
                break;
            }

            //
            //  QueryDosDeviceW does not allow a trailing backslash,
            //  so temporarily remove it.
            volumeName[volumeNameLen - 1] = '\0';
            char[256] targetPath = void;
            DWORD charCount = QueryDosDeviceA(&volumeName[4], targetPath.ptr, targetPath.length);

            volumeName[volumeNameLen - 1] = '\\';

            char[256] labelName = void;
            BOOL ret = GetVolumeInformationA(volumeName.ptr, labelName.ptr,
                    labelName.length, NULL, NULL, NULL, NULL, 0);
            size_t labelNameLen = strlen(labelName.ptr);

            char[32] pathName;
            GetVolumePathNamesForVolumeNameA(volumeName.ptr, pathName.ptr, pathName.length, NULL);
            size_t pathNameLen = strlen(pathName.ptr);
            Volume volume;
            if(volumeNameLen != 0) volume.volumeName = volumeName[0..volumeNameLen].dup;
            if(labelNameLen != 0) volume.labelName = labelName[0..labelNameLen].dup;
            if(pathNameLen != 0) volume.pathName = pathName[0..pathNameLen].dup;
            volume.volumeId = chompPrefix(volume.volumeName, "\\\\?\\Volume{");
            volume.volumeId = chomp(volume.volumeId, "}\\");
            volumes ~= volume;
            //  Move on to the next volume.
            BOOL success = FindNextVolumeA(findHandle, volumeName.ptr, volumeName.length);

            if (!success)
            {
                break;
            }
        }

        return volumes;

    }

    void SetVolumeLabel(string volumePathName, string labelName)
    {
        SetVolumeLabelA(volumePathName.toStringz(), labelName.toStringz());
    }
}

unittest
{
    info("listAllVolume");
    Volume[] items = listAllVolume();
    info("listAllVolume return volume count:", items.length);
    foreach (item; items)
    {
        writeln("volumeId:", item.volumeId, ", volumeName:", item.volumeName, ", labelName:", item.labelName,
                ", pathName:", item.pathName);
    }
    //SetVolumeLabel("c:\\", "123");
}
