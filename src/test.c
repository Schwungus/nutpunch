#include <stdlib.h>
#include <string.h>

#include <nutpunch.h>
#include <raylib.h>

#ifdef NUTPUNCH_WINDOSE
#define _AMD64_
#define _INC_WINDOWS
#include <windef.h>
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static const char* const lobbyName = "Ligma";

struct Player {
	int32_t x, y;
	struct sockaddr addr;
};

static const char* randomNames[] = {"Fimon", "Trollga", "Marsoyob", "Ficus", "Caccus", "Skibidi69er", "Caulksucker"};
static const int nameCount = sizeof(randomNames) / sizeof(*randomNames);

static struct Player players[NUTPUNCH_MAX_PLAYERS] = {0};

#define PAYLOAD_SIZE ((size_t)(2))
#define SCALE (3)

int main(int argc, char* argv[]) {
	if (argc != 2 && argc != 3) {
		printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
		return EXIT_FAILURE;
	}
	if (argc == 3)
		NutPunch_SetServerAddr(argv[2]);

	int waitingForPlayers = strtol(argv[1], NULL, 10), _waitingForPlayers = waitingForPlayers;
	if (waitingForPlayers < 2) {
		printf("The fuck do you mean?\n");
		return EXIT_FAILURE;
	}

	InitWindow(400, 300, "nutpunch test");
	SetExitKey(KEY_Q);
	SetTargetFPS(60);

	const int fs = 20, sqr = 30;
	while (!WindowShouldClose()) {
		int status = NutPunch_Update();

		static uint8_t data[512] = {0};
		while (NutPunch_HasMessage()) {
			int size = sizeof(data), peer = NutPunch_NextMessage(data, &size);
			if (peer >= NUTPUNCH_MAX_PLAYERS)
				continue;
			if (size == PAYLOAD_SIZE) {
				players[peer].x = ((int32_t)(data[0])) * SCALE;
				players[peer].y = ((int32_t)(data[1])) * SCALE;
			} else
				printf("%s\n", data);
		}

		BeginDrawing();
		ClearBackground(RAYWHITE);

		if (waitingForPlayers && NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS) {
			int size = 0, *ptr = NutPunch_LobbyGet("PLAYERS", &size);
			if (sizeof(int) == size && *ptr && NutPunch_PeerCount() >= *ptr) {
				memset(players, 0, sizeof(players));
				players[NutPunch_LocalPeer()].x = 200 - sqr / 2;
				players[NutPunch_LocalPeer()].y = 150 - sqr / 2;
				waitingForPlayers = 0;
			}
		}

		if (NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS) {
			const int32_t spd = 5;
			if (IsKeyDown(KEY_A))
				players[NutPunch_LocalPeer()].x -= spd;
			if (IsKeyDown(KEY_D))
				players[NutPunch_LocalPeer()].x += spd;
			if (IsKeyDown(KEY_W))
				players[NutPunch_LocalPeer()].y -= spd;
			if (IsKeyDown(KEY_S))
				players[NutPunch_LocalPeer()].y += spd;
		}

		data[0] = (uint8_t)(players[NutPunch_LocalPeer()].x / SCALE);
		data[1] = (uint8_t)(players[NutPunch_LocalPeer()].y / SCALE);
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			NutPunch_Send(i, data, PAYLOAD_SIZE);
			char name1[32] = {0}, name2[32] = {0};

			const char* theirName = NutPunch_PeerGet(i, "NAME", NULL);
			if (theirName == NULL)
				continue;
			snprintf(name1, sizeof(name1), "%s", theirName);

			const char* ourName = NutPunch_PeerGet(NutPunch_LocalPeer(), "NAME", NULL);
			if (ourName == NULL)
				continue;
			snprintf(name2, sizeof(name2), "%s", ourName);

			static char buf[96] = {0};
			snprintf(buf, sizeof(buf), "[%s]: Hi, %s!", name2, name1);
			if (IsKeyPressed(KEY_T))
				NutPunch_SendReliably(i, buf, sizeof(buf));
		}

		if (NP_Status_Error == status)
			NP_Log("ERROR: %s", NutPunch_GetLastError());
		else if (NP_Status_Online == status) {
			for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
				if (NutPunch_LocalPeer() == i)
					DrawRectangle(players[i].x, players[i].y, sqr, sqr, RED);
				else if (NutPunch_PeerAlive(i))
					DrawRectangle(players[i].x, players[i].y, sqr, sqr, GREEN);
				else
					continue;
				const char* name = NutPunch_PeerGet(i, "NAME", NULL);
				int fs = 20, width = MeasureText(name, fs);
				DrawText(name, players[i].x + sqr / 2 - width / 2, players[i].y - fs, fs, BLACK);
			}
			DrawText("GAMING!!!", 240, 5, fs, GREEN);
			if (NutPunch_IsMaster())
				DrawText("MASTERFULLY!!!", 240, 5 + fs, fs, GREEN);
		} else {
			waitingForPlayers = _waitingForPlayers;
			DrawText("DISCONNECTED", 5, 5, fs, RED);
			DrawText("Press J to join", 5, 5 + fs, fs, BLACK);
			DrawText("Press H to host", 5, 5 + fs * 2, fs, BLACK);
			DrawText("Press K to reset", 5, 5 + fs * 3, fs, BLACK);

			if (IsKeyPressed(KEY_J))
				NutPunch_Join(lobbyName);
			else if (IsKeyPressed(KEY_H))
				NutPunch_Host(lobbyName);
			else
				goto skip_network;

			NutPunch_LobbySet("PLAYERS", sizeof(waitingForPlayers), &waitingForPlayers);
			const char* name = randomNames[GetRandomValue(1, nameCount) - 1];
			NutPunch_PeerSet("NAME", (int)strlen(name) + 1, name);
		}

	skip_network:
		if (IsKeyPressed(KEY_K))
			NutPunch_Reset();

		EndDrawing();
	}

cleanup:
	CloseWindow();
	NutPunch_Cleanup();
	return EXIT_SUCCESS;
}
