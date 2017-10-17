module imagecenter.rest.impl.image;
import imagecenter.rest.api.image;
import std.experimental.logger;
import std.file;
import std.path;
import std.string;
import std.process;
import vibe.web.rest;
import imagecenter.model.file.image;
import imageserver.rest.api.file;
import imagecommon.rest.api.common;
import imagecenter.model.file.appconf;


struct Volume
{
    string imageServerIP;
    ushort imageServerPort;
    string volumeId;
    string volumeLabelName;
}

long getEwfMediaSize(string ewf)
{
    auto dmd = execute(["ewfinfo.exe", ewf]);
    if (dmd.status != 0)
    {
        return 0;
    }
    else
    {
        string[] lines = dmd.output.split("\n");
        foreach(line; lines)
        {
            line = line.strip();
            if(line.indexOf("mediaSize:") != -1)
            {
                string sizeStr = line["mediaSize:".length..line.length];
                info("media size:[", sizeStr, "]");
                return to!long(sizeStr);
            }
        }
        return 0;
    }

}

Volume[string] getVolumeFromAllImageServer()
{
    Volume[string] volumes;
    foreach (server; gAppConf.imageServers)
    {

        string httpAddress = format!"http://%s:%s"(server.ip, server.port);
        info("get volume from image server address:", httpAddress);
        try
        {
            auto api = new RestInterfaceClient!(imageserver.rest.api.file.FileApi)(httpAddress);
            imageserver.rest.api.file.VolumeGetRep ret = api.getVolumes();
            if (ret.code == 0)
            {
                foreach (item; ret.items)
                {
                    Volume volume;
                    volume.imageServerIP = server.ip;
                    volume.imageServerPort = server.port;
                    volume.volumeLabelName = item.labelName;
                    volume.volumeId = item.volumeId;
                    volumes[volume.volumeLabelName] = volume;
                }
            }
        }
        catch (Exception e)
        {
            error("get volume from image server catch a exception:", e.toString);
        }

    }
    return volumes;
}

class ImageImpl : ImageApi
{
    ImageGetRep getByPath(string path)
    {
        info("get files, path:", path);
        ImageGetRep rep;
        string localPath = buildPath("fileroot", path);
        if (!exists(localPath))
        {
            warning("file not exist, path:", localPath);
            rep.code = -1;
            rep.msg = "file not exist";
            return rep;
        }

        Volume[string] volumes = getVolumeFromAllImageServer();

        if (isDir(localPath))
        {
            foreach (dirItem; dirEntries(localPath, SpanMode.shallow))
            {
                Image item;
                item.isDir = dirItem.isDir;
                if (dirItem.isDir)
                {
                    item.name = baseName(dirItem.name);
                    item.path = chompPrefix(dirItem.name, localPath);
                }
                else
                {
                    item = imagecenter.model.file.image.loadFromFile(dirItem.name);
                    if (item.volumeLabelName in volumes)
                    {
                        item.isOnline = true;
                    }
                    else
                    {
                        item.isOnline = false;
                    }
                }
                rep.items ~= item;
            }
        }
        else
        {
            Image item = imagecenter.model.file.image.loadFromFile(localPath);
            if (item.volumeLabelName in volumes)
            {
                Volume volume = volumes[item.volumeLabelName];
                item.isOnline = true;
                item.nfsPathInImageServer = format!"//%s/%s/%s"(volume.imageServerIP, volume.volumeLabelName, item.pathInVolume);
                item.nfsPathInImageServer = item.nfsPathInImageServer.replace("\\", "/");
                item.smbPathInImageServer = format!"\\\\%s\\%s\\%s"(volume.imageServerIP, volume.volumeLabelName, item.pathInVolume);
                item.smbPathInImageServer = item.smbPathInImageServer.replace("/", "\\");
            }
            else
            {
                item.isOnline = false;
            }
            rep.items ~= item;
        }
        return rep;
    }

    CommonRep postByPath(string path, ImagePostReq data)
    {
        info("create file, path:", path, "name:", data.name);
        CommonRep rep;
        string localPath = buildPath("fileroot", path, data.name);
        if (exists(localPath))
        {
            warning("file already exist, can't create, path:", localPath);
            rep.code = -1;
            rep.msg = "already exist, can't create";
            return rep;
        }
        if (data.isDir)
        {
            info("create dir:", localPath);
            mkdir(localPath);
            return rep;
        }
        else
        {
            Image item;
            item.name = data.name;
            item.path = buildPath(path, data.name);
            item.isDir = data.isDir;
            item.format = data.format;
            item.volumeLabelName = data.volumeLabelName;
            item.pathInVolume = data.pathInVolume;
            item.sizeInVolume = data.sizeInVolume;
            item.smbPathInImageServer = data.smbPathInImageServer;
            item.nfsPathInImageServer = data.nfsPathInImageServer;
            if(data.format == "raw")
            {
                item.size = data.sizeInVolume;
            }
            else if(data.format == "ewf")
            {
                item.size = getEwfMediaSize(data.smbPathInImageServer);
            }
            info("save to file:", localPath);
            saveToFile(item, localPath);
        }
        return rep;
    }

    CommonRep deleteByPath(string path)
    {
        info("delete file, path:", path);
        CommonRep rep;
        string localPath = buildPath("fileroot", path);
        if (!exists(localPath))
        {
            return rep;
        }
        if (isDir(localPath))
        {
            info("remove dir:", localPath);
            rmdirRecurse(localPath);
        }
        else
        {
            info("remove file:", localPath);
            remove(localPath);
        }
        return rep;
    }
}
