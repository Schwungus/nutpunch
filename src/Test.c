#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

static struct {
	int32_t x, y;
} players[NUTPUNCH_MAX_PLAYERS] = {0};

enum {
	CHAN_GAME,
	CHAN_CHAT,
};

static const char *const magicKey = "NUTPUNCH", *const lobbyName = "Ligma";
static const uint8_t magicValue = 66;

static const char* randomNames[]
	= {"Fimon", "Trollga", "Marsoyob", "Ficus", "Caccus", "Skibidi69er", "Caulksucker"};
static const int nameCount = sizeof(randomNames) / sizeof(*randomNames);

static uint8_t targetPlayerCount = 0;

static void reset_gamestate() {
	NutPunch_Memset(players, 0, sizeof(players));
	players[NutPunch_LocalPeer()].x = poor_width() / 2;
	players[NutPunch_LocalPeer()].y = poor_height() / 2;
}

static void maybe_join_netgame() {
	if (NutPunch_IsOnline())
		return;

	if (poor_key_pressed(POOR_J))
		NutPunch_Join(lobbyName);
	else if (poor_key_pressed(POOR_H))
		NutPunch_Host(lobbyName, targetPlayerCount);
	else
		return;

	NutPunch_LobbySet(magicKey, sizeof(magicValue), &magicValue);

	srand(time(NULL));
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

	while (NutPunch_HasMessage(CHAN_GAME)) {
		int size = sizeof(data);
		const int peer = NutPunch_NextMessage(CHAN_GAME, data, &size);
		players[peer].x = ((int32_t)(data[0]));
		players[peer].y = ((int32_t)(data[1]));
	}

	while (NutPunch_HasMessage(CHAN_CHAT)) {
		int size = sizeof(data);
		const int peer = NutPunch_NextMessage(CHAN_CHAT, data, &size);
		NutPunch_Log("[%s]: %s", (char*)NutPunch_PeerGet(peer, "NAME", &size), data);
	}
}

static void chat_with(int idx) {
	const char* theirName = NutPunch_PeerGet(idx, "NAME", NULL);
	if (theirName == NULL)
		return;
	static char buf[96] = {0};
	const int size = NutPunch_SNPrintF(buf, sizeof(buf), "Hi, %s!", theirName);
	NutPunch_SendReliably(CHAN_CHAT, idx, buf, size + 1);
}

static void send_shit() {
	static uint8_t data[2] = {0};
	if (NutPunch_LocalPeer() == NUTPUNCH_MAX_PLAYERS)
		return;

	data[0] = (uint8_t)(players[NutPunch_LocalPeer()].x);
	data[1] = (uint8_t)(players[NutPunch_LocalPeer()].y);

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		NutPunch_Send(CHAN_GAME, i, data, sizeof(data));
		if (poor_key_pressed(POOR_T))
			chat_with(i);
	}
}

static void move_our_dot() {
	if (NutPunch_LocalPeer() == NUTPUNCH_MAX_PLAYERS)
		return;
	players[NutPunch_LocalPeer()].x += poor_key_down(POOR_D) - poor_key_down(POOR_A);
	players[NutPunch_LocalPeer()].y += poor_key_down(POOR_S) - poor_key_down(POOR_W);
}

static void draw_debug_bits(int status) {
	poor_printf(0, 0, "DBG:%c%c:%d/%d:%d", NPS_Online == status ? '+' : '-',
		NutPunch_IsMaster() ? 'M' : 'S', NutPunch_PeerCount(), NutPunch_GetMaxPlayers(),
		1 + NutPunch_LocalPeer());
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
	NutPunch_SNPrintF(fname, sizeof(fname), "log%s.txt", argc > 3 ? argv[3] : "");

	logfile = fopen(fname, "w");
	if (!logfile) {
		printf("WTF???");
		return EXIT_FAILURE;
	}

	for (poor_init(); poor_running(); poor_tick()) {
		poor_title("nutpunch test");
		if (poor_key_pressed(POOR_ESC) || poor_key_pressed(POOR_Q))
			poor_exit();

		const bool was_ready = NutPunch_IsReady();
		const int status = NutPunch_Update();
		draw_debug_bits(status);

		receive_shit();
		move_our_dot();
		send_shit();

		if (NutPunch_IsReady() && !was_ready)
			reset_gamestate();
		if (NutPunch_IsReady())
			draw_players();

		if (poor_key_pressed(POOR_K)) {
			NutPunch_Disconnect();
			NutPunch_Reset();
		} else {
			maybe_join_netgame();
		}
	}

	NutPunch_Cleanup();
	fflush(logfile), fclose(logfile);
	return EXIT_SUCCESS;
}
