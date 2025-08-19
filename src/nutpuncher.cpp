#include <cstring>
#include <map>
#include <string>

#include "nutpunch.h"

#define NutPunch_Log(...)                                                                                              \
	do {                                                                                                           \
		fprintf(stderr, __VA_ARGS__);                                                                          \
		fprintf(stderr, "\n");                                                                                 \
		fflush(stderr);                                                                                        \
	} while (0)

#ifdef NUTPUNCH_WINDOSE
#define sleepMs(ms) (Sleep((ms)))
#else
#error Bad luck.
#endif

struct Lobby;

constexpr const int beatsPerSecond = 30, keepAliveSeconds = 3, keepAliveBeats = keepAliveSeconds * beatsPerSecond,
		    maxLobbies = 512;

static SOCKET sock = INVALID_SOCKET;
static std::map<std::string, Lobby> lobbies;

struct Player {
	sockaddr_in addr;
	int countdown;

	Player(const sockaddr_in& addr) : addr(addr), countdown(keepAliveBeats) {}
	Player() : addr(*reinterpret_cast<const sockaddr_in*>(&zeroAddr)), countdown(0) {}

	bool isDead() const {
		return !countdown || !*reinterpret_cast<const char*>(&addr);
	}

      private:
	static constexpr const char zeroAddr[sizeof(addr)] = {0};
};

struct Payload {
	uint8_t buf[NUTPUNCH_PAYLOAD_SIZE] = {0}, *ptr = buf;

	operator const char*() const {
		return reinterpret_cast<const char*>(buf);
	}

	void write(const Player& player) {
		if (player.isDead()) {
			ptr += 6;
			return;
		}

		std::memcpy(ptr, &player.addr.sin_addr, 4);
		ptr += 4;

		std::uint16_t port = player.addr.sin_port;
		*ptr++ = (port >> 0) & 0xFF;
		*ptr++ = (port >> 8) & 0xFF;
	}
};

static const char* fmtLobbyId(const char* id) {
	static char buf[NUTPUNCH_ID_MAX + 1] = {0};

	for (int i = 0; i < sizeof(id); i++) {
		char c = id[i];
		if (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '-' || c == '_')
			buf[i] = c;
		else
			buf[i] = ' ';
	}
	for (int i = sizeof(id) - 1; i >= 0; i--)
		if (buf[i] != ' ' && buf[i] != '\0') {
			buf[i + 1] = '\0';
			break;
		}

	return buf;
}

struct Lobby {
	char id[NUTPUNCH_ID_MAX];
	Player players[NUTPUNCH_MAX_PLAYERS];

	Lobby() {
		std::memset(id, 0, sizeof(id));
		std::memset(players, 0, sizeof(players));
	}

	Lobby(const char* id) : Lobby() {
		std::memcpy(this->id, id, sizeof(this->id));
	}

	const char* fmtId() const {
		return fmtLobbyId(id);
	}

	void beat(const sockaddr_in& addr) {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (players[i].isDead())
				continue;
			if (!std::memcmp(&players[i].addr, &addr, sizeof(addr))) {
				players[i].countdown = keepAliveBeats;
				return;
			}
		}
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (players[i].isDead()) {
				players[i].addr = addr;
				players[i].countdown = keepAliveBeats;
				NutPunch_Log("Player %d joined lobby '%s'", i + 1, fmtId());
				return;
			}
		}
		NutPunch_Log("Lobby '%s' is full", fmtId());
	}

	void update() {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (players[i].countdown > 0) {
				players[i].countdown -= 1;
				if (players[i].isDead())
					NutPunch_Log("Player %d timed out in lobby '%s'", i + 1, fmtId());
			}
			if (players[i].isDead())
				continue;

			sockaddr clientAddr = *reinterpret_cast<sockaddr*>(&players[i].addr);
			int addrLen = sizeof(clientAddr);

			char recvId[NUTPUNCH_ID_MAX + 1] = {0};
			int64_t nRecv = recvfrom(sock, recvId, NUTPUNCH_ID_MAX, 0, &clientAddr, &addrLen);

			if (nRecv == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK
				&& WSAGetLastError() != WSAECONNRESET)
			{
				NutPunch_Log("Peer %d disconnect (code %d)", i + 1, WSAGetLastError());
				players[i].countdown = 0;
			}
			if (!nRecv) {
				NutPunch_Log("Peer %d gracefully disconnected", i + 1);
				players[i].countdown = 0;
			}
			if (nRecv != NUTPUNCH_ID_MAX)
				continue;
			if (std::memcmp(id, recvId, NUTPUNCH_ID_MAX)) {
				NutPunch_Log("Peer %d changed its lobby ID, which is currently unsupported", i + 1);
				players[i].countdown = 0;
				continue;
			}

			players[i].countdown = keepAliveBeats;
		}
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (players[i].isDead())
				continue;

			Payload pl;
			pl.write(players[i]);
			for (int j = 0; j < NUTPUNCH_MAX_PLAYERS; j++)
				if (i != j)
					pl.write(players[j]);
			const char* buf = pl;

			struct sockaddr addr = *reinterpret_cast<sockaddr*>(&players[i].addr);
			int sent = sendto(sock, buf, NUTPUNCH_PAYLOAD_SIZE, 0, &addr, sizeof(addr));
			if (sent != SOCKET_ERROR || WSAGetLastError() == WSAEWOULDBLOCK)
				continue;
			players[i].countdown = 0;
		}
	}

	bool isDead() const {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!players[i].isDead())
				return false;
		return true;
	}
};

static void bindSock() {
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET)
		throw "Failed to create the underlying UDP socket";

	u_long argp = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&argp, sizeof(argp)))
		throw "Failed to set socket reuseaddr option";

	argp = 1;
	if (SOCKET_ERROR == ioctlsocket(sock, FIONBIO, &argp))
		throw "Failed to set socket to non-blocking mode";

	sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = NUTPUNCH_SERVER_PORT;

	if (SOCKET_ERROR == bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
		throw "Failed to bind the UDP socket";
}

static int acceptConnections() {
	if (sock == INVALID_SOCKET) {
		std::exit(EXIT_FAILURE);
		return 0;
	}

	static timeval instantBitchNoodles = {0, 0};
	fd_set s = {1, {sock}};

	int res = select(0, &s, nullptr, nullptr, &instantBitchNoodles);
	if (res == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
		return WSAGetLastError();
	if (!res)
		return 0;

	char id[NUTPUNCH_ID_MAX + 1] = {0};
	sockaddr_in addr = {0};
	int addrLen = sizeof(addr);

	int nRecv = recvfrom(sock, id, NUTPUNCH_ID_MAX, 0, (struct sockaddr*)&addr, &addrLen);
	if (nRecv == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAECONNRESET)
		return WSAGetLastError();
	if (nRecv != NUTPUNCH_ID_MAX) // skip weirdass packets
		return 0;

	if (!lobbies.count(id)) {
		if (lobbies.size() > maxLobbies) {
			NutPunch_Log("WARN: Reached lobby limit...");
			return 0;
		} else {
			NutPunch_Log("Created lobby '%s'", fmtLobbyId(id));
			lobbies.insert({id, Lobby(id)});
		}
	}
	lobbies[id].beat(addr);

	return 0;
}

struct cleanup {
	cleanup() = default;
	~cleanup() {
		if (sock != INVALID_SOCKET) {
			closesocket(sock);
			sock = INVALID_SOCKET;
		}
		WSACleanup();
	}
};

int main(int, char**) {
	cleanup clnup;

	WSADATA bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &bitch);

	try {
		bindSock();
	} catch (const char* msg) {
		NutPunch_Log("Bind failed (code %d) - %s", WSAGetLastError(), msg);
		return EXIT_FAILURE;
	}

	NutPunch_Log("Running!");

	std::int64_t start = clock(), end, delta, minDelta = 1000 / beatsPerSecond;
	int acpt;

	for (;;) {
		if ((acpt = acceptConnections()))
			NutPunch_Log("Failed to accept connection (code %d)", acpt);

		for (auto& [id, lobby] : lobbies)
			if (!lobby.isDead())
				lobby.update();
		std::erase_if(lobbies, [](const auto& kv) {
			const auto& lobby = kv.second;
			bool dead = lobby.isDead();
			if (dead)
				NutPunch_Log("Deleted lobby '%s'", lobby.fmtId());
			return dead;
		});

		end = clock();
		delta = ((end - start) * 1000) / CLOCKS_PER_SEC;
		start = end;

		if (delta < minDelta)
			sleepMs(minDelta - delta);
	}

	return EXIT_SUCCESS;
}
