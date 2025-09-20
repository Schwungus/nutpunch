#include <stdlib.h>

#include <nutpunch.h>

#ifdef NUTPUNCH_WINDOSE
#define _AMD64_
#define _INC_WINDOWS
#include <synchapi.h>
#include <windef.h>
#include <winsock2.h>
#define SleepMs(ms) Sleep(ms)
#else
#error FIXME: too lazy to port this one
#endif

int main(int argc, char* argv[]) {
	if (argc != 4)
		goto fail;
	int data = strtol(argv[2], NULL, 10);
	if (!data)
		goto fail;
	NutPunch_SetServerAddr(argv[3]);

	struct NutPunch_Filter filter = {0};
	memcpy(filter.name, argv[1], NUTPUNCH_FIELD_NAME_MAX);
	memcpy(filter.value, &data, sizeof(data));
	filter.comparison = 0;

	NutPunch_FindLobbies(1, &filter);

	int rate = 20, ms = 1000;
	for (int i = 0; i < ms / rate; i++) {
		NutPunch_Update();
		SleepMs(ms / rate);
	}

	int lobbyCount = NutPunch_LobbyCount();
	printf("%d lobbies\n", lobbyCount);
	for (int i = 0; i < lobbyCount; i++)
		printf("'%s' ", NutPunch_GetLobby(i));

	return EXIT_SUCCESS;

fail:
	printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
	return EXIT_FAILURE;
}
