#include <stdio.h>
#include <stdlib.h>

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

#ifdef NUTPUNCH_WINDOSE
#include <windows.h>
#define SleepMs(ms) Sleep(ms)
#else
#error FIXME: too lazy to port this one
#endif

int main(int argc, char* argv[]) {
	if (argc < 3)
		goto fail;
	int data = strtol(argv[2], NULL, 10);
	if (!data)
		goto fail;
	if (argc >= 4)
		NutPunch_SetServerAddr(argv[3]);

	size_t len = strlen(argv[1]);
	if (len > NUTPUNCH_FIELD_NAME_MAX)
		len = NUTPUNCH_FIELD_NAME_MAX;
	else if (!len)
		goto fail;

	NutPunch_Filter filter = {0};
	memcpy(filter.name, argv[1], len);
	memcpy(filter.value, &data, sizeof(data));
	filter.comparison = NPF_Eq;
	NutPunch_FindLobbies(1, &filter);

	int rate = 20, ms = 1000;
	for (int i = 0; i < ms / rate; i++) {
		if (NPS_Error == NutPunch_Update())
			goto shit;
		SleepMs(ms / rate);
	}

	int lobbyCount = NutPunch_LobbyCount();
	printf("%d %s", lobbyCount, lobbyCount == 1 ? "lobby" : "lobbies");
	if (lobbyCount)
		printf(":");
	printf("\n");
	for (int i = 0; i < lobbyCount; i++)
		printf("'%s' ", NutPunch_GetLobby(i)->name);
	if (lobbyCount)
		printf("\n");
	return EXIT_SUCCESS;

fail:
	printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
	return EXIT_FAILURE;

shit:
	printf("failed to connect or smth\n");
	return EXIT_FAILURE;
}
