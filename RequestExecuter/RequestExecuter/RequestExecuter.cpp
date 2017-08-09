#include "stdafx.h"

#include <iostream>
#include <string.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <time.h>
#include <fstream>
#include <Windows.h>

#pragma comment(lib, "Ws2_32.lib")

#define BUFSIZE_SOCKET 200

using namespace std;

bool file_exists(const char *filename);
int executeRequest(string& request);
streampos getFileSize(const char *filename);
void print_error(const char* error);

bool standardNames = false;

HANDLE hConsole;

int main(int argc, char *argv[]) {
	long long bytesToProcess;
	string allRequests, request, sHelper, responseHeader, filename;
	size_t offset, nextOffset, requestOffset;
	WSADATA wsadata;
	ifstream file;
	char buffer[1];

	if (argc > 1) {
		if (argc == 2 && strcmp(argv[1], "-general") == 0) {
			standardNames = true;
		}
		else {
			cout << "Usage: RequestExecuter [-general]" << endl << "\tgeneral: received file has name response.[type]" << endl;
			return 1;
		}
	}

	//Get Console Output Handler
	hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

	//Start Winsock2
	if (WSAStartup(MAKEWORD(2, 2), &wsadata) == -1) {
		cerr << "Not able to start Winsock 2!" << endl;
		return 1;
	}

	//read request from file request.txt
	file.open("request.txt", ios::in);
	if (!file.good()) {
		print_error("File request.txt not found!");
		return 1;
	}
	bytesToProcess = getFileSize("request.txt");
	file.read(buffer, 1);
	while (!file.eof()) {
		allRequests += (buffer[0]);
		file.read(buffer, 1);
	}
	file.close();

	//offset marks the start of the request, requestOffset the end of it.
	offset = 0;
	while (offset < MAXSIZE_T) {
		
		
		requestOffset = MAXSIZE_T;
		for (const char *method : { "GET", "POST", "HEAD", "PUT", "DELETE", "CONNECT", "OPTIONS", "PATCH" }) {
			if ((nextOffset = allRequests.find(method, offset + 10)) != string::npos && nextOffset < requestOffset)
				requestOffset = nextOffset;
		}

		if (requestOffset == MAXSIZE_T)
			request = allRequests.substr(offset);
		else
			request = allRequests.substr(offset, requestOffset - offset);
		while (request.at(request.length() - 2) == '\n')
			request.erase(request.length() - 2, 1);
		
		offset = requestOffset;
		SetConsoleTextAttribute(hConsole, 14);
		cout << "Executing request:\n\n";
		SetConsoleTextAttribute(hConsole, 11);
		cout << request.c_str();
		SetConsoleTextAttribute(hConsole, 14);
		cout << "\n...\n\n";
		SetConsoleTextAttribute(hConsole, 7);
		if (executeRequest(request) != 0) {
			return 1;
		}

		
	}

	WSACleanup();
	SetConsoleTextAttribute(hConsole, 10);
	cout << "Finished all requests." << endl;
	SetConsoleTextAttribute(hConsole, 10);
	return 0;
}

//returns 0 when suceeded
int executeRequest(string& request) {
	size_t offset, nextOffset;
	long long bytesProcessed, bytesToProcess, totalSize, totalBytesProcessed;
	string sHelper, responseHeader, filename;
	const char* servername, *stringToSend;
	addrinfo *serverInfo;
	sockaddr_in *server;
	SOCKET s;
	ofstream file;
	char buffer[BUFSIZE_SOCKET], ipAddress[16];
	time_t start, timeVal;

	//find hostname in request and nslookup IP
	offset = request.find("Host:");
	if (offset == string::npos) {
		print_error("Request invalid!");
		return -1;
	}
	sHelper = request.substr(offset + 6, request.find("\n", offset) - offset - 6);
	servername = sHelper.c_str();

	if (getaddrinfo(servername, "80", NULL, &serverInfo) != 0) {
		SetConsoleTextAttribute(hConsole, 12);
		cerr << "Server " << servername << "not found!" << endl;
		SetConsoleTextAttribute(hConsole, 7);
		return 1;
	}

	server = (sockaddr_in*)serverInfo->ai_addr;
	server->sin_family = AF_INET;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		print_error("Error while creating socket.");
		return 1;
	}

	std::cout << "Connecting to Server " << servername << " (" << inet_ntop(AF_INET, &server->sin_addr, ipAddress, 16) << ", Port " << ntohs(server->sin_port) << ")..." << endl << endl;
	if (connect(s, (sockaddr*)server, sizeof(sockaddr_in)) != 0) {
		print_error("Connection to Server failed.");
		return 1;
	}

	SetConsoleTextAttribute(hConsole, 14);
	std::cout << "Sending request..." << endl << endl;
	SetConsoleTextAttribute(hConsole, 7);

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
		std::cout << stringToSend;
		bytesProcessed = 0;
		while (bytesToProcess > 0) {
			bytesProcessed = send(s, stringToSend + (int)bytesProcessed, (int)bytesToProcess, 0);
			bytesToProcess -= bytesProcessed;
		}
		offset = nextOffset + 1;
	}

	SetConsoleTextAttribute(hConsole, 14);
	std::cout << endl << "Receiving header..." << endl << endl;
	SetConsoleTextAttribute(hConsole, 7);

	totalSize = 4;
	recv(s, buffer, 4, 0);
	buffer[4] = 0;
	responseHeader.assign(buffer);
	while (!(responseHeader.at((int)totalSize - 3) == '\n' && responseHeader.at((int)totalSize - 2) == '\r' && responseHeader.at((int)totalSize - 1) == '\n')) {
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
			_itoa_s(counter, buffer, BUFSIZE_SOCKET, 10);
			sHelper.append(buffer);
			sHelper.append(filename.substr(offset));
			counter++;
		} while (file_exists(sHelper.c_str()));
		filename.assign(sHelper);
	}

	file.open(filename, ios::out | ios::binary | ios::trunc);

	if (totalSize >= 0) {
		SetConsoleTextAttribute(hConsole, 14);
		std::cout << "Receiving " << totalSize << " bytes into file " << filename.c_str() << " ..." << endl << endl;
		SetConsoleTextAttribute(hConsole, 13);
		totalBytesProcessed = 0;
		bytesToProcess = totalSize;
		start = time(0);
		while (bytesToProcess > 0) {
			bytesProcessed = recv(s, buffer, BUFSIZE_SOCKET, 0);
			file.write(buffer, bytesProcessed);
			totalBytesProcessed += bytesProcessed;
			//print some info every 200KB
			if (totalBytesProcessed % 200000 < bytesProcessed) {
				timeVal = time(0) - start;
				SetConsoleTextAttribute(hConsole, 10);
				printf("\r%.2f kb/s.\t", (float)totalBytesProcessed / 1000 / timeVal);
				timeVal = (time_t)((double)timeVal / totalBytesProcessed * bytesToProcess);
				SetConsoleTextAttribute(hConsole, 13);
				printf( "%.1f of %.1f MB (%lld%%);\t",
					(float)totalBytesProcessed / 1000000, (float)totalSize / 1000000, totalBytesProcessed * 100 / totalSize);
				cout.precision(1);
				SetConsoleTextAttribute(hConsole, 11);
				cout << fixed << timeVal << " seconds (" << (float)timeVal / 60 << " minutes) left.     ";
			}
			bytesToProcess -= bytesProcessed;
		}
	}
	else {
		SetConsoleTextAttribute(hConsole, 14);
		std::cout << "Receiving into file" << filename.c_str() << " ..." << endl << endl;
		SetConsoleTextAttribute(hConsole, 13);
		bytesProcessed = recv(s, buffer, BUFSIZE_SOCKET, 0);
		totalBytesProcessed = bytesProcessed;
		start = time(0);
		while (bytesProcessed > 0) {
			file.write(buffer, bytesProcessed);
			bytesProcessed = recv(s, buffer, BUFSIZE_SOCKET, 0);
			totalBytesProcessed += bytesProcessed;
			//print some info every 200KB
			if (totalBytesProcessed % 200000 < bytesProcessed) {
				timeVal = time(0) - start;
				printf("\r%.2f kb/s     ", (float)totalBytesProcessed / 1000 / timeVal);
			}
		}
	}

	file.close();
	closesocket(s);
	SetConsoleTextAttribute(hConsole, 10);
	std::cout << "\nDone.\n" << endl;
	SetConsoleTextAttribute(hConsole, 7);
	return 0;
}

streampos getFileSize(const char* filename) {
	ifstream file(filename);
	file.seekg(0, ios::end);
	streampos s = file.tellg();
	file.close();
	return s;
}

bool file_exists(const char *filename) {
	ifstream file(filename);
	bool exists = file.good();
	file.close();
	return exists;
}

void print_error(const char* error) {
	SetConsoleTextAttribute(hConsole, 12);
	cerr << error << endl;
	SetConsoleTextAttribute(hConsole, 7);
}