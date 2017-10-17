module imageserver.rest.api.file;
import vibe.web.rest;
import vibe.data.json;
import imagecommon.file;
import imagecommon.rest.api.common;

struct VolumeGetRep
{
    int code;
    string msg;
    Volume[] items;
}

struct File
{
    string name;
    bool isDir;
    long size;
    string volumeId;
    string pathInVolume;
    string nfsPath;
    string smbPath;
    string httpPath;
    
}

struct FileGetRep
{
    int code;
    string msg;
    File[] items;
}

interface FileApi
{
    @path("/volume")
    @method(HTTPMethod.GET)
    VolumeGetRep getVolumes();

    @path("/volume/:volumeId")
    @method(HTTPMethod.GET)
    VolumeGetRep getVolume(string _volumeId);

    @path("/volume/:volumeId/setlabel")
    @queryParam("labelName", "label")
    @method(HTTPMethod.GET)
    CommonRep setLabel(string _volumeId, string labelName);

    @path("/volume/:volumeId/file")
    @queryParam("path", "path")
    @method(HTTPMethod.GET)
    FileGetRep getFiles(string _volumeId, string path);
}
