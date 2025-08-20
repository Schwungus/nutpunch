#include <stdlib.h>

#define NUTPUNCH_IMPLEMENTATION
#include "nutpunch.h"
#include "raylib.h"

static const char* const lobbyName = "Ligma";

struct Player {
	int32_t x, y;
	Color color;
};
static struct Player players[NUTPUNCH_MAX_PLAYERS] = {0};

#define PAYLOAD_SIZE ((size_t)(2))
#define SCALE (3)

static void updateByAddr(struct sockaddr addr, const uint8_t* data) {
	struct sockaddr_in realAddr = *(struct sockaddr_in*)&addr;
	for (int playerIdx = 0; playerIdx < NutPunch_GetPeerCount(); playerIdx++) {
		const struct NutPunch* peer = &NutPunch_GetPeers()[playerIdx];
		bool sameHost = !memcmp(&realAddr.sin_addr, peer->addr, 4);
		bool samePort = realAddr.sin_port == htons(peer->port);

		if (sameHost && samePort) {
			players[playerIdx].x = ((int32_t)(data[0])) * SCALE;
			players[playerIdx].y = ((int32_t)(data[1])) * SCALE;
			players[playerIdx].color = GREEN;
			break;
		}
	}
}

static void sendReceiveUpdates() {
	// Refer to mental notes for why it's this sized.
	static char rawData[NUTPUNCH_PAYLOAD_SIZE] = {0};
	static uint8_t* data = (uint8_t*)rawData;

	// Accept new connections:
	while (NutPunch_MayAccept()) {
		struct sockaddr_in baseAddr;
		int addrSize = sizeof(baseAddr);

		struct sockaddr* addr = (struct sockaddr*)&baseAddr;
		memset(addr, 0, sizeof(*addr));

		memset(data, 0, PAYLOAD_SIZE);
		int io = recvfrom(NutPunch_LocalSocket, rawData, sizeof(rawData), 0, addr, &addrSize);

		if (io == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
			printf("Failed to receive from socket (%d)\n", WSAGetLastError());
			NutPunch_NukeSocket();
		}
		if (io != PAYLOAD_SIZE)
			continue;

		updateByAddr(*addr, data);
	}

	// Process existing peers:
	for (int i = 0; i < NutPunch_GetPeerCount(); i++) {
		if (!NutPunch_GetPeers()[i].port || NutPunch_LocalPeer() == i)
			continue;

		struct sockaddr_in baseAddr;
		baseAddr.sin_family = AF_INET;
		// NOTE: port is converted to host format by `NutPunch_Query()`.
		baseAddr.sin_port = htons(NutPunch_GetPeers()[i].port);
		memcpy(&baseAddr.sin_addr, NutPunch_GetPeers()[i].addr, 4);

		struct sockaddr* addr = (struct sockaddr*)&baseAddr;
		int addrSize = sizeof(baseAddr);

		memset(data, 0, PAYLOAD_SIZE);
		int io = recvfrom(NutPunch_LocalSocket, rawData, sizeof(rawData), 0, addr, &addrSize);

		if (io == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
			printf("Failed to receive from peer %d (%d)\n", i + 1, WSAGetLastError());
			NutPunch_GetPeers()[i].port = 0; // just nuke them...
		}
		if (io != PAYLOAD_SIZE)
			continue;

		updateByAddr(*addr, data);
	}

	// Send each peer your own position:
	data[0] = (uint8_t)(players[NutPunch_LocalPeer()].x / SCALE);
	data[1] = (uint8_t)(players[NutPunch_LocalPeer()].y / SCALE);

	for (int i = 0; i < NutPunch_GetPeerCount(); i++) {
		if (!NutPunch_GetPeers()[i].port || NutPunch_LocalPeer() == i)
			continue;

		struct sockaddr_in baseAddr;
		baseAddr.sin_family = AF_INET;
		baseAddr.sin_port = htons(NutPunch_GetPeers()[i].port); // see NOTE above
		memcpy(&baseAddr.sin_addr, NutPunch_GetPeers()[i].addr, 4);
		struct sockaddr* addr = (struct sockaddr*)&baseAddr;

		int io = sendto(NutPunch_LocalSocket, rawData, PAYLOAD_SIZE, 0, addr, sizeof(baseAddr));
		if (SOCKET_ERROR == io && WSAGetLastError() != WSAEWOULDBLOCK) {
			printf("Failed to send to peer %d (%d)\n", i + 1, WSAGetLastError());
			NutPunch_GetPeers()[i].port = 0; // just nuke them...
		}
	}
}

static int playerCount = 0;

int main(int argc, char* argv[]) {
	if (argc != 3) {
		printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
		return EXIT_FAILURE;
	}

	playerCount = strtol(argv[1], NULL, 10);
	if (!playerCount) {
		printf("The fuck do you mean?\n");
		return EXIT_FAILURE;
	}

	InitWindow(400, 300, "nutpunch test");
	InitAudioDevice();

	SetExitKey(KEY_Q);
	SetTargetFPS(60);

	const int fs = 20;
	while (!WindowShouldClose()) {
		const int status = NutPunch_Query(), sqr = 30;

		BeginDrawing();
		ClearBackground(RAYWHITE);

		if (NutPunch_GetPeerCount() >= playerCount) {
			if (status == NP_Status_Punched) {
				NutPunch_LocalSocket = NutPunch_Release();

				memset(players, 0, sizeof(players));
				players[NutPunch_LocalPeer()].x = 200 - sqr / 2;
				players[NutPunch_LocalPeer()].y = 150 - sqr / 2;
				players[NutPunch_LocalPeer()].color = RED;
			}

			const int32_t spd = 5;
			if (IsKeyDown(KEY_A))
				players[NutPunch_LocalPeer()].x -= spd;
			if (IsKeyDown(KEY_D))
				players[NutPunch_LocalPeer()].x += spd;
			if (IsKeyDown(KEY_W))
				players[NutPunch_LocalPeer()].y -= spd;
			if (IsKeyDown(KEY_S))
				players[NutPunch_LocalPeer()].y += spd;

			sendReceiveUpdates();
			for (int i = 0; i < NutPunch_GetPeerCount() + 1; i++)
				DrawRectangle(players[i].x, players[i].y, sqr, sqr, players[i].color);
		}

		if (NutPunch_LocalSocket == INVALID_SOCKET) {
			DrawText("DISCONNECTED", 5, 5, fs, RED);
			DrawText("Press J to join", 5, 5 + fs, fs, BLACK);
			DrawText("Press K to reset", 5, 5 + fs + fs, fs, BLACK);

			if (IsKeyPressed(KEY_K))
				NutPunch_Reset();
			else if (IsKeyPressed(KEY_J)) {
				NutPunch_SetServerAddr(argv[2]);
				NutPunch_Join(lobbyName);
			}
		}

		if (NutPunch_GetPeerCount()) {
			static char buf[64] = {0};
			snprintf(buf, sizeof(buf), "port: %d", NutPunch_GetPeers()[NutPunch_LocalPeer()].port);
			DrawText(buf, 200, 5, fs, BLACK);
		}

		EndDrawing();
	}

cleanup:
	CloseAudioDevice();
	CloseWindow();

	NutPunch_Cleanup();
	return EXIT_SUCCESS;
}
