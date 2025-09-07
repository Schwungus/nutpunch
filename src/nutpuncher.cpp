#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <string>

#define NUTPUNCH_IMPLEMENTATION
#include "nutpunch.h"

#ifdef NP_Log
#undef NP_Log
#endif

// Just remove the damn prefix...
#define NP_Log(...)                                                                                                    \
	do {                                                                                                           \
		fprintf(stdout, __VA_ARGS__);                                                                          \
		fprintf(stdout, "\n");                                                                                 \
		fflush(stdout);                                                                                        \
	} while (0)

#ifdef NUTPUNCH_WINDOSE
#define SleepMs(ms) Sleep(ms)
#else
#include <time.h> // stolen from: <https://stackoverflow.com/a/1157217>
#define SleepMs(ms)                                                                                                    \
	do {                                                                                                           \
		struct timespec ts;                                                                                    \
		int res;                                                                                               \
		ts.tv_sec = (ms) / 1000;                                                                               \
		ts.tv_nsec = ((ms) % 1000) * 1000000;                                                                  \
		do                                                                                                     \
			res = nanosleep(&ts, &ts);                                                                     \
		while (res && errno == EINTR);                                                                         \
	} while (0)
#endif

struct Lobby;

constexpr const int beatsPerSecond = 60, keepAliveSeconds = 5, keepAliveBeats = keepAliveSeconds * beatsPerSecond,
		    maxLobbies = 512;

static NP_SocketType sock = NUTPUNCH_INVALID_SOCKET;
static std::map<std::string, Lobby> lobbies;

static const char* fmtLobbyId(const char* id) {
	static char buf[NUTPUNCH_ID_MAX + 1] = {0};
	for (int i = 0; i < NUTPUNCH_ID_MAX; i++) {
		char c = id[i];
		if (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '-' || c == '_')
			buf[i] = c;
		else if (!c) {
			buf[i] = 0;
			return buf;
		} else
			buf[i] = ' ';
	}
	for (int i = NUTPUNCH_ID_MAX; i >= 0; i--)
		if (buf[i] != ' ' && buf[i] != 0) {
			buf[i + 1] = '\0';
			return buf;
		}
	return buf;
}

struct Field : NutPunch_Field {
	Field() {
		reset();
	}

	bool isDead() const {
		static const Field nully;
		if (!size)
			return true;
		return !std::memcmp(name, nully.name, sizeof(name));
	}

	bool nameMatches(const char* name) const {
		if (isDead())
			return false;
		int nameLen = NUTPUNCH_FIELD_NAME_MAX, inputLen = std::strlen(name);
		if (inputLen > NUTPUNCH_FIELD_NAME_MAX)
			inputLen = NUTPUNCH_FIELD_NAME_MAX;
		for (int i = 0; i < inputLen; i++)
			if (!name[i]) {
				nameLen = i;
				break;
			}
		if (!nameLen)
			return false;
		if (nameLen != inputLen)
			return false;
		return !std::memcmp(this->name, name, nameLen);
	}

	bool filterMatches(const NutPunch_Filter& filter) const {
		if (isDead())
			return false;
		if (!nameMatches(filter.name))
			return false;
		if (std::memcmp(data, filter.value, size) == filter.comparison)
			return true;
		return false;
	}

	void reset() {
		std::memset(name, 0, sizeof(name));
		std::memset(data, 0, sizeof(data));
		size = 0;
	}
};

struct Metadata {
	Field fields[NUTPUNCH_MAX_FIELDS];

	Metadata() {
		reset();
	}

	Field* get(const char* name) {
		int idx = find(name);
		if (NUTPUNCH_MAX_FIELDS == idx)
			return nullptr;
		return &fields[idx];
	}

	void set(const char* name, int size, const void* value) {
		int idx = find(name);
		if (NUTPUNCH_MAX_FIELDS == idx)
			return;
		int count = std::strlen(name);
		if (count > NUTPUNCH_FIELD_NAME_MAX)
			count = NUTPUNCH_FIELD_NAME_MAX;
		std::memcpy(fields[idx].name, name, count);
		fields[idx].size = size;
		std::memcpy(fields[idx].data, value, size);
	}

	void reset() {
		std::memset(fields, 0, sizeof(fields));
	}

private:
	int find(const char* name) {
		if (!*name)
			return NUTPUNCH_MAX_FIELDS;
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) // first matching
			if (fields[i].nameMatches(name))
				return i;
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) // or first empty
			if (fields[i].isDead())
				return i;
		return NUTPUNCH_MAX_FIELDS; // or bust
	}
};

struct Player {
	sockaddr_in addr;
	std::uint32_t countdown;
	Metadata metadata;

	Player(const sockaddr_in& addr) : addr(addr), countdown(keepAliveBeats) {}

	Player() : countdown(0) {
		std::memset(&addr, 0, sizeof(addr));
	}

	bool isDead() const {
		static constexpr const char zeroAddr[sizeof(addr)] = {0};
		return !countdown || !std::memcmp(&addr, zeroAddr, sizeof(addr));
	}

	void reset() {
		std::memset(&addr, 0, sizeof(addr));
		metadata.reset();
		countdown = 0;
	}
};

struct Lobby {
	char id[NUTPUNCH_ID_MAX];
	Player players[NUTPUNCH_MAX_PLAYERS];
	Metadata metadata;

	Lobby() : Lobby(nullptr) {}
	Lobby(const char* id) {
		if (id == nullptr)
			std::memset(this->id, 0, sizeof(this->id));
		else
			std::memcpy(this->id, id, sizeof(this->id));
		std::memset(players, 0, sizeof(players));
	}

	const char* fmtId() const {
		return fmtLobbyId(id);
	}

	void update() {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			auto& plr = players[i];
			if (plr.countdown > 0) {
				plr.countdown--;
				if (plr.isDead()) {
					NP_Log("Peer %d timed out in lobby '%s'", i + 1, fmtId());
					plr.reset();
				}
			}
		}
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			sendTo(i);
	}

	bool isDead() const {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!players[i].isDead())
				return false;
		return true;
	}

	void processRequest(const int playerIdx, const char* request) {
		if (std::memcmp(request, id, NUTPUNCH_ID_MAX))
			return;

		players[playerIdx].countdown = keepAliveBeats;

		const char* ptr = request + NUTPUNCH_ID_MAX;
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
			const auto* fields = (Field*)ptr;
			players[playerIdx].metadata.set(fields[i].name, fields[i].size, fields[i].data);
		}

		if (playerIdx != getMasterIdx())
			return;

		ptr += sizeof(Metadata);
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
			const auto* fields = (Field*)ptr;
			metadata.set(fields[i].name, fields[i].size, fields[i].data);
		}
	}

	void sendTo(const int playerIdx) {
		auto& plr = players[playerIdx];
		if (plr.isDead())
			return;

		static uint8_t buf[NUTPUNCH_RESPONSE_SIZE] = {'J', 'O', 'I', 'N', 0};
		uint8_t* ptr = buf + NUTPUNCH_HEADER_SIZE;

		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (players[i].isDead()) {
				std::memset(ptr, 0, 6);
				ptr += 6;

				std::memset(ptr, 0, sizeof(Metadata));
				ptr += sizeof(Metadata);
			} else {
				if (playerIdx == i)
					std::memset(ptr, 0, 4);
				else
					std::memcpy(ptr, &players[i].addr.sin_addr, 4);
				ptr += 4;

				std::memcpy(ptr, &players[i].addr.sin_port, 2);
				ptr += 2;

				std::memcpy(ptr, players[i].metadata.fields, sizeof(Metadata));
				ptr += sizeof(Metadata);
			}
		}
		std::memcpy(ptr, metadata.fields, sizeof(Metadata));

		auto addr = *reinterpret_cast<sockaddr*>(&plr.addr);
		int sent = sendto(sock, (char*)buf, NUTPUNCH_RESPONSE_SIZE, 0, &addr, sizeof(addr));
		if (sent < 0 && NP_SockError() != NP_WouldBlock)
			plr.reset();
	}

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

private:
	int masterIdx = NUTPUNCH_MAX_PLAYERS;
};

static void bindSock() {
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == NUTPUNCH_INVALID_SOCKET)
		throw "Failed to create the underlying UDP socket";

#ifdef NUTPUNCH_WINDOSE
	u_long
#else
	std::uint32_t
#endif
		argp;

	argp = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&argp, sizeof(argp)))
		throw "Failed to set socket reuseaddr option";

	argp = 1;
	if (
#ifdef NUTPUNCH_WINDOSE
		ioctlsocket(sock, FIONBIO, &argp)
#else
		fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK)
#endif
		< 0)
		throw "Failed to set socket to non-blocking mode";

	sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(NUTPUNCH_SERVER_PORT);

	if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
		throw "Failed to bind the UDP socket";
}

static void sendLobbyList(sockaddr addr, const NutPunch_Filter* filters) {
	static uint8_t buf[NUTPUNCH_RESPONSE_SIZE] = {'L', 'I', 'S', 'T', 0};
	uint8_t* ptr = buf + NUTPUNCH_HEADER_SIZE;
	int resultCount = 0, filterCount = 0;

	for (; filterCount < NUTPUNCH_SEARCH_FILTERS_MAX; filterCount++) {
		static constexpr const NutPunch_Filter nully = {0};
		if (!std::memcmp(&filters[filterCount], &nully, sizeof(*filters)))
			break;
	}
	if (!filterCount)
		return;

	std::memset(ptr, 0, (size_t)(NUTPUNCH_ID_MAX * NUTPUNCH_SEARCH_RESULTS_MAX));
	for (const auto& [id, lobby] : lobbies) {
		for (int f = 0; f < filterCount; f++) {
			for (int m = 0; m < NUTPUNCH_MAX_FIELDS; m++)
				if (lobby.metadata.fields[m].filterMatches(filters[f]))
					goto nextFilter;
			goto nextLobby; // no field matched the filter

		nextFilter:
			continue;
		}

		// All filters matched.
		std::memset(ptr, 0, NUTPUNCH_ID_MAX);
		std::memcpy(ptr, id.data(), std::strlen(lobby.fmtId()));
		ptr += NUTPUNCH_ID_MAX;
		resultCount++;

	nextLobby:
		continue;
	}

	for (int i = 0; i < 5; i++)
		sendto(sock, (char*)buf, NUTPUNCH_HEADER_SIZE + NP_LIST_LEN, 0, &addr, sizeof(addr));
}

static int receiveShit() {
	if (sock == NUTPUNCH_INVALID_SOCKET)
		std::exit(EXIT_FAILURE);

	char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	sockaddr addr = {0};
#ifdef NUTPUNCH_WINDOSE
	int
#else
	socklen_t
#endif
		addrSize;
	addrSize = sizeof(addr);

	int rcv = recvfrom(sock, heartbeat, sizeof(heartbeat), 0, &addr, &addrSize);
	if (rcv < 0 && NP_SockError() != NP_ConnReset)
		return NP_SockError() == NP_WouldBlock ? -1 : NP_SockError();

	char* ptr = heartbeat + NUTPUNCH_HEADER_SIZE;
	if (!std::memcmp(heartbeat, "LIST", NUTPUNCH_HEADER_SIZE) && rcv == NUTPUNCH_HEADER_SIZE + sizeof(NP_Filters)) {
		sendLobbyList(addr, reinterpret_cast<const NutPunch_Filter*>(ptr));
		return 0;
	}
	if (!std::memcmp(heartbeat, "JOIN", NUTPUNCH_HEADER_SIZE) && rcv == NUTPUNCH_HEARTBEAT_SIZE)
		goto process;
	return 0; // most likely junk...

process:
	static char id[NUTPUNCH_ID_MAX + 1] = {0};
	std::memcpy(id, ptr, NUTPUNCH_ID_MAX);

	if (!lobbies.count(id)) {
		if (lobbies.size() >= maxLobbies) {
			NP_Log("WARN: Reached lobby limit...");
			return 0;
		}

		// Match against existing peers to prevent creating multiple lobbies with the same master.
		for (const auto& [lobbyId, lobby] : lobbies)
			for (const auto& player : lobby.players)
				if (!player.isDead() && !std::memcmp(&player.addr, &addr, sizeof(addr)))
					return 0; // fuck you...

		NP_Log("Created lobby '%s'", fmtLobbyId(id));
		lobbies.insert({id, Lobby(id)});
	}

	auto* players = lobbies[id].players;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!players[i].isDead() && !std::memcmp(&players[i].addr, &addr, sizeof(addr))) {
			lobbies[id].processRequest(i, ptr);
			return 0;
		}
	}
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (players[i].isDead()) {
			std::memcpy(&players[i].addr, &addr, sizeof(addr));
			lobbies[id].processRequest(i, ptr);
			NP_Log("Peer %d joined lobby '%s'", i + 1, fmtLobbyId(id));
			return 0;
		}
	}

	NP_Log("Lobby '%s' is full!", fmtLobbyId(id));
	return 0;
}

struct cleanup {
	cleanup() = default;
	~cleanup() {
		if (sock != NUTPUNCH_INVALID_SOCKET) {
#ifdef NUTPUNCH_WINDOSE
			closesocket(sock);
#else
			close(sock);
#endif
			sock = NUTPUNCH_INVALID_SOCKET;
		}
#ifdef NUTPUNCH_WINDOSE
		WSACleanup();
#endif
	}
};

int main(int, char**) {
	cleanup clnup;

#ifdef NUTPUNCH_WINDOSE
	WSADATA bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &bitch);
#endif

	try {
		bindSock();
	} catch (const char* msg) {
		NP_Log("Bind failed (code %d) - %s", NP_SockError(), msg);
		return EXIT_FAILURE;
	}

	std::int64_t start = clock(), end, delta;
	const std::int64_t minDelta = 1000 / beatsPerSecond;

	NP_Log("Running!");
	for (;;) {
		int result;
		while (!(result = receiveShit())) {
		}
		if (result > 0)
			NP_Log("Failed to receive data (code %d)", result);

		for (auto& [id, lobby] : lobbies)
			lobby.update();
		std::erase_if(lobbies, [](const auto& kv) {
			const auto& lobby = kv.second;
			bool dead = lobby.isDead();
			if (dead)
				NP_Log("Deleted lobby '%s'", lobby.fmtId());
			return dead;
		});

		end = clock();
		delta = ((end - start) * 1000) / CLOCKS_PER_SEC;

		if (delta < minDelta)
			SleepMs(minDelta - delta);
		start = clock();
	}

	return EXIT_SUCCESS;
}
