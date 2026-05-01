#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

static const char *LOBBY = "Zalooping", *WOWZA = "FUCK YOU";

enum {
    CHAN_MAIN,
    CHAN_COUNT,
};

int main(int argc, char* argv[]) {
    NutPunch_SetGameId("Zalooper");
    NutPunch_SetChannelCount(CHAN_COUNT);

    if (argc > 1)
        NutPunch_SetServerAddr(argv[1]);

    if (argc > 2) {
        int players = strtol(argv[2], NULL, 10);
        NutPunch_Host(LOBBY);
        NutPunch_SetMaxPlayers(players);
        NP_Info("HOSTANING>..... for %d", players);
    } else {
        NutPunch_Join(LOBBY);
        NP_Info("JOINGING>.....");
    }

    for (;;) {
        if (NutPunch_Update() == NPS_Error)
            goto fuck;
        if (NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS) {
            if (NutPunch_PeerCount() >= NutPunch_GetMaxPlayers())
                break;
        }
        NP_SleepMs(100);
    }

    NP_Info("SENDING SHIT OUT");
    for (int peer = 0; peer < NUTPUNCH_MAX_PLAYERS; peer++)
        if (peer != NutPunch_LocalPeer() && NutPunch_PeerAlive(peer))
            NutPunch_SendReliably(CHAN_MAIN, peer, WOWZA, (int)strlen(WOWZA));

    for (int i = 0; i < 30; i++) {
        if (NutPunch_Update() == NPS_Error)
            goto fuck;
        while (NutPunch_HasMessage(CHAN_MAIN)) {
            static char data[32] = "";
            memset(data, 0, sizeof(data));

            int size = sizeof(data);
            const int sender = NutPunch_NextMessage(CHAN_MAIN, data, &size);
            NP_Info("From %02d: %s", sender, data);
        }
        NP_SleepMs(100);
    }

    NutPunch_Shutdown();
    NP_Info("DONE BYE");
    return EXIT_SUCCESS;

fuck:
    NP_Warn("UPDATE WENT WRONG: %s", NutPunch_GetLastError());
    return EXIT_FAILURE;
}
