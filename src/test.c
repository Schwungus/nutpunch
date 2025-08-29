#include <stdlib.h>

#include "nutpunch.h"
#include "raylib.h"

#ifdef NUTPUNCH_WINDOSE

#define _AMD64_
#define _INC_WINDOWS

#include <windef.h>

#include <minwinbase.h>
#include <winbase.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#else
#error shit....
#endif

static const char* const lobbyName = "Ligma";

struct Player {
	int32_t x, y;
};
static struct Player players[NUTPUNCH_MAX_PLAYERS] = {0};
static SOCKET sock = INVALID_SOCKET;

#define PAYLOAD_SIZE ((size_t)(2))
#define SCALE (3)

static void killSock() {
	if (sock != INVALID_SOCKET) {
		memset(players, 0, sizeof(players));
		closesocket(sock);
		sock = INVALID_SOCKET;
	}
}

static void updateByAddr(struct sockaddr addr, const uint8_t* data) {
	struct sockaddr_in realAddr = *(struct sockaddr_in*)&addr;
	for (int playerIdx = 0; playerIdx < NUTPUNCH_MAX_PLAYERS; playerIdx++) {
		const struct NutPunch* peer = &NutPunch_Peers()[playerIdx];
		bool sameHost = !memcmp(&realAddr.sin_addr, peer->addr, 4);
		bool samePort = realAddr.sin_port == htons(peer->port);

		if (sameHost && samePort) {
			players[playerIdx].x = ((int32_t)(data[0])) * SCALE;
			players[playerIdx].y = ((int32_t)(data[1])) * SCALE;
			break;
		}
	}
}

static bool hasNext() {
	static struct timeval instantBitchNoodles = {0, 0};
	fd_set s = {1, {sock}};

	int res = select(0, &s, NULL, NULL, &instantBitchNoodles);
	if (res == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
		printf("Failed to poll socket (%d)\n", WSAGetLastError());
	return res > 0;
}

static struct sockaddr peer2addr(int idx) {
	struct sockaddr result = {0};

	// NOTE: port is converted to host format by `NutPunch_Query()`.
	struct sockaddr_in* inet = (struct sockaddr_in*)&result;
	inet->sin_family = AF_INET;
	inet->sin_port = htons(NutPunch_Peers()[idx].port);
	memcpy(&inet->sin_addr, NutPunch_Peers()[idx].addr, 4);

	return result;
}

static void sendReceiveUpdates() {
	// Refer to mental notes for why it's this sized.
	static char rawData[NUTPUNCH_RESPONSE_SIZE] = {0};
	static uint8_t* data = (uint8_t*)rawData;

	// Accept new connections:
	while (hasNext()) {
		struct sockaddr_in baseAddr;
		int addrSize = sizeof(baseAddr);

		struct sockaddr* addr = (struct sockaddr*)&baseAddr;
		memset(addr, 0, sizeof(*addr));

		memset(data, 0, sizeof(rawData));
		int io = recvfrom(sock, rawData, sizeof(rawData), 0, addr, &addrSize);

		if (io == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAEWOULDBLOCK)
				break;
			printf("Failed to receive from socket (%d)\n", WSAGetLastError());
			killSock();
			return;
		}
		if (io != PAYLOAD_SIZE)
			continue;

		updateByAddr(*addr, data);
	}

	// Process existing peers:
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (NutPunch_LocalPeer() == i || !NutPunch_Peers()[i].port)
			continue;

		struct sockaddr addr = peer2addr(i);
		int addrSize = sizeof(addr);

		memset(data, 0, PAYLOAD_SIZE);
		int io = recvfrom(sock, rawData, sizeof(rawData), 0, &addr, &addrSize);

		if (io == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
			printf("Failed to receive from peer %d (%d)\n", i + 1, WSAGetLastError());
			NutPunch_Peers()[i].port = 0; // just nuke them...
		}
		if (io != PAYLOAD_SIZE)
			continue;

		updateByAddr(addr, data);
	}

	// Send each peer your own position:
	data[0] = (uint8_t)(players[NutPunch_LocalPeer()].x / SCALE);
	data[1] = (uint8_t)(players[NutPunch_LocalPeer()].y / SCALE);

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (NutPunch_LocalPeer() == i || !NutPunch_Peers()[i].port)
			continue;

		struct sockaddr addr = peer2addr(i);
		int io = sendto(sock, rawData, PAYLOAD_SIZE, 0, &addr, sizeof(addr));

		if (SOCKET_ERROR == io && WSAGetLastError() != WSAEWOULDBLOCK) {
			printf("Failed to send to peer %d (%d)\n", i + 1, WSAGetLastError());
			NutPunch_Peers()[i].port = 0; // just nuke them...
		}
	}
}

int main(int argc, char* argv[]) {
	if (argc != 3) {
		printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
		return EXIT_FAILURE;
	}

	int expectingPlayers = strtol(argv[1], NULL, 10);
	if (!expectingPlayers) {
		printf("The fuck do you mean?\n");
		return EXIT_FAILURE;
	}

	InitWindow(400, 300, "nutpunch test");
	SetExitKey(KEY_Q);
	SetTargetFPS(60);

	const int fs = 20, sqr = 30;
	while (!WindowShouldClose()) {
		NutPunch_Set("PLAYERS", sizeof(expectingPlayers), &expectingPlayers);
		if (NP_Status_Error == NutPunch_Query())
			killSock();

		BeginDrawing();
		ClearBackground(RAYWHITE);

		if (sock == INVALID_SOCKET) {
			int size = 0, *ptr = NutPunch_Get("PLAYERS", &size);
			if (sizeof(int) == size && *ptr && NutPunch_PeerCount() >= *ptr) {
				memset(players, 0, sizeof(players));
				players[NutPunch_LocalPeer()].x = 200 - sqr / 2;
				players[NutPunch_LocalPeer()].y = 150 - sqr / 2;
				sock = *(SOCKET*)NutPunch_Done();
			}
		}

		if (NutPunch_PeerCount()) {
			const int32_t spd = 5;
			if (IsKeyDown(KEY_A))
				players[NutPunch_LocalPeer()].x -= spd;
			if (IsKeyDown(KEY_D))
				players[NutPunch_LocalPeer()].x += spd;
			if (IsKeyDown(KEY_W))
				players[NutPunch_LocalPeer()].y -= spd;
			if (IsKeyDown(KEY_S))
				players[NutPunch_LocalPeer()].y += spd;

			if (sock != INVALID_SOCKET) {
				sendReceiveUpdates();
				for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
					if (NutPunch_LocalPeer() == i)
						DrawRectangle(players[i].x, players[i].y, sqr, sqr, RED);
					else if (NutPunch_Peers()[i].port)
						DrawRectangle(players[i].x, players[i].y, sqr, sqr, GREEN);
			}

			DrawText("GAMING!!!", 240, 5, fs, GREEN);
		} else {
			DrawText("DISCONNECTED", 5, 5, fs, RED);
			DrawText("Press J to join", 5, 5 + fs, fs, BLACK);
			DrawText("Press K to reset", 5, 5 + fs + fs, fs, BLACK);

			if (IsKeyPressed(KEY_K)) {
				NutPunch_Reset();
				killSock();
			}
			if (IsKeyPressed(KEY_J)) {
				NutPunch_SetServerAddr(argv[2]);
				NutPunch_Join(lobbyName);
			}
		}

		EndDrawing();
	}

cleanup:
	CloseWindow();
	NutPunch_Cleanup();
	return EXIT_SUCCESS;
}
