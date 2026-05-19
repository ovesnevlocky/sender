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
#define WINDOW_SIZE 5

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

//#define TARGET_IP "10.1.6.72"
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


bool setTimeout(SOCKET* socketS)
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

    char* inputFile = "utada.jpeg";
    FILE* fp = fopen(inputFile, "rb");
    if (!fp)
        printf("%s\n", strerror(errno));

    fseek(fp, 0, SEEK_END);
    u32 fileSize = ftell(fp);
    rewind(fp);

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

    bytesReceived = recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&local, &localLen);
    printf("hello AFter recfrom outof whileloop\n");

    if (dataReceived.packet_type == SYNC)
        printf("got sync\n");
    else if (isTimeout())
        printf("timeout!\n");


    if (bytesReceived == SOCKET_ERROR)
    {
        printf("socket error: %i\n", bytesReceived);
        printError();
        exit(1);
    }

    count = 0;

    if (dataReceived.packet_type != SYNC)
    {
        printf("sync failed got: %i\n", dataReceived.packet_type);
        exit(1);
    }
    else if (dataReceived.packet_type == SYNC)
        printf("got sync, start the communication\n");


    struct PacketStruct window[WINDOW_SIZE];
    bool acked[WINDOW_SIZE] = { false };
    bool in_use[WINDOW_SIZE] = { false };
    DWORD send_time[WINDOW_SIZE] = { 0 };

    u32 base_pos = 0;
    u32 next_pos = 0;

    while (base_pos < fileSize)
    {
        while (next_pos < base_pos + (WINDOW_SIZE * BUFFERS_LEN) && next_pos < fileSize)
        {
            int slot = (next_pos / BUFFERS_LEN) % WINDOW_SIZE;

            if (!in_use[slot])
            {
                fseek(fp, next_pos, SEEK_SET);
                sendingPacketLength = fread(window[slot].payload, 1, BUFFERS_LEN, fp);

                window[slot].packet_type = DATA;
                window[slot].pos = next_pos;
                window[slot].packet_len = sendingPacketLength;
                window[slot].crc32 = CRC_CalculateCRC32(window[slot].payload, sendingPacketLength);

                sendto(socketS, (char*)&window[slot], sizeof(struct PacketStruct), 0, (sockaddr*)&addrDst, sizeof(addrDst));
                printf("sending...: %lu\n", next_pos);

                send_time[slot] = GetTickCount();
                in_use[slot] = true;
                acked[slot] = false;

                next_pos += sendingPacketLength;
            }
            else
            {
                break;
            }
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(socketS, &readfds);
        struct timeval tv = { 0, 10000 };

        int ready = select(0, &readfds, NULL, NULL, &tv);
        if (ready > 0)
        {
            memset(&dataReceived, 0, sizeof(dataReceived));
            recvfrom(socketS, (char*)&dataReceived, sizeof(dataReceived), 0, (sockaddr*)&local, &localLen);

            int slot = (dataReceived.pos / BUFFERS_LEN) % WINDOW_SIZE;

            switch (dataReceived.packet_type)
            {
            case ACK:
            {
                if (dataReceived.pos >= base_pos && dataReceived.pos < base_pos + (WINDOW_SIZE * BUFFERS_LEN))
                {
                    acked[slot] = true;

                    if (dataReceived.pos == base_pos)
                    {
                        while (acked[(base_pos / BUFFERS_LEN) % WINDOW_SIZE])
                        {
                            int s = (base_pos / BUFFERS_LEN) % WINDOW_SIZE;
                            acked[s] = false;
                            in_use[s] = false;
                            base_pos += window[s].packet_len;
                            if (base_pos >= fileSize) break;
                        }
                    }
                }
                break;
            }
            case NAK:
            {
                printf("got nak\n");
                if (in_use[slot] && !acked[slot])
                {
                    sendto(socketS, (char*)&window[slot], sizeof(struct PacketStruct), 0, (sockaddr*)&addrDst, sizeof(addrDst));
                    send_time[slot] = GetTickCount();
                }
                break;
            }
            }
        }

        // if not getting the reply, then send the same packet//
        DWORD current_time = GetTickCount();
        for (u32 check_pos = base_pos; check_pos < next_pos; check_pos += BUFFERS_LEN)
        {
            int slot = (check_pos / BUFFERS_LEN) % WINDOW_SIZE;
            if (in_use[slot] && !acked[slot])
            {
                if (current_time - send_time[slot] > 1000)
                {
                    count++;
                    printf("%i: timeout, sending the same data\n", count);
                    sendto(socketS, (char*)&window[slot], sizeof(struct PacketStruct), 0, (sockaddr*)&addrDst, sizeof(addrDst));
                    send_time[slot] = GetTickCount();
                }
            }
        }
    }

    printf("finish Sending packet.\n");
    u32 TotalLen = fileSize;

    char* resInput = (char*)calloc(TotalLen + 1, 1);

    memset(&dataToSend, 0, sizeof(dataToSend));
    rewind(fp);
    TotalLen = fread(resInput, sizeof(u8), TotalLen, fp);
    printf("size is: %lu\n", TotalLen);

    MD5Context ctx;
    md5Init(&ctx);
    md5Update(&ctx, (uint8_t*)resInput, TotalLen);
    md5Finalize(&ctx);
    memcpy(dataToSend.md5, ctx.digest, 16);

    print_hash(dataToSend.md5);
    dataToSend.packet_type = STOP;
    sendto(socketS, (char*)&dataToSend, sizeof(dataToSend), 0, (sockaddr*)&addrDst, sizeof(addrDst));

    closesocket(socketS);
    fclose(fp);
    free(resInput);

    getchar(); //wait for press Enter

    return 0;
}
