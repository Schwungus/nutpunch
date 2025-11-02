#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <string>

#define NUTPUNCH_IMPLEMENTATION
#include <NutPunch.h>

#ifdef NUTPUNCH_WINDOSE
#define SleepMs(ms) Sleep(ms)
#else
#include <time.h>
#define SleepMs(ms) sleepUnix(ms)
static void sleepUnix(int ms) {
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

	bool dead() const {
		static constexpr const char nullname[sizeof(name)] = {0};
		return !size || !std::memcmp(name, nullname, sizeof(name));
	}

	bool named(const char* name) const {
		if (!name || !*name || dead())
			return false;

		int argLen = static_cast<int>(std::strlen(name));
		if (argLen > NUTPUNCH_FIELD_NAME_MAX)
			argLen = NUTPUNCH_FIELD_NAME_MAX;

		int ourLen = NUTPUNCH_FIELD_NAME_MAX;
		for (int i = 1; i < NUTPUNCH_FIELD_NAME_MAX; i++)
			if (!this->name[i]) {
				ourLen = i;
				break;
			}

		return ourLen == argLen && !std::memcmp(this->name, name, ourLen);
	}

	bool matches(const NutPunch_Filter& filter) const {
		const int diff = std::memcmp(data, filter.value, size), eq = !diff, flags = filter.comparison;
		bool result = true;
		if (flags & NPF_Greater) {
			result &= diff > 0;
			if (flags & NPF_Eq)
				result |= eq;
		} else if (flags & NPF_Less) {
			result &= diff < 0;
			if (flags & NPF_Eq)
				result |= eq;
		} else if (flags & NPF_Eq)
			result &= eq;
		else // junk
			return false;
		return (flags & NPF_Not) ? !result : result;
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

	Field* find(const char* name) {
		const int idx = indexOf(name);
		return NUTPUNCH_MAX_FIELDS == idx ? nullptr : &fields[idx];
	}

	void insert(const Field& field) {
		if (!field.size)
			return;

		const int idx = indexOf(field.name);
		if (NUTPUNCH_MAX_FIELDS == idx)
			return;

		int nameSize = static_cast<int>(std::strlen(field.name));
		if (nameSize > NUTPUNCH_FIELD_NAME_MAX)
			nameSize = NUTPUNCH_FIELD_NAME_MAX;

		int dataSize = field.size;
		if (dataSize > NUTPUNCH_FIELD_DATA_MAX)
			dataSize = NUTPUNCH_FIELD_DATA_MAX;

		auto& target = fields[idx];
		std::memcpy(target.name, field.name, nameSize);
		std::memcpy(target.data, field.data, dataSize);
		target.size = dataSize;
	}

	void load(const char* ptr) {
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++)
			insert(reinterpret_cast<const Field*>(ptr)[i]);
	}

	void reset() {
		std::memset(fields, 0, sizeof(fields));
	}

private:
	int indexOf(const char* name) {
		if (!name || !*name)
			return NUTPUNCH_MAX_FIELDS;
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) // first matching
			if (fields[i].named(name))
				return i;
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++) // or first uninitialized
			if (fields[i].dead())
				return i;
		return NUTPUNCH_MAX_FIELDS; // or bust
	}
};

struct Addr : NP_Addr {
	Addr() : Addr(NP_IPv4) {}
	Addr(NP_IPv ipv) {
		std::memset(&raw, 0, sizeof(raw));
		this->ipv = ipv;
	}

	sockaddr_in* v4() {
		return reinterpret_cast<sockaddr_in*>(&raw);
	}

	const sockaddr_in* v4() const {
		return reinterpret_cast<const sockaddr_in*>(&raw);
	}

	sockaddr_in6* v6() {
		return reinterpret_cast<sockaddr_in6*>(&raw);
	}

	const sockaddr_in6* v6() const {
		return reinterpret_cast<const sockaddr_in6*>(&raw);
	}

	template <typename T> int send(const T buf[], size_t size) const {
		const auto sock = ipv == NP_IPv6 ? sock6 : sock4;
		return sendto(sock, reinterpret_cast<const char*>(buf), static_cast<int>(size), 0,
			reinterpret_cast<const sockaddr*>(&raw), sizeof(raw));
	}

	int gtfo(uint8_t error) const {
		int status = 0;
		static uint8_t buf[NUTPUNCH_HEADER_SIZE + 1] = "GTFO";
		buf[NUTPUNCH_HEADER_SIZE] = error;
		for (int i = 0; i < 5; i++)
			status |= send(buf, sizeof(buf));
		return status;
	}

	void load(const uint8_t* ptr) {
		return load(reinterpret_cast<const char*>(ptr));
	}

	void load(const char* ptr) {
		ipv = *ptr++;
		if (NP_IPv4 == ipv) {
			v4()->sin_family = AF_INET;
			std::memcpy(&v4()->sin_addr, ptr, 4), ptr += 16;
			std::memcpy(&v4()->sin_port, ptr, 2), ptr += 2;
		} else {
			v6()->sin6_family = AF_INET6;
			std::memcpy(&v6()->sin6_addr, ptr, 16), ptr += 16;
			std::memcpy(&v6()->sin6_port, ptr, 2), ptr += 2;
		}
	}

	void dump(uint8_t* ptr) const {
		return dump(reinterpret_cast<char*>(ptr));
	}

	void dump(char* ptr) const {
		*ptr++ = *(char*)&ipv;
		if (NP_IPv4 == ipv) {
			std::memcpy(ptr, &v4()->sin_addr, 4), ptr += 16;
			std::memcpy(ptr, &v4()->sin_port, 2), ptr += 2;
		} else {
			std::memcpy(ptr, &v6()->sin6_addr, 16), ptr += 16;
			std::memcpy(ptr, &v6()->sin6_port, 2), ptr += 2;
		}
	}
};

struct Player {
	Addr addr;
	std::uint32_t countdown;
	Metadata metadata;

	Player() : countdown(0) {}

	bool dead() const {
		static constexpr const char zero[sizeof(Addr)] = {0};
		return countdown < 1 || !std::memcmp(&addr, zero, sizeof(addr));
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
			tick(i);
			if (!players[i].dead())
				beat(i);
		}
	}

	bool dead() const {
		return !gamers();
	}

	int gamers() const {
		int count = 0;
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			count += !players[i].dead();
		return count;
	}

	void accept(const int idx, const char* meta) {
		auto& player = players[idx];
		player.countdown = keepAliveBeats;
		player.metadata.load(meta), meta += sizeof(Metadata);
		if (idx == master())
			metadata.load(meta);
	}

	void tick(const int playerIdx) {
		auto& plr = players[playerIdx];
		if (plr.dead())
			return;
		plr.countdown--;
		if (!plr.dead())
			return;
		NP_Warn("Peer %d timed out in lobby '%s'", playerIdx + 1, fmtId());
		plr.reset();
	}

	void beat(const int playerIdx) {
		static uint8_t buf[NUTPUNCH_RESPONSE_SIZE] = "BEAT";
		uint8_t* ptr = buf + NUTPUNCH_HEADER_SIZE;
		*ptr++ = static_cast<NP_ResponseFlagsStorage>(playerIdx == master()) * NP_R_Master;

		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			auto& player = players[i];

			player.addr.dump(ptr);
			if (playerIdx == i) // local peer gets a zeroed IP and a non-zero port
				NutPunch_Memset(ptr + 1, 0, 16);
			ptr += NUTPUNCH_ADDRESS_SIZE;

			std::memset(ptr, 0, sizeof(Metadata));
			std::memcpy(ptr, &player.metadata, sizeof(Metadata));
			ptr += sizeof(Metadata);
		}
		std::memcpy(ptr, &metadata, sizeof(Metadata));

		auto& player = players[playerIdx];
		if (player.addr.send(buf, sizeof(buf)) < 0) {
			NP_Warn("Player %d aborted connection", playerIdx + 1);
			player.reset();
		}
	}

	int master() {
		if (NUTPUNCH_MAX_PLAYERS != masterIdx && !players[masterIdx].dead())
			return masterIdx;
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!players[i].dead())
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
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&argp), sizeof(argp)))
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

	sockaddr_storage addr = {0};
	if (ipv == NP_IPv6) {
		reinterpret_cast<sockaddr_in6*>(&addr)->sin6_family = AF_INET6;
		reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port = htons(NUTPUNCH_SERVER_PORT);
		reinterpret_cast<sockaddr_in6*>(&addr)->sin6_addr = in6addr_any;
	} else {
		reinterpret_cast<sockaddr_in*>(&addr)->sin_family = AF_INET;
		reinterpret_cast<sockaddr_in*>(&addr)->sin_port = htons(NUTPUNCH_SERVER_PORT);
	}

	if (!bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)))
		NP_Info("Bound IPv%s socket", ipv == NP_IPv6 ? "6" : "4");
	else if (ipv == NP_IPv6) { // IPv6 is optional and skipped with a warning
		NP_Warn("Failed to bind IPv6 socket (%d)", NP_SockError());
		sock = NUTPUNCH_INVALID_SOCKET;
	} else
		throw "Failed to bind IPv4 socket, and IPv6-only mode is unsupported";
}

static void sendLobbies(Addr addr, const NutPunch_Filter* filters) {
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
			const auto& filter = filters[f];
			for (int m = 0; m < NUTPUNCH_MAX_FIELDS; m++) {
				const auto& field = lobby.metadata.fields[m];
				if (field.dead() || !field.named(filter.name))
					continue;
				if (field.matches(filter))
					goto nextFilter;
			}
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

enum {
	RecvKeepGoing = 0,
	RecvDone = -1,
};

static int receive(NP_IPv ipv) {
	const auto sock = ipv == NP_IPv6 ? sock6 : sock4;
	if (sock == NUTPUNCH_INVALID_SOCKET)
		return RecvDone;
	Player* players = nullptr;

	char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	socklen_t addrSize = ipv == NP_IPv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	Addr addr(ipv);

	int rcv = recvfrom(sock, heartbeat, sizeof(heartbeat), 0, reinterpret_cast<sockaddr*>(&addr.raw), &addrSize);
	if (rcv < 0)
		switch (NP_SockError()) {
		case NP_ConnReset:
			return RecvKeepGoing;
		case NP_WouldBlock:
			return RecvDone;
		default:
			return NP_SockError();
		}

	const char* ptr = heartbeat + NUTPUNCH_HEADER_SIZE;
	if (!std::memcmp(heartbeat, "LIST", NUTPUNCH_HEADER_SIZE) && rcv == NUTPUNCH_HEADER_SIZE + sizeof(NP_Filters)) {
		sendLobbies(addr, reinterpret_cast<const NutPunch_Filter*>(ptr));
		return RecvKeepGoing;
	}
	if (std::memcmp(heartbeat, "JOIN", NUTPUNCH_HEADER_SIZE) || rcv != NUTPUNCH_HEARTBEAT_SIZE)
		return RecvKeepGoing; // most likely junk...

	static char id[NUTPUNCH_ID_MAX + 1] = {0};
	std::memcpy(id, ptr, NUTPUNCH_ID_MAX), ptr += NUTPUNCH_ID_MAX;

	auto flags = *reinterpret_cast<const NP_HeartbeatFlagsStorage*>(ptr);
	ptr += sizeof(flags);

	if (!flags) // wtf do you want??
		return RecvKeepGoing;

	if ((lobbies.count(id) && !(flags & NP_HB_Join)) || (!lobbies.count(id) && !(flags & NP_HB_Create)))
		goto exists;
	if (!lobbies.count(id) && lobbies.size() >= maxLobbies) {
		addr.gtfo(NPE_NoSuchLobby); // TODO: update bogus error code
		NP_Warn("Reached lobby limit");
		return RecvKeepGoing;
	}
	if (!lobbies.count(id)) {
		// Match against existing peers to prevent creating multiple lobbies with the same master.
		for (const auto& [lobbyId, lobby] : lobbies)
			for (const auto& player : lobby.players)
				if (!player.dead() && !std::memcmp(&player.addr, &addr, sizeof(addr)))
					return RecvKeepGoing; // fuck you...
		lobbies.insert({id, Lobby(id)});
		NP_Info("Created lobby '%s'", fmtLobbyId(id));
	}

	players = lobbies[id].players;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!players[i].dead() && !std::memcmp(&players[i].addr.raw, &addr.raw, sizeof(addr.raw))) {
			lobbies[id].accept(i, ptr);
			return RecvKeepGoing;
		}
	}

	if ((flags & NP_HB_Create) && lobbies[id].gamers() > 0)
		goto exists;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!players[i].dead())
			continue;

		players[i].addr = addr;
		lobbies[id].accept(i, ptr);

		const char* ipv_s = ipv == NP_IPv6 ? "IPv6" : "IPv4";
		NP_Info("Peer %d joined lobby '%s' (over %s)", i + 1, fmtLobbyId(id), ipv_s);
		return RecvKeepGoing;
	}

	NP_Info("Lobby '%s' is full!", fmtLobbyId(id));
	return RecvKeepGoing;

exists:
	addr.gtfo(NPE_LobbyExists);
	return RecvKeepGoing;
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

int main(int, char*[]) {
	cleanup __cleanup;

#ifdef NUTPUNCH_WINDOSE
	WSADATA __bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &__bitch);
#endif

	try {
		bindSock(NP_IPv6);
		bindSock(NP_IPv4);
	} catch (const char* msg) {
		NP_Info("CRITICAL: %s (code %d)", msg, NP_SockError());
		return EXIT_FAILURE;
	}

	int result;
	std::int64_t start = clock(), end, delta;
	const std::int64_t minDelta = 1000 / beatsPerSecond;

	NP_Info("Running!");
	for (;;) {
		if (sock4 == NUTPUNCH_INVALID_SOCKET && sock6 == NUTPUNCH_INVALID_SOCKET)
			return EXIT_FAILURE;

		static constexpr const NP_IPv ipvs[2] = {NP_IPv6, NP_IPv4};
		for (const auto ipv : ipvs) {
			do
				result = receive(ipv);
			while (RecvKeepGoing == result);
			if (result > 0) {
				NP_Warn("Failed to receive data (code %d)", result);
				(ipv == NP_IPv6 ? sock6 : sock4) = NUTPUNCH_INVALID_SOCKET;
			}
		}

		for (auto& [id, lobby] : lobbies)
			lobby.update();
		std::erase_if(lobbies, [](const auto& kv) {
			const auto& lobby = kv.second;
			bool dead = lobby.dead();
			if (dead)
				NP_Info("Deleted lobby '%s'", lobby.fmtId());
			return dead;
		});

		end = clock(), delta = ((end - start) * 1000) / CLOCKS_PER_SEC;
		if (delta < minDelta)
			SleepMs(minDelta - delta);
		start = clock();
	}

	return EXIT_SUCCESS;
}
