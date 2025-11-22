// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <https://unlicense.org>

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
static void SleepMs(int ms) {
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

constexpr const int beatsPerSecond = 60, keepAliveSeconds = 3, keepAliveBeats = keepAliveSeconds * beatsPerSecond,
		    maxLobbies = 512;

struct Lobby;
static NP_Socket sock4 = NUTPUNCH_INVALID_SOCKET, sock6 = NUTPUNCH_INVALID_SOCKET;
static std::map<std::string, Lobby> lobbies;

static const char* fmt_lobby_id(const char* id) {
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

static bool match_field_value(const int diff, const int flags) {
	const int eq = !diff;
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

	bool matches(const Lobby& lobby, const NutPunch_Filter& filter) const {
		const int diff = std::memcmp(data, filter.field.value, size);
		return match_field_value(diff, filter.comparison);
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
		const int idx = index_of(name);
		return NUTPUNCH_MAX_FIELDS == idx ? nullptr : &fields[idx];
	}

	void insert(const Field& field) {
		if (!field.size)
			return;

		const int idx = index_of(field.name);
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
	int index_of(const char* name) {
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
	Addr() {
		std::memset(&raw, 0, sizeof(raw));
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

	int ipv() const {
		return reinterpret_cast<const sockaddr*>(&raw)->sa_family == AF_INET6 ? NP_IPv6 : NP_IPv4;
	}

	template <typename T> int send(const T buf[], size_t size) const {
		const auto sock = ipv() == NP_IPv6 ? sock6 : sock4;
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

	void dump(uint8_t* ptr) const {
		return dump(reinterpret_cast<char*>(ptr));
	}

	void dump(char* ptr) const {
		*ptr++ = (char)ipv();
		if (NP_IPv6 == ipv()) {
			std::memcpy(ptr, &v6()->sin6_addr, 16), ptr += 16;
			std::memcpy(ptr, &v6()->sin6_port, 2), ptr += 2;
		} else {
			std::memcpy(ptr, &v4()->sin_addr, 4), ptr += 16;
			std::memcpy(ptr, &v4()->sin_port, 2), ptr += 2;
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
	uint8_t capacity;
	Player players[NUTPUNCH_MAX_PLAYERS];
	Metadata metadata;

	Lobby() : Lobby(nullptr) {}
	Lobby(const char* id) : capacity(NUTPUNCH_MAX_PLAYERS) {
		if (id == nullptr)
			std::memset(this->id, 0, sizeof(this->id));
		else
			std::memcpy(this->id, id, sizeof(this->id));
		std::memset(players, 0, sizeof(players));
	}

	const char* fmt_id() const {
		return fmt_lobby_id(id);
	}

	void update() {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			tick(i);
			if (!players[i].dead())
				beat(i);
		}
	}

	int special(NutPunch_SpecialField idx) const {
		switch (idx) {
		case NPSF_Capacity:
			return capacity;
		case NPSF_Players:
			return gamers();
		default:
			return 0;
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

	void accept(const int idx, const NP_HeartbeatFlagsStorage flags, const char* meta) {
		auto& player = players[idx];
		player.countdown = keepAliveBeats;
		player.metadata.load(meta), meta += sizeof(Metadata);
		if (idx == master()) {
			capacity = (flags & 0xF0) >> 4;
			metadata.load(meta);
		}
	}

	void tick(const int playerIdx) {
		auto& plr = players[playerIdx];
		if (plr.dead())
			return;
		plr.countdown--;
		if (!plr.dead())
			return;
		NP_Warn("Peer %d timed out in lobby '%s'", playerIdx + 1, fmt_id());
		plr.reset();
	}

	void beat(const int playerIdx) {
		static uint8_t buf[NUTPUNCH_RESPONSE_SIZE] = "BEAT";
		uint8_t* ptr = buf + NUTPUNCH_HEADER_SIZE;

		*ptr++ = static_cast<uint8_t>(playerIdx);

		*ptr = static_cast<NP_ResponseFlagsStorage>(playerIdx == master()) * NP_R_Master;
		*ptr++ |= (capacity & 0xF) << 4;

		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			auto& player = players[i];

			player.addr.dump(ptr), ptr += NUTPUNCH_ADDRESS_SIZE;
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

static void bindSock(const NP_IPv ipv) {
	sockaddr_storage addr = {0};
	auto& sock = ipv == NP_IPv6 ? sock6 : sock4;

	sock = socket(ipv == NP_IPv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == NUTPUNCH_INVALID_SOCKET) {
		NP_Warn("Failed to create the underlying UDP socket (%d)", NP_SockError());
		return;
	}

	if (!NP_MakeReuseAddr(sock)) {
		NP_Warn("Failed to set socket reuseaddr option (%d)", NP_SockError());
		goto sockfail;
	}

	if (!NP_MakeNonblocking(sock)) {
		NP_Warn("Failed to set socket to non-blocking mode (%d)", NP_SockError());
		goto sockfail;
	}

	if (ipv == NP_IPv6) {
		reinterpret_cast<sockaddr_in6*>(&addr)->sin6_family = AF_INET6;
		reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port = htons(NUTPUNCH_SERVER_PORT);
	} else {
		reinterpret_cast<sockaddr_in*>(&addr)->sin_family = AF_INET;
		reinterpret_cast<sockaddr_in*>(&addr)->sin_port = htons(NUTPUNCH_SERVER_PORT);
	}

	if (!bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) {
		NP_Info("Bound IPv%s socket", ipv == NP_IPv6 ? "6" : "4");
		return;
	} else if (ipv == NP_IPv6) { // IPv6 is optional and skipped with a warning
		NP_Warn("Failed to bind IPv6 socket (%d)", NP_SockError());
		goto sockfail;
	} else {
		NP_Warn("Failed to bind an IPv4 socket. IPv6-only mode is unsupported (%d)", NP_SockError());
		goto sockfail;
	}

sockfail:
	NP_NukeSocket(&sock);
}

static void send_lobbies(Addr addr, const NutPunch_Filter* filters) {
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
			if (filter.field.alwayszero != 0) {
				int diff = (int)(uint8_t)filter.special.value;
				diff -= lobby.special((NutPunch_SpecialField)filter.special.index);
				if (match_field_value(diff, filter.comparison))
					goto nextFilter;
				goto nextLobby;
			}
			for (int m = 0; m < NUTPUNCH_MAX_FIELDS; m++) {
				const auto& field = lobby.metadata.fields[m];
				if (field.dead() || !field.named(filter.field.name))
					continue;
				if (field.matches(lobby, filter))
					goto nextFilter;
			}
			goto nextLobby; // no field matched the filter
		nextFilter:
			continue;
		}

		// All filters matched.
		*ptr++ = lobby.gamers(), *ptr++ = lobby.capacity;
		std::memset(ptr, 0, NUTPUNCH_ID_MAX);
		std::memcpy(ptr, id.data(), std::strlen(lobby.fmt_id()));
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

	int attempts = 0;
	char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	socklen_t addrSize = ipv == NP_IPv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	Addr addr;

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
		send_lobbies(addr, reinterpret_cast<const NutPunch_Filter*>(ptr));
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
		NP_Info("Created lobby '%s'", fmt_lobby_id(id));
	}

	players = lobbies[id].players;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!players[i].dead() && !std::memcmp(&players[i].addr.raw, &addr.raw, sizeof(addr.raw))) {
			lobbies[id].accept(i, flags, ptr);
			return RecvKeepGoing;
		}
	}

	if ((flags & NP_HB_Create) && lobbies[id].gamers() > 0)
		goto exists;

	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!players[i].dead()) {
			attempts++;
			if (attempts >= lobbies[id].capacity)
				break;
			else
				continue;
		}

		players[i].addr = addr;
		lobbies[id].accept(i, flags, ptr);

		const char* ipv_s = ipv == NP_IPv6 ? "IPv6" : "IPv4";
		NP_Info("Peer %d joined lobby '%s' (over %s)", i + 1, fmt_lobby_id(id), ipv_s);
		return RecvKeepGoing;
	}

	NP_Info("Lobby '%s' is full!", fmt_lobby_id(id));
	return RecvKeepGoing;

exists:
	addr.gtfo(NPE_LobbyExists);
	return RecvKeepGoing;
}

struct cleanup {
	cleanup() = default;
	~cleanup() {
		NP_NukeSocket(&sock4), NP_NukeSocket(&sock6);
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
	bindSock(NP_IPv6), bindSock(NP_IPv4);

	int result;
	std::int64_t start = clock(), end, delta;
	const std::int64_t minDelta = 1000 / beatsPerSecond;

	NP_Info("Running!");
	for (;;) {
		if (sock4 == NUTPUNCH_INVALID_SOCKET && sock6 == NUTPUNCH_INVALID_SOCKET)
			return EXIT_FAILURE;

		static constexpr const NP_IPv ipvs[2] = {NP_IPv6, NP_IPv4};
		for (const auto ipv : ipvs) {
			while ((result = receive(ipv)) == RecvKeepGoing) {}
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
				NP_Info("Deleting lobby '%s'", lobby.fmt_id());
			return dead;
		});

		end = clock(), delta = ((end - start) * 1000) / CLOCKS_PER_SEC;
		if (delta < minDelta)
			SleepMs(minDelta - delta);
		start = clock();
	}

	return EXIT_SUCCESS;
}
