#include <stdlib.h>

#define NUTPUNCH_IMPLEMENTATION
#include "nutpunch.h"

#ifdef NUTPUNCH_WINDOSE
#include <windows.h>
#define sleepMs(ms) (Sleep((ms)))
#else
#error Bad luck.
#endif

int main(int argc, char* argv[]) {
	uint16_t port = 0;
	NutPunch_SetServerAddr("127.0.0.1");

	for (;;) {
		int query = NutPunch_Query();
		if (query == NP_Status_Idle || query == NP_Status_Error) {
			printf("\nreconnecting...");
			NutPunch_Join("aabb");
		}
		if (query == NP_Status_Error) {
			printf("\nnutpunch failed......");
			return EXIT_FAILURE;
		}

		printf("\n\rcount = %d; our port = ", NutPunch_GetPeerCount());
		if (query == NP_Status_Punched) {
			printf("%d", NutPunch_Release());
		} else {
			printf("none yet");
		}

		sleepMs(500);
	}

	NutPunch_Cleanup();
	return EXIT_SUCCESS;
}
