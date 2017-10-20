module imagecenter.rest.api.imageserver;
import vibe.web.rest;
import vibe.data.json;
import imagecommon.rest.api.common;
import imagecenter.model.file.appconf;

struct ImageServerGetRep
{
    int code;
    string msg;
    ImageServer[] items;
}


interface ImageServerApi
{
   	@path("/imageserver")
	@method(HTTPMethod.GET)
	ImageServerGetRep getImageServers();
}
