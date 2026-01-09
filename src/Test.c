#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// gotta use a logfile since we're drawing to the console
static FILE* logfile = NULL;

// clang-format off
#define NutPunch_Log(msg, ...) do { fprintf(logfile, msg "\n", ##__VA_ARGS__), fflush(logfile); } while (0);
// clang-format on

#define NUTPUNCH_IMPLEMENTATION
// #define NUTPUNCH_TRACING
#include <NutPunch.h>

#define POOR_IMPLEMENTATION
#include <poormans.h>

static const char *const magicKey = "NUTPUNCH", *const lobbyName = "Ligma";
static const uint8_t magicValue = 66;

struct Player {
	int32_t x, y;
};

static const char* randomNames[] = {"Fimon", "Trollga", "Marsoyob", "Ficus",
	"Caccus", "Skibidi69er", "Caulksucker"};
static const int nameCount = sizeof(randomNames) / sizeof(*randomNames);

static struct Player players[NUTPUNCH_MAX_PLAYERS] = {0};
#define PAYLOAD_SIZE ((size_t)(2))

static uint8_t targetPlayerCount = 0;
static bool done_waiting = false;

static void maybe_start_netgame() {
	if (done_waiting || NutPunch_LocalPeer() == NUTPUNCH_MAX_PLAYERS)
		return;
	if (NutPunch_PeerCount() < NutPunch_GetMaxPlayers())
		return;
	NutPunch_Memset(players, 0, sizeof(players));
	players[NutPunch_LocalPeer()].x = poor_width() / 2;
	players[NutPunch_LocalPeer()].y = poor_height() / 2;
	done_waiting = true;
}

static void maybe_join_netgame() {
	if (poor_key_pressed(POOR_J))
		NutPunch_Join(lobbyName);
	else if (poor_key_pressed(POOR_H))
		NutPunch_Host(lobbyName, targetPlayerCount);
	else
		return;

	done_waiting = false;
	NutPunch_LobbySet(magicKey, sizeof(magicValue), &magicValue);

	const char* name = randomNames[rand() % nameCount];
	NutPunch_PeerSet("NAME", (int)strlen(name) + 1, name);
}

static void draw_players() {
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
}

static void receive_shit() {
	static uint8_t data[512] = {0};
	while (NutPunch_HasMessage()) {
		int size = sizeof(data),
		    peer = NutPunch_NextMessage(data, &size);
		if (peer == NUTPUNCH_MAX_PLAYERS)
			continue;
		if (size == PAYLOAD_SIZE) {
			players[peer].x = ((int32_t)(data[0]));
			players[peer].y = ((int32_t)(data[1]));
		} else {
			printf("%s\n", data);
		}
	}
}

static void chat_with(int idx) {
	char name1[32] = {0}, name2[32] = {0};

	const char* theirName = NutPunch_PeerGet(idx, "NAME", NULL);
	if (theirName == NULL)
		return;
	NutPunch_SNPrintF(name1, sizeof(name1), "%s", theirName);

	const char* ourName
		= NutPunch_PeerGet(NutPunch_LocalPeer(), "NAME", NULL);
	if (ourName == NULL)
		return;
	NutPunch_SNPrintF(name2, sizeof(name2), "%s", ourName);

	static char buf[96] = {0};
	NutPunch_SNPrintF(buf, sizeof(buf), "[%s]: Hi, %s!", name2, name1);

	NutPunch_SendReliably(idx, buf, sizeof(buf));
}

static void send_shit() {
	static uint8_t data[512] = {0};
	if (NutPunch_LocalPeer() == NUTPUNCH_MAX_PLAYERS)
		return;

	data[0] = (uint8_t)(players[NutPunch_LocalPeer()].x);
	data[1] = (uint8_t)(players[NutPunch_LocalPeer()].y);

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		NutPunch_Send(i, data, PAYLOAD_SIZE);
		if (poor_key_pressed(POOR_T))
			chat_with(i);
	}
}

static void move_our_dot() {
	if (NutPunch_LocalPeer() == NUTPUNCH_MAX_PLAYERS)
		return;
	players[NutPunch_LocalPeer()].x
		+= poor_key_down(POOR_D) - poor_key_down(POOR_A);
	players[NutPunch_LocalPeer()].y
		+= poor_key_down(POOR_S) - poor_key_down(POOR_W);
}

static void draw_debug_bits(int status) {
	const char sep = ':';
	const int chrs[] = {
		'D',
		'B',
		'G',
		sep,
		NPS_Online == status ? '+' : '-',
		NutPunch_IsMaster() ? 'M' : 'S',
		sep,
		'0' + NutPunch_PeerCount(),
		'/',
		'0' + NutPunch_GetMaxPlayers(),
		sep,
		'1' + NutPunch_LocalPeer(),
	};

	const int x = 0, y = 0;
	for (int i = 0; i < sizeof(chrs) / sizeof(*chrs); i++)
		poor_at(x + i, y)->chr = chrs[i];
}

int main(int argc, char* argv[]) {
	if (argc < 2 || argc > 4) {
		printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
		return EXIT_FAILURE;
	}
	if (argc > 2)
		NutPunch_SetServerAddr(argv[2]);

	targetPlayerCount = strtol(argv[1], NULL, 10);
	if (targetPlayerCount < 2) {
		printf("The fuck do you mean?\n");
		return EXIT_FAILURE;
	}

	static char fname[256] = "";
	NutPunch_SNPrintF(
		fname, sizeof(fname), "log%s.txt", argc > 3 ? argv[3] : "");

	logfile = fopen(fname, "w");
	if (!logfile) {
		printf("WTF???");
		return EXIT_FAILURE;
	}

	for (poor_init(); poor_running(); poor_tick()) {
		poor_title("nutpunch test");
		if (poor_key_pressed(POOR_ESC) || poor_key_pressed(POOR_Q))
			poor_exit();

		int status = NutPunch_Update();
		draw_debug_bits(status);
		maybe_start_netgame();

		receive_shit();
		move_our_dot();
		send_shit();

		if (NPS_Online == status)
			draw_players();
		else if (NPS_Error != status)
			maybe_join_netgame();
		if (poor_key_pressed(POOR_K)) {
			NutPunch_Reset();
			done_waiting = false;
		}
	}

cleanup:
	NutPunch_Cleanup();
	fflush(logfile), fclose(logfile);
	return EXIT_SUCCESS;
}
