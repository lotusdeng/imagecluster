module imageimport.app;
import std.stdio;
import std.string;
import std.path;
import std.process;
import std.experimental.logger;
import imageimport.model.file.appconf;
import imagecenter.rest.api.imageserver;
import imageserver.rest.api.volume;
import imagecommon.log;
import imagecommon.rest.api.common;
import vibe.core.log;
import vibe.web.rest;

void initVibeLog()
{
	info("vibeLogLevel:", gAppConf.vibeLogLevel);
	if (gAppConf.vibeLogLevel == "trace")
	{
		setLogLevel(vibe.core.log.LogLevel.trace);
	}
	else if (gAppConf.vibeLogLevel == "info")
	{
		setLogLevel(vibe.core.log.LogLevel.info);
	}
}

struct MyVolume
{
	string imageServerIP;
	ushort imageServerPort;
	string volumeId;
	string volumeLabelName;
	long totalSize;
	long freeSize;
}

shared static this()
{
	info("app this");
	setLogLevel(vibe.core.log.LogLevel.trace);
}

void main(string[] args)
{
	info("imageimport start, args.length:", args.length);
	if (args.length != 3)
	{
		return;
	}

	string imageLocalPath = args[1];
	string imagePathInCenter = args[2];

	string logFileName = baseName(args[0]);
	logFileName = logFileName.replace(".exe", "");
	logFileName ~= ".txt";
	initLog(logFileName);

	string confFileName = baseName(args[0]);
	confFileName = confFileName.replace(".exe", "");
	confFileName ~= ".json";
	string confFilePath = buildPath("conf", confFileName);
	initAppConf(confFilePath);

	//initVibeLog();

	info("--------------------------------------------------------");
	info(baseName(args[0]), "start, processid:", thisProcessID(), ", arch:",
			(size_t.sizeof == 4 ? "32bit" : "64bit"));

	MyVolume[] findVolumes;
	string imageCenterHttpAddress = format!"http://%s:%s"(gAppConf.imageCenterIP,
			gAppConf.imageCenterPort);
	info("get image server from image center address:", imageCenterHttpAddress);
	try
	{
		auto api1 = new RestInterfaceClient!(imagecenter.rest.api.imageserver.ImageServerApi)(
				imageCenterHttpAddress);
		imagecenter.rest.api.imageserver.ImageServerGetRep imageServerGetRep = api1.getImageServers();
		foreach (imageServerItem; imageServerGetRep.items)
		{
			string httpAddress = format!"http://%s:%s"(imageServerItem.ip, imageServerItem.port);
			auto api2 = new RestInterfaceClient!(imageserver.rest.api.volume.VolumeApi)(httpAddress);
			imageserver.rest.api.volume.VolumeGetRep volumeGetRep = api2.getVolumes();
			foreach (volumeItem; volumeGetRep.items)
			{
				MyVolume myVolume;
				myVolume.imageServerIP = imageServerItem.ip;
				myVolume.imageServerPort = imageServerItem.port;
				myVolume.volumeId = volumeItem.volumeId;
				myVolume.volumeLabelName = volumeItem.labelName;
				myVolume.totalSize = volumeItem.totalSize;
				myVolume.freeSize = volumeItem.freeSize;
				findVolumes ~= myVolume;

			}
		}
	}
	catch (Exception e)
	{

	}

	info("find volume count:", findVolumes.length);

	MyVolume selectVolume;
	foreach (volume; findVolumes)
	{
		if (selectVolume.freeSize == 0)
		{
			selectVolume = volume;
		}
		else if (volume.freeSize < selectVolume.freeSize && volume.freeSize > 50 * 1024 * 1024
				* 1024)
		{
			selectVolume = volume;
		}
	}
	if(selectVolume.imageServerIP.length == 0)
	{
		info("not find volume");
		return;
	}
	info("select volume, imageserver ip:", selectVolume.imageServerIP, ", port:", selectVolume.imageServerPort,
			", volumeId:", selectVolume.volumeId, ", label:", selectVolume.volumeLabelName);
	
	string smbPath = format!"\\\\%s\\%s\\%s"(selectVolume.imageServerIP,
			selectVolume.volumeLabelName, baseName(imageLocalPath));
	info("smbpath:", smbPath);

	auto destFd = std.stdio.File(smbPath, "a");
	auto srcFd = std.stdio.File(imageLocalPath);
	info("smb file size:", destFd.size);
	srcFd.seek(destFd.size);
	static ubyte[1024*1024*5] buffer;
	while (srcFd.tell() < srcFd.size)
	{
		auto data = srcFd.rawRead(buffer);
		//scope (exit)
		//	GC.free(data.ptr);
		destFd.rawWrite(data);
	}
	info("write smb finish");
	info("start crate file in imagecenter:", imagePathInCenter);

	auto api1 = new RestInterfaceClient!(imagecenter.rest.api.image.ImageApi)(
			imageCenterHttpAddress);
	imagecenter.rest.api.image.ImagePostReq req;
	req.name = baseName(imagePathInCenter);
	req.isDir = false;
	req.format = (imageLocalPath.indexOf(".e01") != -1 || imageLocalPath.indexOf(".E01") != -1) ? "ewf" : "dd";
	req.smbPathInImageServer = smbPath;
	req.volumeLabelName = selectVolume.volumeLabelName;
	req.pathInVolume = baseName(imageLocalPath);
	req.sizeInVolume = srcFd.size;
	string path = imagePathInCenter.indexOf("/") == -1 ? "" : baseName(imagePathInCenter);
	info("path:", path);
	imagecommon.rest.api.common.CommonRep commonRep = api1.postByPath(path, req);
	if (commonRep.code == 0)
	{
		info("create file in center success");
	}
	else
	{
		error("create file in center fail, msg:", commonRep.msg);
	}
}
