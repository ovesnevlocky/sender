// UDP_Communication_Framework.cpp : Defines the entry point for the console application.
//

#pragma comment(lib, "ws2_32.lib")
#include "stdafx.h"
#include <winsock2.h>
#include <stdint.h>
#include "ws2tcpip.h"
#include <errno.h>
#include <string.h>
#include "md5.h"
#include "crc32.h"
#include <stdbool.h>

//maxsize for udp is 65507 bytes 
#define BUFFERS_LEN 1024
#define RECV_TIMEOUT_MS 4000

enum
{
	SYNC,
	DATA,
	ACK,
	NAK,
	MASK,
	STOP
};

typedef uint8_t u8;
typedef uint32_t u32;

struct PacketStruct
{
	//u32 packet_type;
	u8  packet_type;
	u32 packet_len;
	u32 crc32;
	//keep track of position in a file
	u32 pos;
	u8 md5[16];
	u8 payload[BUFFERS_LEN];

}packet;

//#define TARGET_IP	"10.1.6.72"
#define TARGET_IP "127.0.0.1"


#define SENDER
#ifdef SENDER
//#define TARGET_PORT 5111
//#define LOCAL_PORT 5222
//for net deper
#define TARGET_PORT 5333
#define LOCAL_PORT 5444
#endif // SENDER


void InitWinsock()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
}

void prepare(struct sockaddr_in* local, struct sockaddr_in* addrDst)
{
	local->sin_family = AF_INET;
	local->sin_port = htons(LOCAL_PORT);
	local->sin_addr.s_addr = INADDR_ANY;

	addrDst->sin_family = AF_INET;
	addrDst->sin_port = htons(TARGET_PORT);
	InetPton(AF_INET, _T(TARGET_IP), &(addrDst->sin_addr.s_addr));

}

bool isTimeout()
{
	int err = WSAGetLastError();
	return err == WSAETIMEDOUT;
}

void printError()
{
	int err = WSAGetLastError();
	switch (err)
	{
	case WSAETIMEDOUT:
		printf("error: timeout (%d)\n", err);
		break;
	case WSAENOTSOCK:
		printf("error: invalid socket (%d)\n", err);
		break;
	case WSAEMSGSIZE:
		printf("error: message too large (%d)\n", err);
		break;
	case WSAENETUNREACH:
		printf("error: network unreachable (%d)\n", err);
		break;
	case WSANOTINITIALISED:
		printf("error: WSAStartup not called (%d)\n", err);
		break;
	default:
		printf("error: unknown error (%d)\n", err);
		break;
	}
}


bool setTimeout(SOCKET *socketS)
{
	WSASetLastError(0);
	int check = 0;
	DWORD timeout = RECV_TIMEOUT_MS;
	check = setsockopt(*socketS, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	if (check == SOCKET_ERROR)
	{
		printf("error in setting timeout\n");
		return false;
	}

	return true;

}
//**********************************************************************
int main()
{
	SOCKET socketS;

	InitWinsock();

	struct sockaddr_in local = { 0 };
	struct sockaddr_in addrDst = { 0 };
	struct PacketStruct dataReceived = { 0 };
	struct PacketStruct dataToSend = { 0 };
	
	int addrDstlen = sizeof(addrDst);
	int localLen = sizeof(local);

	char* inputFile = "monk.jpeg";
	FILE* fp = fopen(inputFile, "rb");
	if (!fp)
		printf("%s\n", strerror(errno));


	prepare(&local, &addrDst);
	socketS = socket(AF_INET, SOCK_DGRAM, 0);
	if (bind(socketS, (sockaddr*)&local, sizeof(local)) != 0)
	{
		printf("Binding error!\n");
		getchar(); //wait for press Enter
		return 1;
	}
	int count = 0;
	u32 pos, crc32;
	int sendingPacketLength, check, i, bytesReceived;
	pos = crc32 = sendingPacketLength = check = i = bytesReceived = 0;
	
	//send greetings
	dataToSend.packet_type = SYNC;
	bytesReceived = sendto(socketS, (char*)&dataToSend, sizeof(dataToSend), 0, (sockaddr*)&addrDst, sizeof(addrDst));
	
	if (bytesReceived == SOCKET_ERROR)
		printf("erroror after sendto\n");
	
	printf("hello before recfrom outof whileloop\n");

	setTimeout(&socketS);
		
	//bytesReceived = recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&addrDst, &addrDstlen);

	bytesReceived = recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&local, &localLen);
	printf("hello AFter recfrom outof whileloop\n");

	if (dataReceived.packet_type == SYNC)
		printf("got sync\n");
	else if(isTimeout())
		printf("timeout!\n");


	if (bytesReceived == SOCKET_ERROR)
	{
		printf("socket error: %i\n", bytesReceived);
		printError();
		exit(1);
		//while (1)
		/*
		///{
			count++;
			dataToSend.packet_type = SYNC;
			bytesReceived = sendto(socketS, (char*)&dataToSend, sizeof(dataToSend), 0, (sockaddr*)&addrDst, sizeof(addrDst));

			setTimeout(&socketS);
			bytesReceived = recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&addrDst, &addrDstlen);
			if (!isTimeout())
				break;
			if (count >= 100)
				break;
		}
		*/
		
	}
	count = 0;

	if (dataReceived.packet_type != SYNC)
	{
		printf("sync failed got: %i\n", dataReceived.packet_type);
		exit(1);
	}
	else if(dataReceived.packet_type == SYNC)
		printf("got sync, start the communication\n");

	bool isAfterTimeout = false;
	while (1)
	{

		fseek(fp, pos, SEEK_SET);
		count++;
		if (!isAfterTimeout)
		{
			sendingPacketLength = fread(dataToSend.payload, sizeof(u8), sizeof(dataToSend.payload), fp);
			dataToSend.packet_type = DATA;
			dataToSend.pos = pos;
			dataToSend.packet_len = sendingPacketLength;
			//set crc32
			dataToSend.crc32 = CRC_CalculateCRC32(dataToSend.payload, dataToSend.packet_len);
		}

		if (sendingPacketLength <= 0)
			break;
		
		sendto(socketS, (char*)&dataToSend, sizeof(dataToSend), 0, (sockaddr*)&addrDst, sizeof(addrDst));
		printf("sending...: %lu\n", pos);
		//reset this packet everytime to avoid confusion
		memset(&dataReceived, 0, sizeof(dataReceived));
		// if not getting the reply, then send the same packet//
	
		setTimeout(&socketS);
		//check = recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&addrDst, &addrDstlen);
		bytesReceived = recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&local, &localLen);
		

		if (isTimeout() || check == -1)
		{
			printf("%i: timeout, sending the same data\n", count);
			isAfterTimeout = true;
			continue;
		}
		isAfterTimeout = false;
		//update pos here 
		pos += dataToSend.packet_len;
		switch (dataReceived.packet_type)
		{
		case ACK:
		{
			pos = dataReceived.pos;
			break;
		}
		case NAK:
		{ 
			printf("got nak\n");
			pos -= dataToSend.packet_len;
			//pos = pos < 0 ? 0 : pos;
			pos = dataReceived.pos;
			break;
		}

		}
		
	
	}

	printf("finish Sending packet.\n");
	u32 TotalLen = dataToSend.pos + dataToSend.packet_len;

	char* resInput = (char*)calloc(TotalLen + 1, 1);

	memset(&dataToSend, 0, sizeof(dataToSend));
	rewind(fp);
	TotalLen = fread(resInput, sizeof(u8), TotalLen, fp);
	printf("size is: %lu\n", TotalLen);
	resInput[TotalLen] = '\0';
	md5String(resInput, dataToSend.md5);

	print_hash(dataToSend.md5);
	dataToSend.packet_type = STOP;
	sendto(socketS, (char*)&dataToSend, sizeof(dataToSend), 0, (sockaddr*)&addrDst, sizeof(addrDst));



	closesocket(socketS);
	fclose(fp);
	free(resInput);

	getchar(); //wait for press Enter

	return 0;
}
