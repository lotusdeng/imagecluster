module imagecenter.rest.api.image;
import vibe.web.rest;
import vibe.data.json;
import imagecommon.rest.api.common;
import imagecenter.model.file.image;

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
	string format = "dd";
	@optional string smbPathInImageServer;
	@optional string nfsPathInImageServer;
	@optional string volumeLabelName;
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
}
