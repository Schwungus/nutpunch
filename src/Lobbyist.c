#include <stdio.h>
#include <stdlib.h>

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

static void handle_lobby_list(const void* raw) {
    const NutPunch_LobbyList* list = raw;

    if (!list->count) {
        printf("No lober\n\n");
        return;
    }

    printf("Found %u lobbies:\n", list->count);
    for (int i = 0; i < list->count; i++) {
        const NutPunch_LobbyInfo* lobby = &list->lobbies[i];
        printf("%i. %.*s (%u/%u)\n", i + 1, (int)sizeof(NutPunch_LobbyId), lobby->name,
            lobby->players, lobby->capacity);
    }
    printf("\n");
}

int main(int argc, char* argv[]) {
    if (argc < 1) {
        printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
        return EXIT_FAILURE;
    } else if (argc > 1) {
        NutPunch_SetServerAddr(argv[1]);
    }

    NutPunch_Register(NPCB_LobbyList, handle_lobby_list);
    NutPunch_Query();
    NutPunch_FindLobbies(0, NULL);

    static int refresh = 0;
    while (true) {
        if (NPS_Error == NutPunch_Update()) {
            printf("failed to connect or smth\n");
            return EXIT_FAILURE;
        }

        if (++refresh >= 150) {
            NutPunch_FindLobbies(0, NULL);
            refresh = 0;
        }

        NP_SleepMs(1000 / 30);
    }

    NutPunch_Disconnect();

    return EXIT_SUCCESS;
}
