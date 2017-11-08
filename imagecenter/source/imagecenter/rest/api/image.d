module imagecenter.rest.api.image;
import vibe.web.rest;
import vibe.data.json;
import imagecommon.rest.api.common;
import painlessjson;
import std.json;
import std.file;

struct Image
{
	string name;
	string path;
	bool isDir;
	long size;
	bool isActive = false;
	bool isUploading = false;
	bool isOnline;
	string format;
	string localPathInImageServer;
	string smbPathInImageServer;
	string nfsPathInImageServer;
	string volumeId;
	string pathInVolume;
	long sizeInVolume;
	void loadFromFile(string filePath)
	{
		string jsonStr = readText(filePath);
		JSONValue jsonValue = parseJSON(jsonStr);
		this = fromJSON!Image(jsonValue);
	}

	void saveToFile(string filePath)
	{
		write(filePath, toJSON(this).toPrettyString);
	}
}

struct ImageGetRep
{
	int code;
	string msg;
	Image[] items;
}

struct ImagePostReq
{
	string name;
	bool isDir;
	@optional bool isActive = true;
	@optional bool isUploading = false;
	string format = "dd";
	@optional string smbPathInImageServer;
	@optional string nfsPathInImageServer;
	@optional string volumeId;
	@optional string pathInVolume;
	@optional long sizeInVolume;
}

interface ImageApi
{
	@path("/image")
	@queryParam("path", "path")
	@method(HTTPMethod.GET)
	ImageGetRep getByPath(string path);

	@path("/image")
	@queryParam("path", "path")
	@bodyParam("data")
	@method(HTTPMethod.POST)
	CommonRep postByPath(string path, ImagePostReq data);

	@path("/image/remove")
	@queryParam("path", "path")
	@method(HTTPMethod.GET)
	CommonRep deleteByPath(string path);

	@path("/image/uploadfinish")
	@queryParam("path", "path")
	@method(HTTPMethod.GET)
	CommonRep uploadFinishByPath(string path);

	@path("/image/active")
	@queryParam("path", "path")
	@method(HTTPMethod.GET)
	CommonRep activeByPath(string path);

	@path("/image/disactive")
	@queryParam("path", "path")
	@method(HTTPMethod.GET)
	CommonRep disactiveByPath(string path);
}
