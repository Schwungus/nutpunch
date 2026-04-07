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

	NutPunch_FindLobbies();

	int rate = 20, ms = 1000;
	for (int i = 0; i < ms / rate; i++) {
		if (NPS_Error == NutPunch_Update()) {
			printf("failed to connect or smth\n");
			return EXIT_FAILURE;
		}
		NP_SleepMs(ms / rate);
	}

	int lobby_count = NutPunch_LobbyCount();
	printf("%d %s", lobby_count, lobby_count == 1 ? "lobby" : "lobbies");
	if (lobby_count)
		printf(":");
	printf("\n");
	for (int i = 0; i < lobby_count; i++)
		printf("'%s' ", NutPunch_GetLobby(i)->name);
	if (lobby_count)
		printf("\n");
	return EXIT_SUCCESS;
}
