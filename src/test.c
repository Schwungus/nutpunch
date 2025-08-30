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

static struct Player players[NUTPUNCH_MAX_PLAYERS] = {0};

#define PAYLOAD_SIZE ((size_t)(2))
#define SCALE (3)

int main(int argc, char* argv[]) {
	if (argc != 3) {
		printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
		return EXIT_FAILURE;
	}

	int expectingPlayers = strtol(argv[1], NULL, 10), _expectingPlayers = expectingPlayers;
	if (expectingPlayers < 2) {
		printf("The fuck do you mean?\n");
		return EXIT_FAILURE;
	}

	InitWindow(400, 300, "nutpunch test");
	SetExitKey(KEY_Q);
	SetTargetFPS(60);

	const int fs = 20, sqr = 30;
	while (!WindowShouldClose()) {
		if (expectingPlayers)
			NutPunch_Set("PLAYERS", sizeof(expectingPlayers), &expectingPlayers);
		int status = NutPunch_Update();

		static uint8_t data[PAYLOAD_SIZE] = {0};
		while (NutPunch_HasNext()) {
			int size = sizeof(data), peer = NutPunch_NextPacket(data, &size);
			if (peer >= NUTPUNCH_MAX_PLAYERS || size != sizeof(data))
				continue;
			players[peer].x = ((int32_t)(data[0])) * SCALE;
			players[peer].y = ((int32_t)(data[1])) * SCALE;
		}

		BeginDrawing();
		ClearBackground(RAYWHITE);

		if (expectingPlayers && NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS) {
			int size = 0, *ptr = NutPunch_Get("PLAYERS", &size);
			if (sizeof(int) == size && *ptr && NutPunch_PeerCount() >= *ptr) {
				memset(players, 0, sizeof(players));
				players[NutPunch_LocalPeer()].x = 200 - sqr / 2;
				players[NutPunch_LocalPeer()].y = 150 - sqr / 2;
				expectingPlayers = 0;
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
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			NutPunch_Send(i, data, sizeof(data));

		if (NP_Status_Online == status) {
			for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
				if (NutPunch_LocalPeer() == i)
					DrawRectangle(players[i].x, players[i].y, sqr, sqr, RED);
				else if (NutPunch_PeerAlive(i))
					DrawRectangle(players[i].x, players[i].y, sqr, sqr, GREEN);
			DrawText("GAMING!!!", 240, 5, fs, GREEN);
		} else {
			expectingPlayers = _expectingPlayers;
			DrawText("DISCONNECTED", 5, 5, fs, RED);
			DrawText("Press J to join", 5, 5 + fs, fs, BLACK);
			DrawText("Press K to reset", 5, 5 + fs + fs, fs, BLACK);

			if (IsKeyPressed(KEY_K))
				NutPunch_Reset();
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
