module imagecenter.rest.server;
import std.concurrency;
import std.conv;
import std.experimental.logger;
import std.functional;
import std.array;
import std.path;
import std.file;
import vibe.http.server;
import vibe.http.router;
import vibe.core.core;
import vibe.web.rest;
import imagecenter.rest.impl.image;
import imagecenter.rest.impl.imageserver;



alias void delegate(HTTPServerRequest req, HTTPServerResponse res) RequestHandler;

void logRequest(HTTPServerRequest req, HTTPServerResponse res)
{
	info("+++++++++++++++++++++++++++++++++++++++++++++++++");

	string[] msgs;
	auto pSeqId = "seqid" in req.headers;
	if (pSeqId)
	{
		msgs ~= ("sedid:" ~ *pSeqId);
		res.headers["seqid"] = *pSeqId;
	}
	msgs ~= to!string(req.method);
	msgs ~= req.requestURL;
	msgs ~= req.peer;

	info(join(msgs, " "));
}

void logResponse(HTTPServerResponse res)
{
	string[] msgs;
	auto pSeqId = "seqid" in res.headers;
	if (pSeqId)
	{
		msgs ~= ("sedid:" ~ *pSeqId);
	}
	msgs ~= ("statusCode:" ~ to!string(res.statusCode));

	info(join(msgs, " "));
	info("-------------------------------------------------");
}

void startRESTServer(string ip, ushort listenPort)
{
	auto settings = new HTTPServerSettings;
	if(ip.length != 0)
	{
		info("bindAddress:", ip);
		settings.bindAddresses = [ip];
	}
	settings.port = listenPort;
	settings.maxRequestSize = 1024 * 1024 * 10;
	info("http server settings.maxRequestSize:", settings.maxRequestSize);
	version (OSX)
	{
		info("osx os");
	}
	else version (Windows)
	{
		info("windows os");
	}
	else
	{
		info("linux os");
	}
	auto router = new URLRouter;
	router.registerRestInterface(new ImageImpl);
	router.registerRestInterface(new ImageServerImpl);

	
	router.get("/", delegate(HTTPServerRequest req, HTTPServerResponse res) {
		logRequest(req, res);
		res.writeBody("iamgecenter", "text/plain");
		logResponse(res);
	});
	
	
	auto routes = router.getAllRoutes();
	foreach(i; routes)
	{
		info("method:", i.method, ", pattern:", i.pattern);
	}
	listenHTTP(settings, router);

	log("listen url:", "http://*:" ~ to!string(listenPort));
	info("runEventLoop start");
	int ret = runEventLoop();
	info("runEventLoop end, ret:", ret);
}

void stopRESTServer()
{
	exitEventLoop();
}
