module imagecenter.model.file.image;
import std.json;
import painlessjson;
import std.file;

struct Image
{
    string name;
    string path;
    bool isDir;
    long size;
    bool isOnline;
    string format;
    string smbPathInImageServer;
    string nfsPathInImageServer;
    string volumeLabelName;
    string pathInVolume;
    long sizeInVolume;
    
}

Image loadFromFile(string filePath)
{
    string jsonStr = readText(filePath);
    JSONValue jsonValue = parseJSON(jsonStr);
    Image ret = fromJSON!Image(jsonValue);
    return ret;
}

void saveToFile(Image image, string filePath)
{
    write(filePath, toJSON(image).toPrettyString);
}
