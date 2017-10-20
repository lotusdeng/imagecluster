module imageserver.app;
import std.stdio;
import std.file;
import std.utf;
import std.conv;
import std.path;
import std.array;
import std.exception;
import std.algorithm;
import std.string;
import std.experimental.logger;
import std.process;
import std.concurrency;
import imageserver.model.file.appconf;
import imageserver.rest.server;
import imagecommon.log;
import vibe.core.log;
import imageserver.task.createsmb;



void initVibeLog()
{
	info("vibeLogLevel:", gAppConf.vibeLogLevel);
	if(gAppConf.vibeLogLevel == "trace")
	{
		setLogLevel(vibe.core.log.LogLevel.trace);
	}
	else if(gAppConf.vibeLogLevel == "info")
	{
		setLogLevel(vibe.core.log.LogLevel.info);
	}
}

void main(string[] args)
{
	
	string logFileName = baseName(args[0]);
	logFileName = logFileName.replace(".exe", "");
    logFileName ~= ".txt";
	initLog(logFileName);

	string confFileName = baseName(args[0]);
	confFileName = confFileName.replace(".exe", "");
    confFileName ~= ".json";
	string confFilePath = buildPath("conf", confFileName);
	initAppConf(confFilePath);
	
	initVibeLog();
	
	info("--------------------------------------------------------");
	info(baseName(args[0]), "程序开始start, processid:", thisProcessID(), "bindAddress:", gAppConf.ip, ", port:", gAppConf.port,
	", arch:", (size_t.sizeof == 4 ? "32bit" : "64bit"));

	if(gAppConf.autoSetSmb)
	{
		auto tid1 = spawn(&Task_CreateSmb);
	}

	startRESTServer(gAppConf.ip, gAppConf.port);
	
}
