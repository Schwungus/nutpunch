#ifndef NUTPUNCH_IMPLEMENTATION
#define NUTPUNCH_IMPLEMENTATION
#include <winsock2.h>
#endif

#include "nutpunch.h"

#ifdef NUTPUNCH_WINDOSE
#define sleepMs(ms) (Sleep((ms)))
#else
#error Bad luck.
#endif

static uint16_t punchedPort = 0;

void tickTestServer() {
	if (NutPunch_LocalSock == INVALID_SOCKET) {
		if (!NutPunch_BindSocket(punchedPort))
			goto fail;
	}

	printf("\n%d peers", NutPunch_GetPeerCount());
	static uint8_t data[NUTPUNCH_PAYLOAD_SIZE + 1] = {0};

	// Receive from all...
	while (NutPunch_MayAccept()) {
		struct sockaddr_in baseAddr;
		struct sockaddr* addr = (struct sockaddr*)&baseAddr;

		memset(data, 0, sizeof(data));
		int fugg = sizeof(baseAddr),
		    io = recvfrom(NutPunch_LocalSock, (char*)data, sizeof(data) - 1, 0, addr, &fugg);

		if (SOCKET_ERROR == io) {
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				continue;
			printf("\nFailed to receive from socket (%d)", WSAGetLastError());
			goto fail;
		}
		if (!io)
			continue;

		for (int i = 1; i < NutPunch_GetPeerCount(); i++) {
			bool sameHost = !memcmp(&baseAddr.sin_addr, NutPunch_GetPeers()[i].addr, 4);
			bool samePort = baseAddr.sin_port == NutPunch_GetPeers()[i].port;
			if (sameHost && samePort) {
				printf("\nrecv(%d): [%d] %s", i + 1, io, data);
				break;
			}
		}
	}

	// And echo back:
	for (int i = 1; i < NutPunch_GetPeerCount(); i++) {
		if (rand() % 20)
			continue;
		memset(data, 0, sizeof(data));

		struct sockaddr_in baseAddr;
		baseAddr.sin_family = AF_INET;
		baseAddr.sin_port = NutPunch_GetPeers()[i].port;
		memcpy(&baseAddr.sin_addr, &NutPunch_GetPeers()[i].addr, 4);
		struct sockaddr* addr = (struct sockaddr*)&baseAddr;

		memcpy(data, "Hello!", sizeof(data) - 1);

		printf("\nsend(%d) attempt: %s", i + 1, data);
		int64_t io = sendto(NutPunch_LocalSock, (char*)data, sizeof(data) - 1, 0, addr, sizeof(baseAddr));
		if (SOCKET_ERROR == io) {
			printf("\nFailed to send echo (%d)", WSAGetLastError());
			goto fail;
		}
	}

	return;

fail:
	NutPunch_LocalSock = INVALID_SOCKET;
	punchedPort = 0;
}

void tickNutpunch() {
	int query = NutPunch_Query();
	if (query == NP_Status_Error)
		printf("\nnutpunch failed......");
	if (query == NP_Status_Idle || query == NP_Status_Error) {
		punchedPort = 0;
		printf("\nreconnecting...");
		NutPunch_Join("aabb");
	}

	printf("\ncount = %d; our port = ", NutPunch_GetPeerCount());
	if (query == NP_Status_Punched && NutPunch_GetPeerCount() >= 2) {
		punchedPort = NutPunch_Release();
		closesocket(NutPunch_LocalSock);
		NutPunch_LocalSock = INVALID_SOCKET;
		printf("%d", punchedPort);
	} else
		printf("none yet");
}

int main(int argc, char* argv[]) {
	if (argc < 2) {
		printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
		exit(EXIT_FAILURE);
	}

	NutPunch_SetServerAddr(argv[1]);
	for (;;) {
		if (punchedPort) {
			tickTestServer();
			sleepMs(500);
		} else {
			tickNutpunch();
			sleepMs(100);
		}
	}

	NutPunch_Cleanup();
	return EXIT_SUCCESS;
}
