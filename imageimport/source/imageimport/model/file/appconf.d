module imageimport.model.file.appconf;
import std.json;
import std.file;
import std.experimental.logger;
import painlessjson;


struct AppConf
{
    string logLevel;
    string vibeLogLevel;
    string imageCenterIP;
    ushort imageCenterPort;
}

__gshared AppConf gAppConf;

void initAppConf(string confFilePath)
{
    info("start load conf:", confFilePath);
    gAppConf = loadFromFile(confFilePath);
    info("load conf end");
}

AppConf loadFromFile(string filePath)
{
    string jsonStr = readText(filePath);
    JSONValue jsonValue = parseJSON(jsonStr);
    AppConf ret = fromJSON!AppConf(jsonValue);
    return ret;
}

void saveToFile(AppConf appConf, string filePath)
{
    write(filePath, toJSON(appConf).toPrettyString);
}