#include "stdafx.h"

#include <iostream>
#include <string.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include <time.h>
#include <fstream>
#include <Windows.h>
#include <thread>
#include <sstream>

#pragma comment(lib, "Ws2_32.lib")

#define BUFSIZE_SOCKET 10000

using namespace std;

bool file_exists(const char *filename);
int executeRequestToFile(string& request);
int executeRequestbyThreading(const string& request, long long maxContentLength, int threads = 1);
void executeRequest(string& request, ofstream *file, sockaddr_in* server, string* out_Response = NULL);
void executePartialRequests(const string& request, ofstream* file, sockaddr_in* server, long long firstByte, long long endByte, long long step);
streampos getFileSize(const char *filename);
void print_error(const char* error);
void writeToFile(ofstream* file, const char *buffer, long long size);
void doNothing();
int findString(char *string, char **list, int size);
string getFileNameFromRequest(const string& request);
void printDownloadSpeed(long long totalBytes);


bool standardNames = false;
volatile long long progressBytes = 0;

HANDLE hConsole;

int main(int argc, char *argv[]) {
	long long bytesToProcess, maxBytesPerPart = MAXLONGLONG;
	string allRequests, request, sHelper, responseHeader, filename;
	size_t offset, nextOffset, requestOffset;
	WSADATA wsadata;
	ifstream file;
	char buffer[1];
	int argcheck = 1, threads = 1, arg;
	if (argc > 1) {
		if (findString("-general", argv, argc) >= 0) {
			standardNames = true;
			++argcheck;
		}
		if ((arg = findString("-max", argv, argc)) >= 0) {
			maxBytesPerPart = atoll(argv[arg + 1]);
			argcheck += 2;
		}
		if ((arg = findString("-threads", argv, argc)) >= 0) {
			threads = atoi(argv[arg + 1]);
			argcheck += 2;
		}
		//Print usage information if arguments were used wrong
		if (argc != argcheck) {
			cout << "Usage: RequestExecuter [-general] [-max <Number>] [-threads <Number>]" << "\n\tgeneral: received file has name 'response.[type]'\n" <<
				"\tmax: response will be downloaded in parts, each part max bytes\n\tthreads: response will be downloaded in parts that will be downloaded parallel" << endl;
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
		while (request.at(request.length() - 1) == '\n')
			request.erase(request.length() - 1, 1);
		
		offset = requestOffset;
		SetConsoleTextAttribute(hConsole, 14);
		cout << "Executing request:\n\n";
		SetConsoleTextAttribute(hConsole, 11);
		cout << request.c_str();
		SetConsoleTextAttribute(hConsole, 14);
		cout << "\n...\n\n";
		SetConsoleTextAttribute(hConsole, 7);
		if (threads > 1 || maxBytesPerPart != MAXLONGLONG) {
			executeRequestbyThreading(request, maxBytesPerPart, threads);
		}
		else {
			if (executeRequestToFile(request) != 0) {
				return 1;
			}
		}

		
	}

	WSACleanup();
	SetConsoleTextAttribute(hConsole, 10);
	cout << "Finished all requests." << endl;
	SetConsoleTextAttribute(hConsole, 7);
	return 0;
}


//returns 0 when suceeded, executes request, saves response in file named after the request
int executeRequestToFile(string& request) {
	size_t offset, nextOffset;
	long long bytesProcessed, bytesToProcess, totalSize, totalBytesProcessed;
	string sHelper, responseHeader, filename;
	const char* servername, *stringToSend;
	addrinfo *serverInfo;
	sockaddr_in *server;
	SOCKET s;
	ofstream file;
	char buffer1[BUFSIZE_SOCKET], buffer2[BUFSIZE_SOCKET], ipAddress[16];
	char *curBuffer = buffer1;
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
		cerr << "Couldn't find Server " << servername << "in DNS!" << endl;
		SetConsoleTextAttribute(hConsole, 7);
		return 1;
	}

	//open file
	file.open(getFileNameFromRequest(request), ios::out | ios::binary | ios::trunc);

	//Setup Connection to server
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
	recv(s, buffer1, 4, 0);
	buffer1[4] = 0;
	responseHeader.assign(buffer1);
	while (!(responseHeader.at((int)totalSize - 3) == '\n' && responseHeader.at((int)totalSize - 2) == '\r' && responseHeader.at((int)totalSize - 1) == '\n')) {
		recv(s, buffer1, 1, 0);
		responseHeader += buffer1[0];
		totalSize++;
	}
	std::cout << responseHeader.c_str() << endl;

	//Return from function here if only the HEAD was requested
	if (request.substr(0, 4).compare("HEAD") == 0) {
		return 0;
	}

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

	//Receive and write into File
	//System works like this: two buffers, receive in one buffer. Then let thread write the buffer into file and receive
	//in the other buffer. Then swap.
	curBuffer = buffer1;
	thread tWrite(doNothing);
	if (totalSize >= 0) {
		SetConsoleTextAttribute(hConsole, 14);
		std::cout << "Receiving " << totalSize << " bytes into file " << filename.c_str() << " ..." << endl << endl;
		SetConsoleTextAttribute(hConsole, 13);
		totalBytesProcessed = 0;
		bytesToProcess = totalSize;
		start = time(0);
		while (bytesToProcess > 0) {
			bytesProcessed = recv(s, curBuffer, BUFSIZE_SOCKET, 0);
			tWrite.join();
			//thread writes current buffer into File
			tWrite = thread(writeToFile, &file, curBuffer, bytesProcessed);
			//swap Buffers
			if (curBuffer == buffer1)
				curBuffer = buffer2;
			else
				curBuffer = buffer1;
			totalBytesProcessed += bytesProcessed;
			//print some info every 500KB
			if (totalBytesProcessed % 500000 < bytesProcessed) {
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
		bytesProcessed = recv(s, curBuffer, BUFSIZE_SOCKET, 0);
		totalBytesProcessed = bytesProcessed;
		start = time(0);
		while (bytesProcessed > 0) {
			tWrite.join();
			tWrite = thread(writeToFile, &file, curBuffer, bytesProcessed);
			//swap buffers
			if (curBuffer == buffer1)
				curBuffer = buffer2;
			else
				curBuffer = buffer1;
			bytesProcessed = recv(s, curBuffer, BUFSIZE_SOCKET, 0);
			totalBytesProcessed += bytesProcessed;
			//print some info every 500KB
			if (totalBytesProcessed % 500000 < bytesProcessed) {
				timeVal = time(0) - start;
				printf("\r%.2f kb/s     ", (float)totalBytesProcessed / 1000 / timeVal);
			}
		}
	}
	tWrite.join();

	file.close();
	closesocket(s);
	SetConsoleTextAttribute(hConsole, 10);
	std::cout << "\nDone.\n" << endl;
	SetConsoleTextAttribute(hConsole, 7);
	return 0;
}


//Downloads the file by using range=0-maxContentLength and so on to split the requests into multiple ones which can be executed parallel
//This only works if the server gives information about the content length
//TODO: This currently only works for 1 thread. 
int executeRequestbyThreading(const string& request, long long maxContentLength, int numThreads) {
	size_t offset;
	
	string sHelper, filename;
	const char* servername;
	addrinfo *serverInfo;
	sockaddr_in *server;
	ofstream file;
	long long fileSize;
	thread **threads;

	//find hostname in request and nslookup IP
	offset = request.find("Host:");
	if (offset == string::npos) {
		print_error("Request invalid!");
		return -1;
	}
	sHelper = request.substr(offset + 6, request.find("\n", offset) - offset - 6);
	servername = sHelper.c_str();

	if (getaddrinfo(servername, "80", NULL, &serverInfo) != 0) {
		print_error("Couldn't Find Server in DNS!");
		return 1;
	}

	server = (sockaddr_in*)serverInfo->ai_addr;
	server->sin_family = AF_INET;


	filename = getFileNameFromRequest(request);
	file.open(filename, ios::trunc | ios::binary);

	//Get Content Length
	sHelper.assign("HEAD");
	sHelper.append(request.substr(request.find(' ')));
	offset = sHelper.find_last_of("\n");
	do {
		--offset;
	} while (sHelper.at(offset) == '\n');
	sHelper.insert(offset + 1, "\nRange: bytes=0-1");

	executeRequest(string(sHelper), &file, server, &sHelper);
	
	if ((offset = sHelper.find("Content-Range:")) != string::npos) {
		offset = sHelper.find("/", offset) + 1;
		sHelper = sHelper.substr(offset, sHelper.find('\n', offset) - offset);
		//The file Size is the maximum Byte Index + 1
		fileSize = atoll(sHelper.c_str()) + 1;
	}
	else {
		//TODO: doesn't work yet
		return -1;
	}

	SetConsoleTextAttribute(hConsole, 14);
	std::cout << "Receiving " << fileSize << " bytes into file " << filename.c_str() << " ..." << endl << endl;
	SetConsoleTextAttribute(hConsole, 13);

	progressBytes = 0;
	threads = new thread*[numThreads];
	int i;
	for (i = 0; i < numThreads; ++i) {
		threads[i] = new thread(executePartialRequests, request, &file, server, i * fileSize / numThreads, (i + 1) / numThreads * fileSize - 1, maxContentLength);
	}
	thread infoThread = thread(printDownloadSpeed, fileSize);

	for (int i = 0; i < numThreads; ++i) {
		threads[i]->join();
	}
	infoThread.join();

	return 0;
}



//downloads part of the file from the request by adding a Range modifier.
void executePartialRequests(const string& request, ofstream* file, sockaddr_in* server, long long firstByte, long long endByte, long long step) {
	int InsertIdx = request.find_last_not_of("\n") + 1;
	string s;
	stringstream rangeStr;
	long long bytesToProcess;

	for (; firstByte <= endByte; firstByte += step) {
		bytesToProcess = min(firstByte + step - 1, endByte);
		s.assign(request);
		rangeStr.str(string());
		rangeStr << "\nRange: bytes=" << firstByte << "-" << bytesToProcess;
		s.insert(InsertIdx, rangeStr.str());
		executeRequest(s, file, server);
		progressBytes += bytesToProcess;
	}

}


void executeRequest(string& request, ofstream *file, sockaddr_in* server, string* out_Response) {
	int offset, nextOffset;
	string sHelper, responseHeader;
	const char *stringToSend;
	long long bytesProcessed, bytesToProcess, totalSize, totalBytesProcessed;
	char buffer1[BUFSIZE_SOCKET], buffer2[BUFSIZE_SOCKET], *curBuffer;

	if (!file->is_open()) {
		print_error("ERROR: File must be opened when calling executeRequestThread");
		return;
	}
	SOCKET s;

	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		print_error("Error while creating socket.");
		return;
	}
	//Connecting to server
	if (connect(s, (sockaddr*)server, sizeof(sockaddr_in)) != 0) {
		print_error("Connection to Server failed.");
		return;
	}

	
	//Sending request
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
		bytesProcessed = 0;
		while (bytesToProcess > 0) {
			bytesProcessed = send(s, stringToSend + (int)bytesProcessed, (int)bytesToProcess, 0);
			bytesToProcess -= bytesProcessed;
		}
		offset = nextOffset + 1;
	}

	//Receiving header
	totalSize = 4;
	recv(s, buffer1, 4, 0);
	buffer1[4] = 0;
	responseHeader.assign(buffer1);
	while (!(responseHeader.at((int)totalSize - 3) == '\n' && responseHeader.at((int)totalSize - 2) == '\r' && responseHeader.at((int)totalSize - 1) == '\n')) {
		recv(s, buffer1, 1, 0);
		responseHeader += buffer1[0];
		totalSize++;
	}
	//copy response if wanted by caller
	if (out_Response != NULL)
		*out_Response = string(responseHeader);
	//Return from function here if only the HEAD was requested
	if (request.substr(0, 4).compare("HEAD") == 0) {
		return;
	}

	//Find content length
	if ((offset = responseHeader.find("Content-Length:")) == string::npos) {
		totalSize = -1;
	}
	else {
		offset += 16;
		sHelper = responseHeader.substr(offset, responseHeader.find('\r', offset) - offset);
		totalSize = atoll(sHelper.c_str());
	}

	curBuffer = buffer1;
	thread tWrite(doNothing);

	totalBytesProcessed = 0;
	bytesToProcess = (totalSize == -1) ? MAXLONGLONG : totalSize;
	while (bytesToProcess > 0) {
		bytesProcessed = recv(s, curBuffer, BUFSIZE_SOCKET, 0);
		tWrite.join();
		//thread writes current buffer into File
		tWrite = thread(writeToFile, file, curBuffer, bytesProcessed);
		//swap Buffers
		if (curBuffer == buffer1)
			curBuffer = buffer2;
		else
			curBuffer = buffer1;
		totalBytesProcessed += bytesProcessed;
		bytesToProcess -= bytesProcessed;
		//Leave function if server doesn't send anymore
		if (bytesProcessed == 0)
			break;
	}
	tWrite.join();
	closesocket(s);

}


void writeToFile(ofstream *file, const char* buffer, long long length) {
	file->write(buffer, length);
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

void doNothing() {

}

int findString(char *string, char **list, int size) {
	for (int i = 0; i < size; ++i) {
		if (strcmp(string, list[i]) == 0)
			return i;
	}
	return -1;
}

//Prints information about Download Speed and such

void printDownloadSpeed(long long totalBytes) {
	long long speed = 1;
	int timeElapsed = 0;
	//stop two seconds before the download finishes
	while (progressBytes + 2 * speed < totalBytes) {
		std::this_thread::sleep_for(1s);
		timeElapsed += 1;
		SetConsoleTextAttribute(hConsole, 10);
		speed = progressBytes / timeElapsed + 1; // + 1 to avoid Division by Zero error
		printf("\r%.1f kb/s.\t", (float)speed / 1000.f);
		SetConsoleTextAttribute(hConsole, 13);
		printf("%.1f of %.1f MB (%lld%%);\t",
			(float)progressBytes / 1000000, (float)totalBytes / 1000000, progressBytes * 100 / totalBytes);
		cout.precision(1);
		SetConsoleTextAttribute(hConsole, 11);
		long long time = (totalBytes - progressBytes) / speed;
		cout << fixed << time << " seconds (" << (float)time / 60.f << " minutes) left.         ";
	}
	SetConsoleTextAttribute(hConsole, 7);
	//clear line
	printf("\r                                                                                                      \n");
}

string getFileNameFromRequest(const string& request) {
	string filename, sHelper;
	int offset, nextOffset;
	char buffer[10];

	//Get File Name; if parameter was set, name file response.[type]
	offset = request.find('\n') - 8;
	nextOffset = request.find_last_of("?", offset);
	if (request.find('?') != string::npos)
		offset = request.find("?") - 1;
	while (request.at(offset) == '/')
		--offset;
	nextOffset = request.find_last_of('/', offset);
	filename = request.substr(nextOffset + 1, offset - nextOffset);

	if (standardNames) {
		//Find content type
		if ((offset = filename.find_last_of('.')) == string::npos) {
			filename.assign("response.txt");
		} else {
			sHelper = filename.substr(offset);
			filename.assign("response");
			filename.append(sHelper);
		}
	}

	//rename file in case it already exists
	int counter = 0;
	if (file_exists(filename.c_str())) {
		offset = filename.find_last_of('.');
		do {
			sHelper.assign(filename.substr(0, offset));
			sHelper.append(" - ");
			_itoa_s(counter, buffer, 10, 10);
			sHelper.append(buffer);
			sHelper.append(filename.substr(offset));
			counter++;
		} while (file_exists(sHelper.c_str()));
	}
	return sHelper;
}