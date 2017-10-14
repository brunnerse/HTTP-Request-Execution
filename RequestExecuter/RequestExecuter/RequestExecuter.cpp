#include "stdafx.h"

#include <iostream>
#include <string.h>

#include "RequestExecution.h"

using namespace std;
using namespace RequestExecution;

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
			setStandardNames(true);
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
		SetConsoleTextAttribute(*getConsoleHandle(), 14);
		cout << "Executing request:\n\n";
		SetConsoleTextAttribute(*getConsoleHandle(), 11);
		cout << request.c_str();
		SetConsoleTextAttribute(*getConsoleHandle(), 14);
		cout << "\n...\n\n";
		SetConsoleTextAttribute(*getConsoleHandle(), 7);
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
	SetConsoleTextAttribute(*getConsoleHandle(), 10);
	cout << "Finished all requests." << endl;
	SetConsoleTextAttribute(*getConsoleHandle(), 7);
	return 0;
}


