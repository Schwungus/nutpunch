#include <stdio.h>
#include <stdlib.h>

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

#ifdef NUTPUNCH_WINDOSE
#include <conio.h>
#endif

static void handle_lobby_list(const void* raw) {
    const NutPunch_LobbyList* list = raw;

    if (!list->count) {
        printf("No lober\n\n");
        return;
    }

    printf("Found %u lobbies:\n", list->count);
    for (int i = 0; i < list->count; i++) {
        const NutPunch_LobbyInfo* lobby = &list->lobbies[i];

        printf("%i. %.*s (%u/%u)\n", i + 1, (int)sizeof(NutPunch_LobbyName), lobby->name,
            lobby->players, lobby->capacity);

        NutPunch_RequestLobbyData(lobby->name);
    }
    printf("\n");
}

static void handle_lobby_data(const void* raw) {
    const NutPunch_LobbyMetadata* info = raw;

    printf("%.*s has ", (int)sizeof(NutPunch_LobbyName), info->name);

    if (!info->metadata) {
        printf("no data\n\n");
        return;
    }

    uint8_t count = 0;
    for (NutPunch_Field* field = info->metadata; field; field = field->next)
        count++;

    printf("%u fields:\n", count);
    for (NutPunch_Field* field = info->metadata; field; field = field->next)
        printf("%s: %s\n", field->name, field->data);
    printf("\n");
}

int main(int argc, char* argv[]) {
    if (argc < 1) {
        printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
        return EXIT_FAILURE;
    }

    if (argc > 1)
        NutPunch_SetGameId(argv[1]);

    if (argc > 2)
        NutPunch_SetServerAddr(argv[2]);

    NutPunch_Register(NPCB_FoundLobbies, handle_lobby_list);
    NutPunch_Register(NPCB_FoundLobbyMetadata, handle_lobby_data);

    NutPunch_QueryMode();
    int threshold = 150, refresh = threshold;

    for (;;) {
        if (++refresh >= threshold) {
            NutPunch_FindLobbies(0, NULL);
            refresh = 0;
        }

        if (NPS_Error == NutPunch_Update()) {
            printf("failed to connect or smth\n");
            return EXIT_FAILURE;
        }

#ifdef NUTPUNCH_WINDOSE
        if (kbhit()) {
            (void)getch();
            break;
        }
#endif

        NP_SleepMs(1000 / 30);
    }

    NutPunch_Shutdown();
    return EXIT_SUCCESS;
}
