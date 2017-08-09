#include "stdafx.h"

#include <iostream>
#include <string.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <time.h>
#include <fstream>

#pragma comment(lib, "Ws2_32.lib")

#define BUFSIZE 500

using namespace std;

bool file_exists(const char *filename);

int main(int argc, char *argv[]) {
	bool standardNames = false;
	long long bytesToProcess, bytesProcessed, totalBytesProcessed, totalSize;
	string request, sHelper, responseHeader, filename;
	fstream file;
	sockaddr_in *server;
	char buffer[BUFSIZE], ipAddress[16];
	const char *stringToSend;
	const char *servername;
	size_t offset, nextOffset;
	addrinfo *serverInfo;
	SOCKET s;
	time_t start, timeVal;
	WSADATA wsadata;

	if (argc > 1) {
		if (argc == 2 && strcmp(argv[1], "-general") == 0) {
			standardNames = true;
		}
		else {
			cout << "Usage: RequestExecuter [-general]" << endl << "\tgeneral: received file has name response.[type]" << endl;
			return 1;
		}
	}

	//Start Winsock2
	if (WSAStartup(MAKEWORD(2, 2), &wsadata) == -1) {
		cerr << "Not able to start Winsock 2!" << endl;
		return 1;
	}

	//read request from file request.txt
	file.open("request.txt", ios::in);
	if (!file.good()) {
		cerr << "File request.txt not found!" << endl;
		return 1;
	}
	file.read(buffer, 1);
	while (!file.eof()) {
		request += buffer[0];
		file.read(buffer, 1);
	}
	file.close();

	//find hostname in request and nslookup IP
	offset = request.find("Host:");
	sHelper = request.substr(offset + 6, request.find("\n", offset) - offset - 6);
	servername = sHelper.c_str();

	if (getaddrinfo(servername, "80", NULL, &serverInfo) != 0) {
		cerr << "Server " << servername << "not found!" << endl;
		return 1;
	}

	server = (sockaddr_in*)serverInfo->ai_addr;
	server->sin_family = AF_INET;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		std::cout << "Error creating socket." << endl;
		return 1;
	}

	std::cout << "Connecting to Server " << servername << " (" << inet_ntop(AF_INET, &server->sin_addr, ipAddress, 16) << ", Port " << ntohs(server->sin_port) << ")..." << endl;
	if (connect(s, (sockaddr*)server, sizeof(sockaddr_in)) != 0) {
		cerr << "Connection to Server failed." << endl;
		return 1;
	}

	std::cout << "Sending request..." << endl;
	if (request.at(request.size() - 1) != '\n')
		request.append("\n\n");
	else
		request += '\n';
	offset = 0;
	while ((nextOffset = request.find('\n', offset)) != string::npos) {
		sHelper = request.substr(offset, nextOffset - offset);
		sHelper.append("\r\n");
		stringToSend = sHelper.c_str();
		bytesToProcess = strlen(stringToSend);
		std::cout << "\t" << stringToSend;
		bytesProcessed = 0;
		while (bytesToProcess > 0) {
			bytesProcessed = send(s, stringToSend + bytesProcessed, bytesToProcess, 0);
			bytesToProcess -= bytesProcessed;
		}
		offset = nextOffset + 1;
	}

	std::cout << "Receiving header..." << endl << endl;

	totalSize = 4;
	recv(s, buffer, 4, 0);
	buffer[4] = 0;
	responseHeader.assign(buffer);
	while (!(responseHeader.at(totalSize - 3) == '\n' && responseHeader.at(totalSize - 2) == '\r' && responseHeader.at(totalSize - 1) == '\n')) {
		recv(s, buffer, 1, 0);
		responseHeader += buffer[0];
		totalSize++;
	}
	std::cout << responseHeader.c_str() << endl;

	//Find content length
	if ((offset = responseHeader.find("Content-Length:")) == string::npos) {
		std::cout << "Server gave no content length" << endl;
		totalSize = -1;
	}
	else {
		offset += 16;
		sHelper = responseHeader.substr(offset, responseHeader.find('\r', offset) - offset);
		totalSize = atoll(sHelper.c_str());
	}

	//Get File Name; if parameter was set, name file response.[type]
	if (standardNames) {
		//Find content type
		filename.assign("response.");
		if ((offset = responseHeader.find("Content-Type:")) == string::npos) {
			std::cout << "Server gave no content type" << endl;
			filename.append("txt");
		}
		else {
			offset = responseHeader.find('/', offset) + 1;
			sHelper = responseHeader.substr(offset, responseHeader.find('\r', offset) - offset);
			filename.append(sHelper);
		}
	}
	else {
		offset = request.find_last_of('/', request.find("\n") - 8) + 1;
		filename = request.substr(offset, request.find("HTTP", offset) - 1 - offset);
	}

	//rename file in case it already exists
	int counter = 0;
	if (file_exists(filename.c_str())) {
		offset = filename.find_last_of('.');
		do {
			sHelper.assign(filename.substr(0, offset));
			sHelper.append(" - ");
			_itoa_s(counter, buffer, BUFSIZE, 10);
			sHelper.append(buffer);
			sHelper.append(filename.substr(offset));
			counter++;
		} while (file_exists(sHelper.c_str()));
		filename.assign(sHelper);
	}

	file.open(filename, ios::out | ios::binary | ios::trunc);

	if (totalSize >= 0) {
		std::cout << "Receiving " << totalSize << " bytes into file " << filename.c_str() << "..." << endl;
		totalBytesProcessed = 0;
		bytesToProcess = totalSize;
		start = time(0);
		while (bytesToProcess > 0) {
			bytesProcessed = recv(s, buffer, BUFSIZE, 0);
			file.write(buffer, bytesProcessed);
			totalBytesProcessed += bytesProcessed;
			//print some info every 10MB
			if (totalBytesProcessed % 10000000 < bytesProcessed) {
				timeVal = time(0) - start;
				std::cout << fixed;
				std::cout.precision(2);
				std::cout << "\tCurrent Download Speed " << (float)totalBytesProcessed / 1000 / timeVal << " kb/s." << endl;
				timeVal = (double)timeVal / totalBytesProcessed * bytesToProcess;
				std::cout << "Downloaded " << (float)totalBytesProcessed / 1000000 << " of " << (float)totalSize / 1000000 << " Megabytes (" << totalBytesProcessed * 100 / totalSize <<
					" %) ; estimated time until finish: " << timeVal << " seconds or " << (float)timeVal / 60 << " minutes." << endl;
			}
			bytesToProcess -= bytesProcessed;
		}
	}
	else {
		std::cout << "Receiving into file" << filename.c_str() << "..." << endl;
		bytesProcessed = recv(s, buffer, BUFSIZE, 0);
		totalBytesProcessed = bytesProcessed;
		start = time(0);
		while (bytesProcessed > 0) {
			file.write(buffer, bytesProcessed);
			bytesProcessed = recv(s, buffer, BUFSIZE, 0);
			totalBytesProcessed += bytesProcessed;
			//print some info every 10MB
			if (totalBytesProcessed % 10000000 < bytesProcessed) {
				timeVal = time(0) - start;
				std::cout << fixed;
				std::cout.precision(2);
				std::cout << "\tCurrent Download Speed " << (float)totalBytesProcessed / 1000 / timeVal << " kb/s." << endl;
			}
		}
	}

	file.close();
	closesocket(s);
	std::cout << "Done." << endl;

	WSACleanup();

	return 0;
}


bool file_exists(const char *filename) {
	ifstream file(filename);
	return file.good();
}