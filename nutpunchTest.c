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
	if (NutPunch_LocalSocket == INVALID_SOCKET) {
		if (!NutPunch_BindSocket(punchedPort))
			goto fail;
	}

	printf("%d peers\n", NutPunch_GetPeerCount());
	static uint8_t data[NUTPUNCH_PAYLOAD_SIZE + 1] = {0};

	// Receive from all...
	while (NutPunch_MayAccept()) {
		struct sockaddr_in baseAddr;
		struct sockaddr* addr = (struct sockaddr*)&baseAddr;

		memset(data, 0, sizeof(data));
		int fugg = sizeof(baseAddr),
		    io = recvfrom(NutPunch_LocalSocket, (char*)data, sizeof(data) - 1, 0, addr, &fugg);

		if (SOCKET_ERROR == io) {
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				continue;
			printf("Failed to receive from socket (%d)\n", WSAGetLastError());
			goto fail;
		}
		if (!io)
			continue;

		for (int i = 1; i < NutPunch_GetPeerCount(); i++) {
			bool sameHost = !memcmp(&baseAddr.sin_addr, NutPunch_GetPeers()[i].addr, 4);
			bool samePort = baseAddr.sin_port == NutPunch_GetPeers()[i].port;
			if (sameHost && samePort) {
				printf("recv(%d): [%d] %s\n", i + 1, io, data);
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
		printf("send(%d) attempt: %s\n", i + 1, data);

		int64_t io = sendto(NutPunch_LocalSocket, (char*)data, sizeof(data) - 1, 0, addr, sizeof(baseAddr));
		if (SOCKET_ERROR == io) {
			printf("Failed to send echo (%d)\n", WSAGetLastError());
			goto fail;
		}
	}

	return;

fail:
	NutPunch_LocalSocket = INVALID_SOCKET;
	punchedPort = 0;
}

void tickNutpunch() {
	int status = NutPunch_Query();
	if (status == NP_Status_Error)
		printf("nutpunch failed......\n");
	if (status == NP_Status_Idle || status == NP_Status_Error) {
		punchedPort = 0;
		printf("reconnecting...\n");
		NutPunch_Join("LIGMA");
	}

	printf("count = %d; our port = ", NutPunch_GetPeerCount());
	if (status == NP_Status_Punched && NutPunch_GetPeerCount() >= 1)
		printf("%d\n", NutPunch_GetPeers()[0].port);
	else
		printf("none yet\n");
	if (status == NP_Status_Punched && NutPunch_GetPeerCount() >= 2)
		punchedPort = NutPunch_Release();
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
			sleepMs(200);
		} else {
			tickNutpunch();
			sleepMs(1000 / 60);
		}
	}

	NutPunch_Cleanup();
	return EXIT_SUCCESS;
}
