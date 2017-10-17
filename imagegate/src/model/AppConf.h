#pragma once
#include <string>

#include <boost/serialization/singleton.hpp>

namespace model
{
	struct AppConf
	{
		std::string coreLogLevel_;
		std::string fileLogLevel_;
		int fileLogRotationMBSize_;
		bool fileLogAutoFlush_;
		std::string consoleLogLevel_;
		std::string imageCenterIP_;
		int imageCenterPort_;
		int imageCenterRequestTimeout_;
		std::string mountPoint_;
		bool dokanOptionDebug_;
		bool dokanOptionStderr_;
		int dokanOptionThreadCount_;
		
		void LoadConf(const std::string& confFilePath);
	};

	typedef boost::serialization::singleton<AppConf> AppConfSingleton;
}

