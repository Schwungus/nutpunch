#define NUTPUNCH_IMPLEMENTATION
#include "NutPunch.h"

#define LOBBY "Zaloopa"
#define WOWZA "FUCK YOU"

int main(int argc, char* argv[]) {
	if (argc > 1) {
		int players = strtol(argv[1], NULL, 10);
		NutPunch_Host(LOBBY, players);
	} else {
		NutPunch_Join(LOBBY);
	}

	for (;;) {
		if (NutPunch_Update() == NPS_Error) {
			NP_Warn("UPDATE WENT WRONG: %s", NutPunch_GetLastError());
			return EXIT_FAILURE;
		}
		if (NutPunch_PeerCount() >= NutPunch_GetMaxPlayers())
			break;
		NP_SleepMs(100);
	}

	for (int peer = 0; peer < NUTPUNCH_MAX_PLAYERS; peer++)
		if (peer != NutPunch_LocalPeer() && NutPunch_PeerAlive(peer))
			NutPunch_SendReliably(peer, WOWZA, sizeof(WOWZA));

	for (int i = 0; i < 30; i++) {
		NutPunch_Update();
		while (NutPunch_HasMessage()) {
			char data[sizeof(WOWZA)] = "";
			int size = sizeof(data), sender = NutPunch_NextMessage(data, &size);
			printf("from %2d: %s", sender, data);
		}
		NP_SleepMs(1000);
	}

	return EXIT_SUCCESS;
}
