#define NUTPUNCH_IMPLEMENTATION
#define NUTPUNCH_COMPILE_SERVER
#include "nutpunch.h"

#ifdef NUTPUNCH_WINDOSE
#include <windows.h>
#define sleepMs(ms) (Sleep((ms)))
#else
#error Bad luck.
#endif

int main(int argc, char* argv[]) {
	for (;;) {
		NutPunch_Serve();
		sleepMs(20);
	}
}
