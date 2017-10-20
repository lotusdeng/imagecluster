module imagecenter.model.file.appconf;
import std.json;
import std.file;
import std.experimental.logger;
import painlessjson;

struct ImageServer
{
    string ip;
    ushort port;
}

struct AppConf
{
    string ip;
    ushort port;
    string logLevel;
    string vibeLogLevel;
    ImageServer[] imageServers;
    void loadFromFile(string filePath)
    {
        string jsonStr = readText(filePath);
        JSONValue jsonValue = parseJSON(jsonStr);
        this = fromJSON!AppConf(jsonValue);
    }

    void saveToFile(string filePath)
    {
        write(filePath, toJSON(this).toPrettyString);
    }
}

__gshared AppConf gAppConf;

void initAppConf(string confFilePath)
{
    info("start load conf:", confFilePath);
    gAppConf.loadFromFile(confFilePath);
    info("load conf end, listenPort:", gAppConf.port);
}

