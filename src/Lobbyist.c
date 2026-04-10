#include <stdio.h>
#include <stdlib.h>

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

int main(int argc, char* argv[]) {
    if (argc < 1) {
        printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
        return EXIT_FAILURE;
    } else if (argc > 1) {
        NutPunch_SetServerAddr(argv[1]);
    }

    NutPunch_FindLobbies(0, NULL);

    static const int rate = 20, ms = 1000;
    for (int t = 0; t < ms / rate; t++) {
        // request each lobby's metadata since that's a separate step...
        for (int i = 0; i < NutPunch_LobbyCount(); i++)
            NutPunch_GetLobbyData(NutPunch_GetLobby(i)->name, "hello", NULL);

        if (NPS_Error == NutPunch_Update()) {
            printf("failed to connect or smth\n");
            return EXIT_FAILURE;
        }

        NP_SleepMs(ms / rate);
    }

    int lobby_count = NutPunch_LobbyCount();
    printf("%d %s", lobby_count, lobby_count == 1 ? "lobby" : "lobbies");

    if (lobby_count)
        printf(":\n");

    static const char zeroname[NUTPUNCH_FIELD_NAME_MAX] = {0};

    for (int i = 0; i < lobby_count; i++) {
        const NutPunch_LobbyInfo* lober = NutPunch_GetLobby(i);

        printf("\n'%s'", lober->name);

        if (!lober->got_meta) {
            printf("\n  no bitches :(");
            continue;
        }

        const NutPunch_Field *fields = lober->metadata, *end = fields + NUTPUNCH_MAX_FIELDS,
                             *ptr = fields;

        while (ptr < end && memcmp(ptr->name, zeroname, sizeof(zeroname))) {
            printf("\n  %.*s: %.*s", NUTPUNCH_FIELD_NAME_MAX, ptr->name, ptr->size, ptr->data);
            ptr += 1;
        }

        printf("\n");
    }

    return EXIT_SUCCESS;
}
