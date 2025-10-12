#include <stdlib.h>
#include <string.h>

#define POOR_IMPLEMENTATION
#include <nutpunch.h>
#include <poormans.h>

static const char *const magicKey = "NUTPUNCH", *const lobbyName = "Ligma";
static const uint8_t magicValue = 66;

struct Player {
	int32_t x, y;
};

static const char* randomNames[] = {"Fimon", "Trollga", "Marsoyob", "Ficus", "Caccus", "Skibidi69er", "Caulksucker"};
static const int nameCount = sizeof(randomNames) / sizeof(*randomNames);

static struct Player players[NUTPUNCH_MAX_PLAYERS] = {0};
#define PAYLOAD_SIZE ((size_t)(2))

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

	srand(time(NULL));
	for (poor_init(); poor_running(); poor_tick()) {
		poor_title("nutpunch test");
		if (poor_key_pressed(POOR_ESC) || poor_key_pressed(POOR_Q))
			poor_exit();

		int status = NutPunch_Update(), statusX = 0;
		poor_at(statusX++, 0)->chr = 'F';
		poor_at(statusX++, 0)->chr = '0' + (NPS_Online == status);
		poor_at(statusX++, 0)->chr = '0' + NutPunch_IsMaster();

		static uint8_t data[512] = {0};
		while (NutPunch_HasMessage()) {
			int size = sizeof(data), peer = NutPunch_NextMessage(data, &size);
			if (peer == NUTPUNCH_MAX_PLAYERS)
				continue;
			if (size == PAYLOAD_SIZE) {
				players[peer].x = ((int32_t)(data[0]));
				players[peer].y = ((int32_t)(data[1]));
			} else
				printf("%s\n", data);
		}

		if (waitingForPlayers && NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS) {
			int size = 0, *ptr = NutPunch_LobbyGet("PLAYERS", &size);
			if (sizeof(int) == size && *ptr && NutPunch_PeerCount() >= *ptr) {
				memset(players, 0, sizeof(players));
				players[NutPunch_LocalPeer()].x = poor_width() / 2;
				players[NutPunch_LocalPeer()].y = poor_height() / 2;
				waitingForPlayers = 0;
			}
		}

		if (NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS) {
			players[NutPunch_LocalPeer()].x += poor_key_down(POOR_D) - poor_key_down(POOR_A);
			players[NutPunch_LocalPeer()].y += poor_key_down(POOR_S) - poor_key_down(POOR_W);
		}

		data[0] = (uint8_t)(players[NutPunch_LocalPeer()].x);
		data[1] = (uint8_t)(players[NutPunch_LocalPeer()].y);
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
			if (poor_key_pressed(POOR_T))
				NutPunch_SendReliably(i, buf, sizeof(buf));
		}

		if (NPS_Error == status) {
			NP_Log("ERROR: %s", NutPunch_GetLastError());
			continue;
		} else if (NPS_Online == status) {
			for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
				const int x = players[i].x, y = players[i].y;
				if (NutPunch_LocalPeer() == i) {
					poor_at(x, y)->chr = ' ';
					poor_at(x, y)->bg = POOR_GREEN;
				} else if (NutPunch_PeerAlive(i)) {
					poor_at(x, y)->chr = ' ';
					poor_at(x, y)->bg = POOR_RED;
				}
			}
			continue;
		}

		waitingForPlayers = _waitingForPlayers;
		if (poor_key_pressed(POOR_J))
			NutPunch_Join(lobbyName);
		else if (poor_key_pressed(POOR_H))
			NutPunch_Host(lobbyName);
		else
			goto skip_network;

		NutPunch_LobbySet(magicKey, sizeof(magicValue), &magicValue);
		NutPunch_LobbySet("PLAYERS", sizeof(waitingForPlayers), &waitingForPlayers);
		const char* name = randomNames[rand() % nameCount];
		NutPunch_PeerSet("NAME", (int)strlen(name) + 1, name);

	skip_network:
		if (poor_key_pressed(POOR_K))
			NutPunch_Reset();
	}

cleanup:
	NutPunch_Cleanup();
	return EXIT_SUCCESS;
}
