#ifndef NUTPUNCH_IMPLEMENTATION
#define NUTPUNCH_IMPLEMENTATION
#endif

#ifndef NUTPUNCH_COMPILE_SERVER
#define NUTPUNCH_COMPILE_SERVER
#endif

#include "nutpunch.h"

#ifdef NUTPUNCH_WINDOSE
#define sleepMs(ms) (Sleep((ms)))
#else
#error Bad luck.
#endif

int main(int argc, char* argv[]) {
	printf("Running...\n");

	for (;;) {
		NutPunch_Serve();
		sleepMs(5000 / NUTPUNCH_HEARTBEAT_RATE);
	}

	return 0;
}
