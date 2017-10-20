module imagecenter.rest.impl.imageserver;
import std.experimental.logger;
import std.file;
import std.path;
import std.string;
import std.process;
import vibe.web.rest;
import imagecommon.rest.api.common;
import imagecenter.model.file.appconf;
import imagecenter.rest.api.imageserver;

class ImageServerImpl : ImageServerApi
{
    ImageServerGetRep getImageServers()
    {
        ImageServerGetRep rep;
        rep.items = gAppConf.imageServers;
        return rep;
    }
}