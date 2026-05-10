#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// gotta use a logfile since we're drawing to the console
static FILE* logfile = NULL;

#define NutPunch_Log(msg, ...)                                                                     \
    do {                                                                                           \
        fprintf(logfile, msg "\n", ##__VA_ARGS__);                                                 \
        fflush(logfile);                                                                           \
    } while (0)

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

#define POOR_IMPLEMENTATION
#include <poormans.h>

static struct {
    int32_t x, y;
} players[NUTPUNCH_MAX_PLAYERS] = {0};

enum {
    CHAN_GAME,
    CHAN_CHAT,
    CHAN_COUNT,
};

static const char *magicKey = "NUTPUNCH", *magicValue = "TEST", *lobbyName = "Ligma";

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

    if (poor_key_pressed(POOR_M)) {
        NutPunch_EnterQueue();
    } else if (poor_key_pressed(POOR_J)) {
        NutPunch_Join(lobbyName);
    } else if (poor_key_pressed(POOR_H)) {
        NutPunch_Host(lobbyName);
        NutPunch_SetMaxPlayers(targetPlayerCount);
    } else {
        return;
    }

    NutPunch_SetLobbyData(magicKey, magicValue);

    const char* name = randomNames[rand() % nameCount];
    NutPunch_SetPeerData("NAME", name);

    NP_Info("We are %s", name);
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
        NP_Info("[%s]: %s", NutPunch_GetPeerData(peer, "NAME"), data);
    }
}

static void chat_with(int idx) {
    const char* theirName = NutPunch_GetPeerData(idx, "NAME");

    if (!theirName)
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
    players[NutPunch_LocalPeer()].x += poor_key_down(POOR_KP_6) - poor_key_down(POOR_KP_4);

    players[NutPunch_LocalPeer()].y += poor_key_down(POOR_S) - poor_key_down(POOR_W);
    players[NutPunch_LocalPeer()].y += poor_key_down(POOR_KP_2) - poor_key_down(POOR_KP_8);
}

static void draw_debug_bits(int status) {
    poor_printf(0, 0, "DBG:%c%c:%d/%d:%d", NPS_Online == status ? '+' : '-',
        NutPunch_MasterPeer() == NutPunch_LocalPeer() ? 'M' : 'S', NutPunch_PeerCount(),
        NutPunch_GetMaxPlayers(), 1 + NutPunch_LocalPeer());

    const int x = poor_width() - 6;

    if (NutPunch_IsOnline())
        poor_printf(x, 0, "S=%4d", NutPunch_ServerPing());

    for (int peer = 0; peer < NUTPUNCH_MAX_PLAYERS; peer++)
        if (NutPunch_PeerAlive(peer) && peer != NutPunch_LocalPeer())
            poor_printf(x, 1 + peer, "%1d=%4d", peer + 1, NutPunch_PeerPing(peer));
}

static void greet(const void* raw) {
    NutPunch_Peer peer = *(NutPunch_Peer*)raw;
    NP_Info("Welcome, %s!", NutPunch_GetPeerData(peer, "NAME"));
}

static void bye(const void* raw) {
    NutPunch_Peer peer = *(NutPunch_Peer*)raw;
    NP_Info("Farewell, %s!", NutPunch_GetPeerData(peer, "NAME"));
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 4) {
        printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
        return EXIT_FAILURE;
    }

    NutPunch_SetGameId("NutPunch Test");
    NutPunch_SetChannelCount(CHAN_COUNT);

    NutPunch_Register(NPCB_PeerJoined, greet);
    NutPunch_Register(NPCB_PeerLeft, bye);

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

    NutPunch_Shutdown();
    fflush(logfile), fclose(logfile);
    return EXIT_SUCCESS;
}
