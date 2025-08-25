#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>

#include "nutpunch.h"

struct Lobby;

constexpr const int beatsPerSecond = 60, keepAliveSeconds = 5, keepAliveBeats = keepAliveSeconds * beatsPerSecond,
		    maxLobbies = 512;

static SOCKET sock = INVALID_SOCKET;
static std::map<std::string, Lobby> lobbies;

struct Player {
	sockaddr_in addr;
	std::uint32_t countdown;

	Player(const sockaddr_in& addr) : addr(addr), countdown(keepAliveBeats) {}
	Player() : addr(*reinterpret_cast<const sockaddr_in*>(&zeroAddr)), countdown(0) {}

	bool isDead() const {
		return !countdown || !std::memcmp(&addr, zeroAddr, sizeof(addr));
	}

	void reset() {
		std::memset(&addr, 0, sizeof(addr));
		countdown = 0;
	}

private:
	static constexpr const char zeroAddr[sizeof(addr)] = {0};
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

struct Field : NutPunch_Field {
	Field() {
		reset();
	}

	bool isDead() const {
		if (!size)
			return true;
		static const Field nully;
		if (!std::memcmp(name, nully.name, sizeof(name)))
			return true;
		return false;
	}

	bool nameMatches(const char* name) const {
		if (isDead())
			return false;
		int nameLen = std::strlen(name);
		if (nameLen > NUTPUNCH_FIELD_NAME_MAX)
			nameLen = NUTPUNCH_FIELD_NAME_MAX;
		if (std::strlen(this->name) > nameLen)
			return false;
		return !std::memcmp(this->name, name, nameLen);
	}

	void reset() {
		std::memset(name, 0, sizeof(name));
		std::memset(data, 0, sizeof(data));
		size = 0;
	}
};

struct Lobby {
	char id[NUTPUNCH_ID_MAX];
	Player players[NUTPUNCH_MAX_PLAYERS];
	Field metadata[NUTPUNCH_MAX_FIELDS];

	Lobby() : Lobby(nullptr) {}
	Lobby(const char* id) {
		if (id == nullptr)
			std::memset(this->id, 0, sizeof(this->id));
		else
			std::memcpy(this->id, id, sizeof(this->id));
		std::memset(players, 0, sizeof(players));
		std::memset(metadata, 0, sizeof(metadata));
	}

	const char* fmtId() const {
		return fmtLobbyId(id);
	}

	void update() {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			receiveFrom(i);
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			sendTo(i);
	}

	bool isDead() const {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!players[i].isDead())
				return false;
		return true;
	}

private:
	void receiveFrom(const int playerIdx) {
		auto& plr = players[playerIdx];
		if (plr.countdown > 0) {
			plr.countdown -= 1;
			if (plr.isDead())
				NutPunch_Log("Peer %d timed out in lobby '%s'", playerIdx + 1, fmtId());
		}
		for (;;) {
			if (plr.isDead()) {
				plr.reset();
				return;
			}

			char request[NUTPUNCH_REQUEST_SIZE] = {0};
			sockaddr clientAddr = *reinterpret_cast<sockaddr*>(&plr.addr);
			int addrLen = sizeof(clientAddr);

			std::int64_t nRecv = recvfrom(sock, request, sizeof(request), 0, &clientAddr, &addrLen);
			if (nRecv == SOCKET_ERROR) {
				if (WSAGetLastError() == WSAEWOULDBLOCK)
					return;
				NutPunch_Log("Peer %d disconnect (code %d)", playerIdx + 1, WSAGetLastError());
				plr.reset();
				return;
			}
			if (!nRecv) {
				NutPunch_Log("Peer %d gracefully disconnected", playerIdx + 1);
				plr.reset();
				return;
			}
			if (nRecv != sizeof(request))
				continue;
			if (std::memcmp(id, request, NUTPUNCH_ID_MAX)) {
				NutPunch_Log(
					"Peer %d changed its lobby ID, which is currently unsupported", playerIdx + 1);
				plr.reset();
				return;
			}
			plr.countdown = keepAliveBeats;

			if (playerIdx != getMasterIdx())
				continue;
			const auto* fields = (Field*)&request[NUTPUNCH_ID_MAX];
			for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
				if (fields[i].isDead())
					continue;
				int idx = nextFieldIdx(fields[i].name);
				if (NUTPUNCH_MAX_FIELDS != idx)
					std::memcpy(&metadata[idx], &fields[i], sizeof(*fields));
			}
		}
	}

	void sendTo(const int playerIdx) {
		auto& plr = players[playerIdx];
		if (plr.isDead())
			return;

		static uint8_t buf[NUTPUNCH_RESPONSE_SIZE] = {0};
		uint8_t* ptr = buf;

		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (players[i].isDead()) {
				std::memset(ptr, 0, 6);
				ptr += 6;
			} else {
				if (playerIdx == i)
					std::memset(ptr, 0xFF, 4);
				else
					std::memcpy(ptr, &players[i].addr.sin_addr, 4);
				ptr += 4;

				if (playerIdx == i)
					std::memset(ptr, 0, 2);
				else
					std::memcpy(ptr, &players[i].addr.sin_port, 2);
				ptr += 2;
			}
		}
		std::memcpy(ptr, metadata, sizeof(metadata));

		auto addr = *reinterpret_cast<sockaddr*>(&plr.addr);
		int sent = sendto(sock, (char*)buf, sizeof(buf), 0, &addr, sizeof(addr));
		if (sent == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
			plr.reset();
	}

	int masterIdx = NUTPUNCH_MAX_PLAYERS;

	int getMasterIdx() {
		if (NUTPUNCH_MAX_PLAYERS == masterIdx || players[masterIdx].isDead()) {
			for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
				if (!players[i].isDead()) {
					masterIdx = i;
					return masterIdx;
				}
			masterIdx = NUTPUNCH_MAX_PLAYERS;
		}
		return masterIdx;
	}

	int nextFieldIdx(const char* name) {
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) // first matching
			if (metadata[i].nameMatches(name))
				return i;
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) // or first empty
			if (metadata[i].isDead())
				return i;
		return NUTPUNCH_MAX_FIELDS; // or bust
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
	addr.sin_port = htons(NUTPUNCH_SERVER_PORT);

	if (SOCKET_ERROR == bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
		throw "Failed to bind the UDP socket";
}

static int acceptConnections() {
	if (sock == INVALID_SOCKET)
		std::exit(EXIT_FAILURE);

	static timeval instantBitchNoodles = {0, 0};
	fd_set s = {1, {sock}};

	int res = select(0, &s, nullptr, nullptr, &instantBitchNoodles);
	if (res == SOCKET_ERROR)
		return WSAGetLastError() == WSAEWOULDBLOCK ? -1 : WSAGetLastError();
	if (!res)
		return -1;

	char request[NUTPUNCH_REQUEST_SIZE] = {0};
	sockaddr_in addr = {0};
	int addrLen = sizeof(addr);

	int nRecv = recvfrom(sock, request, sizeof(request), 0, reinterpret_cast<sockaddr*>(&addr), &addrLen);
	if (SOCKET_ERROR == nRecv && WSAGetLastError() != WSAECONNRESET)
		return WSAGetLastError() == WSAEWOULDBLOCK ? -1 : WSAGetLastError();
	if (sizeof(request) != nRecv) // skip weirdass packets
		return 0;

	static char id[NUTPUNCH_ID_MAX + 1] = {0};
	std::memcpy(id, request, NUTPUNCH_ID_MAX);

	if (!lobbies.count(id)) {
		if (lobbies.size() > maxLobbies) {
			NutPunch_Log("WARN: Reached lobby limit...");
			return 0;
		} else {
			NutPunch_Log("Created lobby '%s'", fmtLobbyId(id));
			lobbies.insert({id, Lobby(id)});
		}
	}

	auto& players = lobbies[id].players;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (players[i].isDead())
			continue;
		if (!std::memcmp(&players[i].addr, &addr, sizeof(addr))) {
			players[i].countdown = keepAliveBeats;
			return 0;
		}
	}
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (players[i].isDead()) {
			players[i].addr = addr;
			players[i].countdown = keepAliveBeats;
			NutPunch_Log("Peer %d joined lobby '%s'", i + 1, fmtLobbyId(id));
			return 0;
		}
	}

	NutPunch_Log("Lobby '%s' is full!", fmtLobbyId(id));
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

	std::int64_t start = clock(), end, delta;
	const std::int64_t minDelta = 1000 / beatsPerSecond;

	NutPunch_Log("Running!");
	for (;;) {
		int acpt;
		while (!(acpt = acceptConnections())) {
		}
		if (acpt > 0)
			NutPunch_Log("Failed to accept connection (code %d)", acpt);

		for (auto& [id, lobby] : lobbies)
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
			NutPunch_SleepMs(minDelta - delta);
	}

	return EXIT_SUCCESS;
}
