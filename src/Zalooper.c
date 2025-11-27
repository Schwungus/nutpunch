#define NUTPUNCH_IMPLEMENTATION
#include "NutPunch.h"

#define LOBBY "Zaloopah"
#define WOWZA "FUCK YOU"

int main(int argc, char* argv[]) {
	if (argc > 1)
		NutPunch_SetServerAddr(argv[1]);
	if (argc > 2) {
		int players = strtol(argv[2], NULL, 10);
		NutPunch_Host(LOBBY, players);
	} else {
		NutPunch_Join(LOBBY);
	}

	NP_Info("JOINGING>.....");
	for (;;) {
		if (NutPunch_Update() == NPS_Error) {
			NP_Warn("UPDATE WENT WRONG: %s", NutPunch_GetLastError());
			return EXIT_FAILURE;
		}
		if (NutPunch_LocalPeer() != NUTPUNCH_MAX_PLAYERS && NutPunch_PeerCount() >= NutPunch_GetMaxPlayers())
			break;
		NP_SleepMs(100);
	}

	NP_Info("SENDING SHIT OUT");
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
		NP_SleepMs(100);
	}

	NP_Info("DONE BYE");
	return EXIT_SUCCESS;
}
