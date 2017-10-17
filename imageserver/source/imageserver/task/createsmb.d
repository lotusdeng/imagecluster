module imageserver.task.createsmb;
import imagecommon.file;
import std.experimental.logger;
import std.process;
import std.format;
import core.thread;
import std.string;
import imageserver.model.file.appconf;

//key is labelName
Volume[string] listAllVolumeWithLabel()
{
    Volume[] volumes = listAllVolume();
    Volume[string] retVolumes;
    foreach (volume; volumes)
    {
        if (volume.labelName.length == 0 || volume.pathName.length == 0)
        {
            continue;
        }

        retVolumes[volume.labelName] = volume;
    }
    return retVolumes;
}

string[string] listSmb()
{
    string[string] smbs;
    string cmd = "net share";
    info(cmd);
    auto ret = executeShell(cmd);
    info("cmd out:", ret.output);
    string[] lines = ret.output.split('\n');
    foreach (line; lines)
    {
        ptrdiff_t pos1 = line.indexOf(":\\");
        if (pos1 != -1)
        {
            string driveLetter = line[(pos1-1)..(pos1+2)];
            ptrdiff_t pos2 = line.indexOf(" ");
            if(pos2 != -1)
            {
                string name = line[0..pos2];
                smbs[name] = driveLetter;
                trace("smb name:", name, ", driveLetter:", driveLetter);
            }
        }
    }
    return smbs;
}

void Task_CreateSmb()
{
    info("Task_CreateSmb start");
    Volume[string] gLastVolumes;

    while (true)
    {
        scope (exit)
        {
            Thread.sleep(dur!("seconds")(5));
        }
        string[string] smbs = listSmb();
        Volume[string] volumes = listAllVolumeWithLabel();
        //find added volume
        foreach (volume; volumes)
        {
            if (volume.labelName !in gLastVolumes)
            {
                info("volume insert, volume id:", volume.volumeId, ", label:",
                        volume.labelName, ", pathName:", volume.pathName);

                bool needSetSmb = true;
                if (volume.labelName in smbs)
                {
                    if (volume.pathName != smbs[volume.labelName])
                    {
                        info(
                                "volume already set smb, but current pathName not equal smb, first remove smb");
                        string cmd = format!"net share %s /delete"(volume.labelName);
                        info(cmd);
                        auto ret = executeShell(cmd);
                        info("cmd out:", ret.output);
                    }
                    else
                    {
                        info("volume already set smb, and current pathName equal smb, not set smb");
                        needSetSmb = false;
                    }
                }

                if (needSetSmb)
                {
                    string cmd = format!"net share %s=%s /GRANT:%s,read"(volume.labelName,
                            volume.pathName, gAppConf.smbAccount);

                    info(cmd);
                    auto ret = executeShell(cmd);
                    info("cmd out:", ret.output);
                }
            }
        }

        //find removed volume
        foreach (volume; gLastVolumes)
        {
            if (volume.labelName !in volumes)
            {
                info("volume remove, volume id:", volume.volumeId, ", label:",
                        volume.labelName, ", pathName:", volume.pathName);
                string cmd = format!"net share %s /delete"(volume.labelName);
                info(cmd);
                auto ret = executeShell(cmd);
                info("cmd out:", ret.output);
            }
        }
        gLastVolumes = volumes;
    }
}
