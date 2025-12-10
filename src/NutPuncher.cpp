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

constexpr const int beats_per_second = 60, keep_alive_seconds = 3,
		    keep_alive_beats = keep_alive_seconds * beats_per_second, max_lobbies = 512;

struct Lobby;
static NP_Socket sock = NUTPUNCH_INVALID_SOCKET;
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

		int arg_len = static_cast<int>(std::strlen(name));
		if (arg_len > NUTPUNCH_FIELD_NAME_MAX)
			arg_len = NUTPUNCH_FIELD_NAME_MAX;

		int our_len = NUTPUNCH_FIELD_NAME_MAX;
		for (int i = 1; i < NUTPUNCH_FIELD_NAME_MAX; i++)
			if (!this->name[i]) {
				our_len = i;
				break;
			}

		return our_len == arg_len && !std::memcmp(this->name, name, our_len);
	}

	bool matches(const NutPunch_Filter& filter) const {
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

		int name_size = static_cast<int>(std::strlen(field.name));
		if (name_size > NUTPUNCH_FIELD_NAME_MAX)
			name_size = NUTPUNCH_FIELD_NAME_MAX;

		int data_size = field.size;
		if (data_size > NUTPUNCH_FIELD_DATA_MAX)
			data_size = NUTPUNCH_FIELD_DATA_MAX;

		auto& target = fields[idx];
		std::memcpy(target.name, field.name, name_size);
		std::memcpy(target.data, field.data, data_size);
		target.size = data_size;
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
		std::memset(this, 0, sizeof(*this));
	}

	Addr(sockaddr_in v4) {
		std::memcpy(this, &v4, sizeof(v4));
	}

	sockaddr_in* v4() {
		return reinterpret_cast<sockaddr_in*>(this);
	}

	const sockaddr_in* v4() const {
		return reinterpret_cast<const sockaddr_in*>(this);
	}

	template <typename T> int send(const T buf[], size_t size) const {
		return sendto(sock, reinterpret_cast<const char*>(buf), static_cast<int>(size), 0,
			reinterpret_cast<const sockaddr*>(this), sizeof(*this));
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
		std::memcpy(ptr, &v4()->sin_addr, 4), ptr += 4;
		std::memcpy(ptr, &v4()->sin_port, 2), ptr += 2;
	}

	static Addr load(const uint8_t* ptr) {
		return load(reinterpret_cast<const char*>(ptr));
	}

	static Addr load(const char* ptr) {
		sockaddr_in addr = {0};
		addr.sin_family = AF_INET;
		std::memcpy(&addr.sin_addr, ptr, 4), ptr += 4;
		std::memcpy(&addr.sin_port, ptr, 2), ptr += 2;
		return Addr(addr);
	}
};

struct Player {
	Addr public_addr, internal_addr;
	std::uint32_t countdown;
	Metadata metadata;

	Player() : countdown(0) {}

	bool dead() const {
		static constexpr const char zero[sizeof(Addr)] = {0};
		return countdown < 1 || !std::memcmp(&public_addr, zero, sizeof(public_addr));
	}

	void reset() {
		std::memset(&public_addr, 0, sizeof(public_addr));
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

	int special(uint8_t idx) const {
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
		player.countdown = keep_alive_beats;
		player.metadata.load(meta), meta += sizeof(Metadata);
		if (idx == master()) {
			capacity = (flags & 0xF0) >> 4;
			metadata.load(meta);
		}
	}

	void tick(const int player_idx) {
		auto& plr = players[player_idx];
		if (plr.dead())
			return;
		plr.countdown--;
		if (!plr.dead())
			return;
		NP_Warn("Peer %d timed out in lobby '%s'", player_idx + 1, fmt_id());
		plr.reset();
	}

	void beat(const int player_idx) {
		static uint8_t buf[NUTPUNCH_RESPONSE_SIZE] = "BEAT";
		uint8_t* ptr = buf + NUTPUNCH_HEADER_SIZE;

		*ptr++ = static_cast<uint8_t>(player_idx);

		*ptr = static_cast<NP_ResponseFlagsStorage>(player_idx == master()) * NP_R_Master;
		*ptr++ |= (capacity & 0xF) << 4;

		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			auto& player = players[i];

			player.public_addr.dump(ptr), ptr += NUTPUNCH_ADDRESS_SIZE;
			player.internal_addr.dump(ptr), ptr += NUTPUNCH_ADDRESS_SIZE;
			std::memset(ptr, 0, sizeof(Metadata));
			std::memcpy(ptr, &player.metadata, sizeof(Metadata));
			ptr += sizeof(Metadata);
		}
		std::memcpy(ptr, &metadata, sizeof(Metadata));

		auto& player = players[player_idx];
		if (player.public_addr.send(buf, sizeof(buf)) < 0) {
			NP_Warn("Player %d aborted connection", player_idx + 1);
			player.reset();
		}
	}

	int master() {
		if (NUTPUNCH_MAX_PLAYERS != master_idx && !players[master_idx].dead())
			return master_idx;
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!players[i].dead())
				return (master_idx = i);
		return (master_idx = NUTPUNCH_MAX_PLAYERS);
	}

private:
	int master_idx = NUTPUNCH_MAX_PLAYERS;
};

static void bind_sock() {
	Addr addr;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
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

	addr.v4()->sin_family = AF_INET;
	addr.v4()->sin_port = htons(NUTPUNCH_SERVER_PORT);
	addr.v4()->sin_addr.s_addr = htonl(INADDR_ANY);

	if (!bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) {
		NP_Info("Bound to port %d", NUTPUNCH_SERVER_PORT);
	} else {
		NP_Warn("Failed to bind a socket. IPv6-only mode is unsupported (%d)", NP_SockError());
	sockfail:
		NP_NukeSocket(&sock);
	}
}

static void send_lobbies(Addr addr, const NutPunch_Filter* filters) {
	static uint8_t buf[NUTPUNCH_HEADER_SIZE + NP_LIST_LEN] = "LIST";
	uint8_t* ptr = buf + NUTPUNCH_HEADER_SIZE;
	size_t filter_count = 0;

	for (; filter_count < NUTPUNCH_SEARCH_FILTERS_MAX; filter_count++) {
		static constexpr const NutPunch_Filter nully = {0};
		if (!std::memcmp(&filters[filter_count], &nully, sizeof(*filters)))
			break;
	}
	if (!filter_count)
		return;

	std::memset(ptr, 0, (size_t)NP_LIST_LEN);
	for (const auto& [id, lobby] : lobbies) {
		for (int f = 0; f < filter_count; f++) {
			const auto& filter = filters[f];
			if (filter.field.alwayszero != 0) {
				int diff = static_cast<int>(filter.special.value);
				diff -= lobby.special(filter.special.index);
				if (match_field_value(diff, filter.comparison))
					goto next_filter;
				goto next_lobby;
			}
			for (int m = 0; m < NUTPUNCH_MAX_FIELDS; m++) {
				const auto& field = lobby.metadata.fields[m];
				if (field.dead() || !field.named(filter.field.name))
					continue;
				if (field.matches(filter))
					goto next_filter;
			}
			goto next_lobby; // no field matched the filter
		next_filter:
			continue;
		}

		// All filters matched.
		*ptr++ = lobby.gamers(), *ptr++ = lobby.capacity;
		std::memset(ptr, 0, NUTPUNCH_ID_MAX);
		std::memcpy(ptr, id.data(), std::strlen(lobby.fmt_id()));
		ptr += NUTPUNCH_ID_MAX;

	next_lobby:
		continue;
	}

	for (int i = 0; i < 5; i++)
		addr.send(buf, sizeof(buf));
}

constexpr const int RecvKeepGoing = 0, RecvDone = -1;

static int receive() {
	if (sock == NUTPUNCH_INVALID_SOCKET)
		return RecvDone;
	Player* players = nullptr;

	int attempts = 0;
	char heartbeat[NUTPUNCH_HEARTBEAT_SIZE] = {0};
	socklen_t addr_size = sizeof(sockaddr_in);
	Addr addr;

	int rcv = recvfrom(sock, heartbeat, sizeof(heartbeat), 0, reinterpret_cast<sockaddr*>(&addr), &addr_size);
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

	Addr internal_addr = Addr::load(ptr);
	ptr += NUTPUNCH_ADDRESS_SIZE;

	auto flags = *reinterpret_cast<const NP_HeartbeatFlagsStorage*>(ptr);
	ptr += sizeof(flags);

	if (!flags) { // wtf do you want??
		addr.gtfo(NPE_Sybau);
		return RecvKeepGoing;
	}

	if (lobbies.count(id) && !(flags & NP_HB_Join))
		goto exists;
	if (!lobbies.count(id) && !(flags & NP_HB_Create)) {
		addr.gtfo(NPE_NoSuchLobby);
		return RecvKeepGoing;
	}

	if (!lobbies.count(id) && lobbies.size() >= max_lobbies) {
		addr.gtfo(NPE_NoSuchLobby); // TODO: update bogus error code
		NP_Warn("Reached lobby limit");
		return RecvKeepGoing;
	}
	if (!lobbies.count(id)) {
		// Match against existing peers to prevent creating multiple lobbies with the same master.
		for (const auto& [lobbyId, lobby] : lobbies)
			for (const auto& player : lobby.players)
				if (!player.dead() && !std::memcmp(&player.public_addr, &addr, sizeof(addr)))
					return RecvKeepGoing; // fuck you...
		lobbies.insert({id, Lobby(id)});
		NP_Info("Created lobby '%s'", fmt_lobby_id(id));
	}

	players = lobbies[id].players;
	for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
		if (!players[i].dead() && !std::memcmp(&players[i].public_addr, &addr, sizeof(addr))) {
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

		NP_Info("Peer %d joined lobby '%s'", i + 1, fmt_lobby_id(id));
		players[i].public_addr = addr;
		lobbies[id].accept(i, flags, ptr);

		return RecvKeepGoing;
	}

	addr.gtfo(NPE_LobbyFull);
	return RecvKeepGoing;

exists:
	addr.gtfo(NPE_LobbyExists);
	return RecvKeepGoing;
}

struct cleanup {
	cleanup() = default;
	~cleanup() {
		NP_NukeSocket(&sock);
#ifdef NUTPUNCH_WINDOSE
		WSACleanup();
#endif
	}
};

int main(int, char*[]) {
	cleanup _cleanup;

#ifdef NUTPUNCH_WINDOSE
	WSADATA _bitch = {0};
	WSAStartup(MAKEWORD(2, 2), &_bitch);
#endif
	bind_sock();

	std::int64_t start = clock(), end, delta;
	const std::int64_t min_delta = 1000 / beats_per_second;

	NP_Info("Running!");
	for (;;) {
		if (sock == NUTPUNCH_INVALID_SOCKET) {
			NP_Warn("SOCKET DIED!!!");
			return EXIT_FAILURE;
		}

		int result;
		do
			result = receive();
		while (result == RecvKeepGoing);

		if (result > 0) {
			NP_Warn("Failed to receive data (code %d)", result);
			sock = NUTPUNCH_INVALID_SOCKET;
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
		if (delta < min_delta)
			NP_SleepMs(min_delta - delta);
		start = clock();
	}

	return EXIT_SUCCESS;
}
