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

// Just strip the damn [NP] prefix...
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

constexpr const int beatsPerSecond = 60, keepAliveSeconds = 3, keepAliveBeats = keepAliveSeconds * beatsPerSecond,
		    maxLobbies = 512;

static NP_Socket sock4 = NUTPUNCH_INVALID_SOCKET, sock6 = NUTPUNCH_INVALID_SOCKET;
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
		int nameLen = NUTPUNCH_FIELD_NAME_MAX, inputLen = static_cast<int>(std::strlen(name));
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
		int count = static_cast<int>(std::strlen(name));
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

struct Addr : NP_Addr {
	Addr() : Addr(false) {}
	Addr(NP_IPv ipv) {
		std::memset(&value, 0, sizeof(value));
		this->ipv = ipv;
	}

	sockaddr_in* asV4() {
		return reinterpret_cast<sockaddr_in*>(&value);
	}

	sockaddr_in6* asV6() {
		return reinterpret_cast<sockaddr_in6*>(&value);
	}

	template <typename T> int send(const T buf[], size_t size) const {
		const auto sock = ipv == NP_IPv6 ? sock6 : sock4;
		return sendto(sock, reinterpret_cast<const char*>(buf), static_cast<int>(size), 0,
			reinterpret_cast<const sockaddr*>(&value), sizeof(value));
	}

	int sendError(uint8_t code) const {
		int status = 0;
		static uint8_t buf[NUTPUNCH_HEADER_SIZE + 1] = "GTFO";
		buf[NUTPUNCH_HEADER_SIZE] = code;
		for (int i = 0; i < 5; i++)
			status |= send(buf, sizeof(buf));
		return status;
	}
};

struct Player {
	Addr addr;
	std::uint32_t countdown;
	Metadata metadata;

	Player(const Addr& addr) : addr(addr), countdown(keepAliveBeats) {}

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
			if (plr.countdown <= 0)
				continue;
			plr.countdown--;
			if (!plr.isDead())
				continue;
			NP_Log("Peer %d timed out in lobby '%s'", i + 1, fmtId());
			plr.reset();
		}
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			sendTo(i);
	}

	bool isDead() const {
		return !playerCount();
	}

	int playerCount() const {
		int count = 0;
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			count += !players[i].isDead();
		return count;
	}

	void processRequest(const int playerIdx, const char* meta) {
		players[playerIdx].countdown = keepAliveBeats;

		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
			const auto* fields = (Field*)meta;
			players[playerIdx].metadata.set(fields[i].name, fields[i].size, fields[i].data);
		}

		if (playerIdx != getMasterIdx())
			return;

		meta += sizeof(Metadata);
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) {
			const auto* fields = (Field*)meta;
			metadata.set(fields[i].name, fields[i].size, fields[i].data);
		}
	}

	void sendTo(const int playerIdx) {
		static uint8_t buf[NUTPUNCH_RESPONSE_SIZE] = "JOIN";
		if (players[playerIdx].isDead())
			return;

		uint8_t* ptr = buf + NUTPUNCH_HEADER_SIZE;
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			auto& cur = players[i];

			std::memset(ptr, 0, 19 + sizeof(Metadata));
			if (cur.isDead()) {
				ptr += 19 + sizeof(Metadata);
				continue;
			}

			*ptr++ = cur.addr.ipv;

			if (playerIdx != i) {
				if (cur.addr.ipv == NP_IPv6)
					std::memcpy(ptr, &cur.addr.asV6()->sin6_addr, 16);
				else
					std::memcpy(ptr, &cur.addr.asV4()->sin_addr, 4);
			}
			ptr += 16;

			if (cur.addr.ipv == NP_IPv6)
				std::memcpy(ptr, &cur.addr.asV6()->sin6_port, 2);
			else
				std::memcpy(ptr, &cur.addr.asV4()->sin_port, 2);
			ptr += 2;

			std::memcpy(ptr, cur.metadata.fields, sizeof(Metadata));
			ptr += sizeof(Metadata);
		}
		std::memcpy(ptr, metadata.fields, sizeof(Metadata));

		auto& player = players[playerIdx];
		if (player.addr.send(buf, sizeof(buf)) < 0 && NP_SockError() != NP_WouldBlock) {
			NP_Log("Player %d aborted connection", playerIdx + 1);
			player.reset();
		}
	}

	int getMasterIdx() {
		if (NUTPUNCH_MAX_PLAYERS != masterIdx && !players[masterIdx].isDead())
			return masterIdx;
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!players[i].isDead())
				return (masterIdx = i);
		return (masterIdx = NUTPUNCH_MAX_PLAYERS);
	}

private:
	int masterIdx = NUTPUNCH_MAX_PLAYERS;
};

static void bindSock(NP_IPv ipv) {
	auto& sock = ipv == NP_IPv6 ? sock6 : sock4;

	sock = socket(ipv == NP_IPv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
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

	sockaddr_storage addr;
	std::memset(&addr, 0, sizeof(addr));
	if (ipv == NP_IPv6) {
		reinterpret_cast<sockaddr_in6*>(&addr)->sin6_family = AF_INET6;
		reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port = htons(NUTPUNCH_SERVER_PORT);
	} else {
		reinterpret_cast<sockaddr_in*>(&addr)->sin_family = AF_INET;
		reinterpret_cast<sockaddr_in*>(&addr)->sin_port = htons(NUTPUNCH_SERVER_PORT);
	}
	if (!bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
		return;

	if (ipv == NP_IPv6) {
		// IPv6 is optional and skipped with a warning
		NP_Log("WARN: failed to bind IPv6 socket");
		sock = NUTPUNCH_INVALID_SOCKET;
	} else
		throw "Failed to bind IPv4 socket, and IPv6-only mode is unsupported";
}

static void sendLobbyList(Addr addr, const NutPunch_Filter* filters) {
	static uint8_t buf[NUTPUNCH_HEADER_SIZE + NP_LIST_LEN] = "LIST";
	uint8_t* ptr = buf + NUTPUNCH_HEADER_SIZE;
	size_t filterCount = 0;

	for (; filterCount < NUTPUNCH_SEARCH_FILTERS_MAX; filterCount++) {
		static constexpr const NutPunch_Filter nully = {0};
		if (!std::memcmp(&filters[filterCount], &nully, sizeof(*filters)))
			break;
	}
	if (!filterCount)
		return;

	std::memset(ptr, 0, (size_t)NP_LIST_LEN);
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

	nextLobby:
		continue;
	}

	for (int i = 0; i < 5; i++)
		addr.send(buf, sizeof(buf));
}

static int receiveShit(NP_IPv ipv) {
	auto& sock = ipv == NP_IPv6 ? sock6 : sock4;
	if (sock == NUTPUNCH_INVALID_SOCKET)
		return -1;
	Player* players = nullptr;

	char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	Addr addr(ipv);
#ifdef NUTPUNCH_WINDOSE
	int
#else
	socklen_t
#endif
		addrSize;
	addrSize = sizeof(addr.value);

	int rcv = recvfrom(sock, heartbeat, sizeof(heartbeat), 0, reinterpret_cast<sockaddr*>(&addr.value), &addrSize);
	if (rcv < 0 && NP_SockError() != NP_ConnReset)
		return NP_SockError() == NP_WouldBlock ? -1 : NP_SockError();

	const char* ptr = heartbeat + NUTPUNCH_HEADER_SIZE;
	if (!std::memcmp(heartbeat, "LIST", NUTPUNCH_HEADER_SIZE) && rcv == NUTPUNCH_HEADER_SIZE + sizeof(NP_Filters)) {
		sendLobbyList(addr, reinterpret_cast<const NutPunch_Filter*>(ptr));
		return 0;
	}
	if (std::memcmp(heartbeat, "JOIN", NUTPUNCH_HEADER_SIZE) || rcv != NUTPUNCH_HEARTBEAT_SIZE)
		return 0; // most likely junk...

	static char id[NUTPUNCH_ID_MAX + 1] = {0};
	std::memcpy(id, ptr, NUTPUNCH_ID_MAX);
	ptr += NUTPUNCH_ID_MAX;

	auto flags = *reinterpret_cast<const NP_HeartbeatFlagsStorage*>(ptr);
	ptr += sizeof(flags);

	if (!flags) // wtf do you want??
		return 0;

	if ((lobbies.count(id) && !(flags & NP_Beat_Join)) || (!lobbies.count(id) && !(flags & NP_Beat_Create)))
		goto exists;
	if (!lobbies.count(id) && lobbies.size() >= maxLobbies) {
		addr.sendError(NP_Err_NoSuchLobby); // TODO: update this error code
		NP_Log("WARN: Reached lobby limit...");
		return 0;
	}
	if (!lobbies.count(id)) {
		// Match against existing peers to prevent creating multiple lobbies with the same master.
		for (const auto& [lobbyId, lobby] : lobbies)
			for (const auto& player : lobby.players)
				if (!player.isDead() && !std::memcmp(&player.addr, &addr, sizeof(addr)))
					return 0; // fuck you...
		lobbies.insert({id, Lobby(id)});
		NP_Log("Created lobby '%s'", fmtLobbyId(id));
	}

	players = lobbies[id].players;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!players[i].isDead() && !std::memcmp(&players[i].addr.value, &addr.value, sizeof(addr.value))) {
			lobbies[id].processRequest(i, ptr);
			return 0;
		}
	}

	if ((flags & NP_Beat_Create) && lobbies[id].playerCount() > 0)
		goto exists;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!players[i].isDead())
			continue;

		players[i].addr = addr;
		lobbies[id].processRequest(i, ptr);

		const char* ipv_s = ipv == NP_IPv6 ? "IPv6" : "IPv4";
		NP_Log("Peer %d joined lobby '%s' (over %s)", i + 1, fmtLobbyId(id), ipv_s);
		return 0;
	}

	NP_Log("Lobby '%s' is full!", fmtLobbyId(id));
	return 0;

exists:
	addr.sendError(NP_Err_LobbyExists);
	return 0;
}

struct cleanup {
	cleanup() = default;
	~cleanup() {
		NP_NukeSocket(&sock4);
		NP_NukeSocket(&sock6);
#ifdef NUTPUNCH_WINDOSE
		WSACleanup();
#endif
	}
};

int main(int, char**) {
	cleanup __cleanup;

#ifdef NUTPUNCH_WINDOSE
	WSADATA __bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &__bitch);
#endif

	try {
		bindSock(true);
		bindSock(false);
	} catch (const char* msg) {
		NP_Log("CRITICAL: %s (code %d)", msg, NP_SockError());
		return EXIT_FAILURE;
	}

	std::int64_t start = clock(), end, delta;
	const std::int64_t minDelta = 1000 / beatsPerSecond;

	NP_Log("Running!");
	for (;;) {
		if (sock4 == NUTPUNCH_INVALID_SOCKET && sock6 == NUTPUNCH_INVALID_SOCKET)
			return EXIT_FAILURE;

		static constexpr const NP_IPv ipvs[2] = {NP_IPv6, NP_IPv4};
		for (const auto ipv : ipvs) {
			int result;
			while (!(result = receiveShit(ipv))) {
			}
			if (result > 0) {
				NP_Log("Failed to receive data (code %d)", result);
				(ipv == NP_IPv6 ? sock6 : sock4) = NUTPUNCH_INVALID_SOCKET;
			}
		}

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
