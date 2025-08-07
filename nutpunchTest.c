#include <stdlib.h>
#include <time.h>

#define NUTPUNCH_IMPLEMENTATION
#include "nutpunch.h"

#ifdef NUTPUNCH_WINDOSE
#include <windows.h>
#include <winsock2.h>
#define sleepMs(ms) (Sleep((ms)))
#else
#error Bad luck.
#endif

static uint16_t punchedPort = 0;

void tickTestServer() {
	// CRAZY HAXXXX!!!

	if (NutPunch_LocalSock == INVALID_SOCKET) {
		if (!NutPunch_CreateSocket()) {
			printf("\nFailed to create a socket.......");
			goto fail;
		}
		struct sockaddr addr = NutPunch_SockAddr("127.0.0.1", punchedPort);
		if (0 > bind(NutPunch_LocalSock, (struct sockaddr*)&addr, sizeof(addr))) {
			printf("\nFailed to bind to punched port.......");
			goto fail;
		}
	}

	printf("\n%d peers", NutPunch_GetPeerCount());

	// Receive from all and echo back.
	static char data[33] = {0};
	for (int i = 1; i < NutPunch_GetPeerCount(); i++) {
		memset(data, 0, sizeof(data));

		struct sockaddr_in addr = {0};
		addr.sin_family = AF_INET;
		memcpy(&addr.sin_addr, NutPunch_GetPeers()[i].addr, 4);
		addr.sin_port = NutPunch_GetPeers()[i].port;
		int addrSize = sizeof(addr);

		if (!(rand() % 20)) {
			strncpy(data, "Hello!", sizeof(data) - 1);
			goto send;
		}

		int status = NutPunch_SockStatus();
		if (!status)
			continue;
		if (status < 0) {
			printf("\nFailed to query socket status (%d)", WSAGetLastError());
			goto fail;
		}
		if (0 > recvfrom(NutPunch_LocalSock, data, sizeof(data), 0, (struct sockaddr*)&addr, &addrSize)) {
			printf("\nFailed to receive from socket (%d)", WSAGetLastError());
			goto fail;
		}

		printf("\nrecv(%d): %s", i, data);
		continue;

	send:
		printf("\nsend(%d) attempt: %s", i, data);
		int result = sendto(NutPunch_LocalSock, data, sizeof(data), 0, (struct sockaddr*)&addr, sizeof(addr));
		if (result < 0) {
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

	srand(time(NULL));
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
