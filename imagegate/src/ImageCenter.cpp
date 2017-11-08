#include "ImageCenter.h"
#include <map>
#include <mutex>
#include <windows.h>
#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string.hpp>
#include "model/AppConf.h"
#include <basecpp/Log.h>

std::map<DWORD/*thread id*/, basecpp::HTTPClient*> gHttpClients;
std::mutex gMutex;

basecpp::HTTPClient* getHttpClient()
{
	std::lock_guard<std::mutex> lock(gMutex);
	DWORD threadId = ::GetCurrentThreadId();
	auto it = gHttpClients.find(threadId);
	if (it == gHttpClients.end())
	{
		basecpp::HTTPClient* httpClient = new basecpp::HTTPClient;
		LOG(error) << "new httpclient, ptr:" << httpClient;
		gHttpClients.insert(std::make_pair(threadId, httpClient));
		return httpClient;
	}
	else
	{
		return it->second;
	}
}

void listImageInImageCenter(basecpp::HTTPClient& httpClient, std::string path, bool onlyShowActive, ImageGetRep& rep)
{
	boost::algorithm::trim_left_if(path, boost::algorithm::is_any_of("\\"));
	boost::algorithm::replace_all(path, "\\", "/");
	basecpp::HTTPRequest httpReq;
	httpReq.url_ = boost::str(boost::format("http://%s:%d/image?path=%s") % model::AppConfSingleton::get_const_instance().imageCenterIP_
		% model::AppConfSingleton::get_const_instance().imageCenterPort_ % path);
	httpReq.action_ = "GET";
	LOG(info) << "http:" << httpReq.url_;
	basecpp::HTTPResult httpResult = httpClient.request(httpReq, model::AppConfSingleton::get_const_instance().imageCenterRequestTimeout_);
	if (httpResult.code_ != 0)
	{
		LOG(error) << " http fail, curl code:" << httpResult.code_ << ", msg:" << httpResult.errorMsg_;
		return;
	}
	if (httpResult.response_.statusCode_ != 200)
	{
		LOG(error) << "http fail, status code:" << httpResult.response_.statusCode_;
		return;
	}
	if (httpResult.response_.contentBody_.size() == 0)
	{
		LOG(error) << "http contentbody size is 0";
		return;
	}
	
	boost::property_tree::ptree pt;
	std::string bodyStr(httpResult.response_.contentBody_.begin(), httpResult.response_.contentBody_.end());
	bodyStr.append("");
	LOG(info) << "image center rep:" << bodyStr;
	std::stringstream ss(bodyStr);
	boost::property_tree::read_json(ss, pt);
	rep.code = pt.get<int>("code", 0);
	rep.msg = pt.get<std::string>("msg", "");

	boost::property_tree::ptree childs = pt.get_child("items");
	for (auto child : childs)
	{
		Image item;
		item.name_ = child.second.get<std::string>("name");
		item.path_ = child.second.get<std::string>("path");
		item.isDir_ = child.second.get<bool>("isDir");
		item.isActive_ = child.second.get<bool>("isActive", false);
		if (onlyShowActive && !item.isActive_)
		{
			continue;
		}
		item.isUploading_ = child.second.get<bool>("isUploading", false);
		item.size_ = child.second.get<int64_t>("size", 0);
		item.isOnline_ = child.second.get<bool>("isOnline");
		item.format_ = child.second.get<std::string>("format");
		item.localPathInImageServer_ = child.second.get<std::string>("localPathInImageServer", "");
		item.smbPathInImageServer_ = child.second.get<std::string>("smbPathInImageServer", "");
		item.nfsPathInImageServer_ = child.second.get<std::string>("nfsPathInImageServer");
		item.volumeId_ = child.second.get<std::string>("volumeId");
		item.pathInVolume_ = child.second.get<std::string>("pathInVolume");
		item.sizeInVolume_ = child.second.get<int64_t>("sizeInVolume", 0);
		rep.items.push_back(item);
	}
}


void listImageInImageCenter(std::string path, bool onlyShowActive, ImageGetRep& rep)
{
	basecpp::HTTPClient* httpClient = getHttpClient();
	listImageInImageCenter(*httpClient, path, onlyShowActive, rep);
}
