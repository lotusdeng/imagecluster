module imageserver.rest.impl.file;
import std.experimental.logger;
import std.file;
import std.path;
import std.format;
import std.concurrency;
import std.string;
import std.algorithm.mutation;
import std.typecons;
import std.container.rbtree;
import vibe.web.rest;
import imageserver.rest.api.file;
import imagecommon.rest.api.common;
import imagecommon.file;
import imageserver.model.file.appconf;

Volume[] listAllVolumeWithFilter()
{
    RedBlackTree!string discardVolumeSet = make!(RedBlackTree!string)(gAppConf.discardVolumes);
    Volume[] retVolumes;
    Volume[] volumes = listAllVolume();
    foreach (volume; volumes)
    {
        if (volume.pathName in discardVolumeSet || volume.pathName.length == 0)
        {
            continue;
        }

        retVolumes ~= volume;
    }
    return retVolumes;
}

class FileImpl : FileApi
{
    Nullable!(imagecommon.file.Volume) getVolumeByVolumeId(string volumeId)
    {
        Nullable!Volume ret;
        Volume[] volumes = listAllVolumeWithFilter();
        foreach (volume; volumes)
        {
            if (volume.volumeId == volumeId)
            {
                ret = volume;
                break;
            }
        }
        return ret;

    }

    Nullable!(imagecommon.file.Volume) getVolumeByVolumeLabel(string volumeLabel)
    {
        Nullable!Volume ret;
        Volume[] volumes = listAllVolumeWithFilter();
        foreach (volume; volumes)
        {
            if (volume.labelName == volumeLabel)
            {
                ret = volume;
                break;
            }
        }
        return ret;

    }

    VolumeGetRep getVolumes()
    {
        VolumeGetRep rep;
        rep.items = listAllVolumeWithFilter();
        return rep;
    }

    VolumeGetRep getVolume(string _volumeId)
    {
        VolumeGetRep rep;
        Nullable!(imagecommon.file.Volume) volume;
        if (_volumeId.length == 36)
        {
            info("get volume, volumeId:", _volumeId);
            volume = getVolumeByVolumeId(_volumeId);
            if (volume.isNull)
            {
                error("volume nost exist, volumeId:", _volumeId);
                rep.code = -1;
                rep.msg = "volume not exist";
                return rep;
            }
        }
        else
        {
            info("get volume, volumeLabel:", _volumeId);
            volume = getVolumeByVolumeLabel(_volumeId);
            if (volume.isNull)
            {
                error("volume nost exist, volume label:", _volumeId);
                rep.code = -1;
                rep.msg = "volume not exist";
                return rep;
            }
        }

        rep.items ~= volume;
        return rep;
    }

    CommonRep setLabel(string _volumeId, string labelName)
    {
        info("set label, volumeId:", _volumeId, ", labelName:", labelName);
        CommonRep rep;
        Nullable!Volume volume = getVolumeByVolumeId(_volumeId);
        if (volume.isNull)
        {
            error("volume nost exist, volumeId:", _volumeId);
            rep.code = -1;
            rep.msg = "volume not exist";
            return rep;
        }

        imagecommon.file.SetVolumeLabel(volume.pathName, labelName);
        return rep;
    }

    FileGetRep getFiles(string _volumeId, string path)
    {
        info("get files, volumeId:", _volumeId, "path:", path);
        FileGetRep rep;
        Nullable!Volume volume;
        if (_volumeId.length == 36)
        {
            volume = getVolumeByVolumeId(_volumeId);
            if (volume.isNull)
            {
                error("volume nost exist, volumeId:", _volumeId);
                rep.code = -1;
                rep.msg = "volume not exist";
                return rep;
            }
        }
        else
        {
            volume = getVolumeByVolumeLabel(_volumeId);
            if (volume.isNull)
            {
                error("volume nost exist, volume label:", _volumeId);
                rep.code = -1;
                rep.msg = "volume not exist";
                return rep;
            }
        }

        string localPath = buildPath(volume.pathName, path);
        info("local path:", localPath);
        if (!exists(localPath))
        {
            error("file nost exist, path:", localPath);
            rep.code = -1;
            rep.msg = "file not exist";
            return rep;
        }

        if (isDir(localPath))
        {
            foreach (dirItem; dirEntries(localPath, SpanMode.shallow))
            {
                File item;
                item.name = baseName(dirItem.name);
                item.size = dirItem.size;
                item.isDir = dirItem.isDir;
                item.volumeId = _volumeId;
                item.pathInVolume = chompPrefix(dirItem.name, volume.pathName);
                item.pathInVolume = item.pathInVolume.replace("\\", "/");
                item.smbPath = format!"\\\\%s\\%s\\%s"(gAppConf.ip,
                        volume.labelName, item.pathInVolume);
                item.smbPath = item.smbPath.replace("/", "\\");
                item.nfsPath = format!"//%s/%s/%s"(gAppConf.ip,
                        volume.labelName, item.pathInVolume);
                item.nfsPath = item.nfsPath.replace("\\", "/");
                item.httpPath = format!"http://%s:%d/volume/%s/file?path=%s"(gAppConf.ip,
                        gAppConf.port, _volumeId, item.pathInVolume);
                rep.items ~= item;
            }
        }
        else
        {
            File item;
            item.name = baseName(localPath);
            item.size = getSize(localPath);
            item.isDir = false;
            item.volumeId = _volumeId;
            item.pathInVolume = path;
            item.pathInVolume = item.pathInVolume.replace("\\", "/");
            item.smbPath = format!"\\\\%s\\%s\\%s"(gAppConf.ip,
                    volume.labelName, item.pathInVolume);
            item.smbPath = item.smbPath.replace("/", "\\");
            item.nfsPath = format!"//%s/%s/%s"(gAppConf.ip, volume.labelName, item.pathInVolume);
            item.nfsPath = item.nfsPath.replace("\\", "/");
            item.httpPath = format!"http://%s:%d/volume/%s/file?path=%s"(gAppConf.ip,
                    gAppConf.port, _volumeId, item.pathInVolume);
            rep.items ~= item;
        }
        return rep;
    }
}
