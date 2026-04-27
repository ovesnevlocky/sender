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
#define BUFFERS_LEN 1024

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
#define TARGET_PORT 5111
#define LOCAL_PORT 5222
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
//**********************************************************************
int main()
{
	SOCKET socketS;

	InitWinsock();

	struct sockaddr_in local = { 0 };
	struct sockaddr_in addrDst = { 0 };
	struct PacketStruct dataReceived = { 0 };
	struct PacketStruct dataToSend = { 0 };
	//struct timeval timeout;
	//timeout.tv_sec = 3;
	//timeout.tv_usec = 0;

	int addrDstlen = sizeof(addrDst);

	char* inputFile = "monk2.jpg";
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
	int sendingPacketLength = 0;
	u32 pos = 0;

	int check = 0;
	int i = 0;
	u32 crc32;
	int bytesReceived = 0;
	//send greetings
	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	dataToSend.packet_type = SYNC;

	check = setsockopt(socketS, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
	//bytesReceived = sendto(socketS, (char*)&dataToSend, sizeof(dataToSend), 0, (sockaddr*)&addrDst, sizeof(addrDst));
	
	if (check == SOCKET_ERROR)
		printf("error in setting timeout\n");

	int err = WSAGetLastError();
	if (err == WSAETIMEDOUT)
	{
		printf("timeout!\n");
		exit(1);
	}
	bytesReceived = recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&addrDst, &addrDstlen);

	if (bytesReceived == SOCKET_ERROR && errno == EWOULDBLOCK)
	{
		printf("socket timeout!\n");
		exit(1);
	}

	if (dataReceived.packet_type != SYNC)
	{
		printf("sync failed got: %i\n", dataReceived.packet_type);
		exit(1);
	}
	printf("here we go\n");
	while (1)
	{
		
		fseek(fp, pos, SEEK_SET);
		sendingPacketLength = fread(dataToSend.payload, sizeof(u8), sizeof(dataToSend.payload), fp);
		if (sendingPacketLength <= 0)
			break;

		dataToSend.packet_type = DATA;
		dataToSend.pos = pos;

		dataToSend.packet_len = sendingPacketLength;

		pos += dataToSend.packet_len;

		//set crc32
		dataToSend.crc32 = CRC_CalculateCRC32(dataToSend.payload, dataToSend.packet_len);
		//printf("0x%X\n", dataToSend.crc32);

		sendto(socketS, (char*)&dataToSend, sizeof(dataToSend), 0, (sockaddr*)&addrDst, sizeof(addrDst));
		//reset this packet everytime to avoid confusion
		memset(&dataReceived, 0, sizeof(dataReceived));
		// if not getting the reply, then send the same packet//
		check = recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&addrDst, &addrDstlen);
		//int timeout = 3000; // 3sec
		//setsockopt(socketS, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
		switch (dataReceived.packet_type)
		{
		case ACK:
		{
			pos = dataReceived.pos;
			break;
		}
		case NAK: 
		{
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