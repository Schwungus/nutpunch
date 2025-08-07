#include <stdlib.h>

#ifndef NUTPUNCH_IMPLEMENTATION
#define NUTPUNCH_IMPLEMENTATION
#endif

#include "nutpunch.h"

#ifdef NUTPUNCH_WINDOSE
#define sleepMs(ms) (Sleep((ms)))
#else
#error Bad luck.
#endif

static uint16_t punchedPort = 0;

void tickTestServer() {
	// CRAZY HAXXXX!!!

	if (NutPunch_LocalSock == TCS_NULLSOCKET) {
		if (!NutPunch_BindSocket(punchedPort))
			goto fail;
	}

	printf("\n%d peers", NutPunch_GetPeerCount());

	// Receive from all and echo back.
	static uint8_t data[33] = {0};
	for (int i = 1; i < NutPunch_GetPeerCount(); i++) {
		memset(data, 0, sizeof(data));

		struct TcsAddress addr = {0};
		const uint8_t* cp = NutPunch_GetPeers()[i].addr;
		addr.family = TCS_AF_IP4;
		addr.data.af_inet.port = NutPunch_GetPeers()[i].port;
		tcs_util_ipv4_args(cp[0], cp[1], cp[2], cp[3], &addr.data.af_inet.address);

		if (!(rand() % 20)) {
			memcpy(data, "Hello!", sizeof(data) - 1);
			goto send;
		}

		if (!NutPunch_GotData())
			continue;

		size_t io = 0;
		if (TCS_SUCCESS != tcs_receive_from(NutPunch_LocalSock, data, sizeof(data), 0, &addr, &io)) {
			printf("\nFailed to receive from socket (%d)", WSAGetLastError());
			goto fail;
		}
		if (!io)
			continue;

		printf("\nrecv(%d): %s", i, data);
		continue;

	send:
		printf("\nsend(%d) attempt: %s", i, data);
		if (TCS_SUCCESS != tcs_send_to(NutPunch_LocalSock, data, sizeof(data), 0, &addr, &io)) {
			printf("\nFailed to send echo (%d)", WSAGetLastError());
			goto fail;
		}
	}

	return;

fail:
	NutPunch_LocalSock = TCS_NULLSOCKET;
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
		NutPunch_LocalSock = TCS_NULLSOCKET;
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
