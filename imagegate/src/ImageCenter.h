#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <basecpp/HTTPClient.h>

struct Image 
{
	std::string name_;
	std::string path_;
	bool isDir_;
	int64_t size_ = 0;
	bool isOnline_;
	std::string format_;
	std::string smbPathInImageServer_;
	std::string nfsPathInImageServer_;
	std::string volumeLabelName_;
	std::string pathInVolume_;
	int64_t sizeInVolume_ = 0;
};

struct ImageGetRep
{
	int code;
	std::string msg;
	std::vector<Image> items;
};

void listImageInImageCenter(basecpp::HTTPClient& httpClient,  std::string path, ImageGetRep& rep);


void listImageInImageCenter(std::string path, ImageGetRep& rep);