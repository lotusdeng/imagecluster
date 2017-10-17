#include "AppConf.h"
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>  
#include <boost/property_tree/xml_parser.hpp> 
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

namespace model
{
	void AppConf::LoadConf(const std::string& confFilePath)
	{

		boost::property_tree::ptree pt;
		boost::property_tree::read_xml(confFilePath, pt);

		coreLogLevel_ = pt.get("root.log.coreLogLevel", "info");
		fileLogLevel_ = pt.get<std::string>("root.log.file.<xmlattr>.level", "info");
		fileLogRotationMBSize_ = pt.get<int>("root.log.file.<xmlattr>.rotationMBSize", 512);
		fileLogAutoFlush_ = pt.get<bool>("root.log.file.<xmlattr>.autoFlush", true);
		consoleLogLevel_ = pt.get<std::string>("root.log.console.<xmlattr>.level", "info");
		imageCenterIP_ = pt.get<std::string>("root.imageCenter.<xmlattr>.ip", "127.0.0.1");
		imageCenterPort_ = pt.get<int>("root.imageCenter.<xmlattr>.port", 9090);
		imageCenterRequestTimeout_ = pt.get<int>("root.imageCenter.<xmlattr>.requestTimeout", 5);
		mountPoint_ = pt.get<std::string>("root.mountPoint", "C:\\imageroot");
		dokanOptionDebug_ = pt.get<bool>("root.dokanOption.<xmlattr>.debug", true);
		dokanOptionStderr_ = pt.get<bool>("root.dokanOption.<xmlattr>.stderr", true);
		dokanOptionThreadCount_ = pt.get<int>("root.dokanOption.<xmlattr>.threadCount", 1);
	}
}



