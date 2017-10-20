module imagecenter.rest.impl.image;
import std.experimental.logger;
import std.file;
import std.path;
import std.string;
import std.process;
import vibe.web.rest;
import imagecenter.rest.api.image;
import imagecenter.model.file.appconf;
import imageserver.rest.api.volume;
import imagecommon.rest.api.common;

struct Volume
{
    string imageServerIP;
    ushort imageServerPort;
    string volumeId;
    string labelName;
    string pathName;
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
        foreach (line; lines)
        {
            line = line.strip();
            if (line.indexOf("mediaSize:") != -1)
            {
                string sizeStr = line["mediaSize:".length .. line.length];
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
            auto api = new RestInterfaceClient!(imageserver.rest.api.volume.VolumeApi)(httpAddress);
            imageserver.rest.api.volume.VolumeGetRep ret = api.getVolumes();
            if (ret.code == 0)
            {
                foreach (item; ret.items)
                {
                    if (item.labelName.length == 0)
                    {
                        continue;
                    }
                    Volume volume;
                    volume.imageServerIP = server.ip;
                    volume.imageServerPort = server.port;
                    volume.labelName = item.labelName;
                    volume.volumeId = item.volumeId;
                    volume.pathName = item.pathName;
                    volumes[volume.labelName] = volume;
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
                    item.loadFromFile(buildPath(dirItem.name, "dir.json"));
                    item.name = baseName(dirItem.name);
                    item.path = chompPrefix(dirItem.name, localPath);
                }
                else if(baseName(dirItem.name) != "dir.json")
                {
                    item.loadFromFile(dirItem.name);
                    item.name = baseName(dirItem.name);
                    item.path = chompPrefix(dirItem.name, localPath);
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
            Image item;
            item.loadFromFile(localPath);
            item.name = baseName(localPath);
            if (item.volumeLabelName in volumes)
            {
                Volume volume = volumes[item.volumeLabelName];
                item.isOnline = true;
                item.nfsPathInImageServer = format!"//%s/%s/%s"(volume.imageServerIP,
                        volume.labelName, item.pathInVolume);
                item.nfsPathInImageServer = item.nfsPathInImageServer.replace("\\", "/");
                item.smbPathInImageServer = format!"\\\\%s\\%s\\%s"(volume.imageServerIP,
                        volume.labelName, item.pathInVolume);
                item.smbPathInImageServer = item.smbPathInImageServer.replace("/", "\\");
                item.localPathInImageServer = buildPath(volume.pathName, item.pathInVolume);
                item.localPathInImageServer = item.localPathInImageServer.replace("/", "\\");
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
            Image item;
            item.saveToFile(buildPath(localPath, "dir.json"));
            return rep;
        }
        else
        {
            Image item;
            item.name = data.name;
            item.path = buildPath(path, data.name);
            item.isUploading = data.isUploading;
            item.isDir = data.isDir;
            item.format = data.format;
            item.volumeLabelName = data.volumeLabelName;
            item.pathInVolume = data.pathInVolume;
            item.sizeInVolume = data.sizeInVolume;
            item.smbPathInImageServer = data.smbPathInImageServer;
            item.nfsPathInImageServer = data.nfsPathInImageServer;
            if (!item.isUploading)
            {
                if (item.format == "raw")
                {
                    item.size = data.sizeInVolume;
                }
                else if (item.format == "ewf")
                {
                    item.size = getEwfMediaSize(data.smbPathInImageServer);
                }
            }
            info("save to file:", localPath);
            item.saveToFile(localPath);
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

    CommonRep uploadFinishByPath(string path)
    {
        info("uploadfinish file, path:", path);
        CommonRep rep;
        string localPath = buildPath("fileroot", path);
        if (!exists(localPath))
        {
            error("image not exist, localPath:", localPath);
            rep.code = -1;
            rep.msg = "image not exist";
            return rep;
        }
        if (isDir(localPath))
        {
            error("dir not support uploadfinish, localPath:", localPath);
            rep.code = -1;
            rep.msg = "dir not support uploadfinish";
            return rep;
        }

        Image item;
        item.loadFromFile(localPath);
        item.isUploading = false;
        if (item.format == "raw")
        {
            item.size = item.sizeInVolume;
        }
        else if (item.format == "ewf")
        {
            item.size = getEwfMediaSize(item.smbPathInImageServer);
        }
        item.saveToFile(localPath);

        return rep;
    }

    CommonRep activeByPath(string path)
    {
        CommonRep rep;
        string localPath = buildPath("fileroot", path);
        if (!exists(localPath))
        {
            error("image not exist, localPath:", localPath);
            rep.code = -1;
            rep.msg = "image not exist";
            return rep;
        }
        string dataFilePath = isDir(localPath) ? buildPath(localPath, "dir.json") : localPath;
        Image item;
        item.loadFromFile(dataFilePath);
        if (!item.isActive)
        {
            item.isActive = true;
            item.saveToFile(dataFilePath);
        }
        return rep;
    }

    CommonRep disactiveByPath(string path)
    {
        CommonRep rep;
        string localPath = buildPath("fileroot", path);
        if (!exists(localPath))
        {
            error("image not exist, localPath:", localPath);
            rep.code = -1;
            rep.msg = "image not exist";
            return rep;
        }
        string dataFilePath = isDir(localPath) ? buildPath(localPath, "dir.json") : localPath;
        Image item;
        item.loadFromFile(dataFilePath);
        if (item.isActive)
        {
            item.isActive = false;
            item.saveToFile(dataFilePath);
        }
        return rep;
    }
}
