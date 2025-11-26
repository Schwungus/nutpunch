#include <stdio.h>
#include <stdlib.h>

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

#ifdef NUTPUNCH_WINDOSE
#define sleep_ms(ms) Sleep(ms)
#else
#include <time.h>
static void sleep_ms(int ms) {
	// Stolen from: <https://stackoverflow.com/a/1157217>
	struct timespec ts;
	ts.tv_sec = (ms) / 1000;
	ts.tv_nsec = ((ms) % 1000) * 1000000;
	int res;
	do
		res = nanosleep(&ts, &ts);
	while (res && errno == EINTR);
}
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
	memcpy(filter.field.name, argv[1], len);
	memcpy(filter.field.value, &data, sizeof(data));
	filter.comparison = NPF_Eq;
	NutPunch_FindLobbies(1, &filter);

	int rate = 20, ms = 1000;
	for (int i = 0; i < ms / rate; i++) {
		if (NPS_Error == NutPunch_Update())
			goto shit;
		sleep_ms(ms / rate);
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

fail:
	printf("YOU FIALED ME!!!! NOW SUFFERRRRR\n");
	return EXIT_FAILURE;

shit:
	printf("failed to connect or smth\n");
	return EXIT_FAILURE;
}
