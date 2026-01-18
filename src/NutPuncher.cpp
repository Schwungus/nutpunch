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

constexpr const int64_t BEATS_PER_SECOND = 60, KEEP_ALIVE_SECONDS = 5;
constexpr const int MAX_LOBBIES = 512;

struct Lobby;
static NP_Socket sock = NUTPUNCH_INVALID_SOCKET;
static std::map<std::string, Lobby> lobbies;

static const char* fmt_lobby_id(const char* id) {
	static char buf[sizeof(NutPunch_Id) + 1] = {0};
	for (int i = 0; i < sizeof(NutPunch_Id); i++) {
		const char c = id[i];
		const bool alpha = c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z',
			   numeric = c >= '0' && c <= '9';
		if (alpha || numeric || c == '-' || c == '_') {
			buf[i] = c;
		} else if (!c) {
			buf[i] = 0;
			return buf;
		} else {
			buf[i] = ' ';
		}
	}
	for (int i = sizeof(NutPunch_Id); i > 0; i--)
		if (buf[i - 1] != ' ' && buf[i - 1] != 0) {
			buf[i] = 0;
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
	} else if (flags & NPF_Eq) {
		result &= eq;
	} else { // junk
		return false;
	}
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

		return our_len == arg_len
		       && !std::memcmp(this->name, name, our_len);
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
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++)
			// first matching
			if (fields[i].named(name))
				return i;
		for (int i = 0; i < NUTPUNCH_MAX_FIELDS; i++)
			// or first uninitialized
			if (fields[i].dead())
				return i;
		return NUTPUNCH_MAX_FIELDS; // or bust
	}
};

struct Addr : NP_Addr {
	Addr() {
		std::memset(this, 0, sizeof(*this));
	}

	// NOTE: need both =='s because C++20 complains about ambiguity:

	bool operator==(const Addr& addr) const {
		return !std::memcmp(this, &addr, sizeof(addr));
	}

	bool operator==(const NP_Addr& addr) const {
		return !std::memcmp(this, &addr, sizeof(addr));
	}

	template <typename T> int send(const T buf[], size_t size) const {
		const auto* cbuf = reinterpret_cast<const char*>(buf);
		const auto* csock = reinterpret_cast<const sockaddr*>(this);
		const auto csize = static_cast<int>(size);
		return sendto(sock, cbuf, csize, 0, csock, sizeof(*this));
	}

	void gtfo(uint8_t error) const {
		static uint8_t buf[sizeof(NP_Header) + 1] = "GTFO";
		buf[sizeof(NP_Header)] = error;
		for (int i = 0; i < 5; i++)
			send(buf, sizeof(buf));
	}

	void dump(uint8_t* ptr) const {
		return dump(reinterpret_cast<char*>(ptr));
	}

	void dump(char* ptr) const {
		std::memcpy(ptr, &sin_addr, 4), ptr += 4;
		std::memcpy(ptr, &sin_port, 2), ptr += 2;
	}
};

struct Player {
	Addr addr;
	std::uint32_t countdown;

	Player() : countdown(0) {}

	bool dead() const {
		static constexpr const char zero[sizeof(Addr)] = {0};
		return countdown < 1 || !std::memcmp(&addr, zero, sizeof(addr));
	}

	void reset() {
		std::memset(&addr, 0, sizeof(addr));
		countdown = 0;
	}
};

struct Lobby {
	NutPunch_Id id;
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

	int index_of(const Addr& addr) const {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			if (!players[i].dead() && players[i].addr == addr)
				return i;
		return NUTPUNCH_MAX_PLAYERS;
	}

	bool has(const Addr& addr) const {
		return index_of(addr) != NUTPUNCH_MAX_PLAYERS;
	}

	int gamers() const {
		int count = 0;
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			count += !players[i].dead();
		return count;
	}

	void accept(const Addr addr, const NP_HeartbeatFlagsStorage flags,
		const char* meta) {
		int idx = index_of(addr);
		if (idx != NUTPUNCH_MAX_PLAYERS)
			goto accept;
		for (idx = 0; idx < NUTPUNCH_MAX_PLAYERS; idx++) {
			if (!players[idx].dead())
				continue;
			NP_Info("Peer %d joined lobby '%s'", idx + 1, fmt_id());
			goto accept;
		}

	accept:
		if (idx == NUTPUNCH_MAX_PLAYERS) {
			addr.gtfo(NPE_LobbyFull);
			return;
		}

		auto& player = players[idx];
		player.addr = addr;
		player.countdown = KEEP_ALIVE_SECONDS * BEATS_PER_SECOND;

		if (idx == master()) {
			capacity = (flags & 0xF0) >> 4;
			metadata.load(meta);
		}
	}

	void tick(const int idx) {
		auto& plr = players[idx];
		if (plr.dead())
			return;
		plr.countdown--;
		if (!plr.dead())
			return;
		NP_Warn("Peer %d timed out", idx + 1);
		plr.reset();
	}

	void beat(const int idx) {
		static uint8_t buf[sizeof(NP_Header) + sizeof(NP_Beating)]
			= "BEAT";
		uint8_t* ptr = buf + sizeof(NP_Header);

#define RF(f, v) ((f) * static_cast<NP_ResponseFlagsStorage>(v))
		*ptr++ = static_cast<uint8_t>(idx);
		*ptr = RF(NP_R_Master, idx == master());
		*ptr++ |= (capacity & 0xF) << 4;
#undef RF

		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++)
			players[i].addr.dump(ptr), ptr += NUTPUNCH_ADDRESS_SIZE;
		std::memcpy(ptr, &metadata, sizeof(Metadata));

		auto& player = players[idx];
		if (player.addr.send(buf, sizeof(buf)) <= 0) {
			NP_Warn("Player %d lost connection", idx + 1);
			player.reset();
		}
	}

	bool kill_bro(const NP_Addr addr) {
		for (int i = 0; i < NUTPUNCH_MAX_PLAYERS; i++) {
			if (players[i].dead() || players[i].addr != addr)
				continue;
			NP_Info("Player %d disconnected gracefully", i + 1);
			players[i].reset();
			return true;
		}
		return false;
	}

	bool match_against(
		const NutPunch_Filter* filters, size_t filter_count) const {
		for (int f = 0; f < filter_count; f++) {
			const auto& filter = filters[f];
			if (filter.field.alwayszero != 0) {
				int diff = static_cast<int>(
					filter.special.value);
				diff -= special(filter.special.index);
				if (match_field_value(diff, filter.comparison))
					goto next_filter;
				return false;
			}
			for (int m = 0; m < NUTPUNCH_MAX_FIELDS; m++) {
				const auto& field = metadata.fields[m];
				if (field.dead())
					continue;
				if (!field.named(filter.field.name))
					continue;
				if (field.matches(filter))
					goto next_filter;
			}
			return false; // no field matched the filter
		next_filter:
			continue;
		}
		// All filters matched.
		return true;
	}

	int master() {
		if (NUTPUNCH_MAX_PLAYERS != master_idx)
			if (!players[master_idx].dead())
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
		const int err = NP_SockError();
		NP_Warn("Failed to create the underlying UDP socket (%d)", err);
		return;
	}

	if (!NP_MakeReuseAddr(sock)) {
		const int err = NP_SockError();
		NP_Warn("Failed to set socket reuseaddr option (%d)", err);
		goto sockfail;
	}

	if (!NP_MakeNonblocking(sock)) {
		const int err = NP_SockError();
		NP_Warn("Failed to set socket to non-blocking mode (%d)", err);
		goto sockfail;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(NUTPUNCH_SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (!bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) {
		NP_Info("Bound to port %d", NUTPUNCH_SERVER_PORT);
	} else {
		NP_Warn("Failed to bind an IPv4 socket. IPv6-only mode is "
			"unsupported (%d)",
			NP_SockError());
	sockfail:
		NP_NukeSocket(&sock);
	}
}

static bool create_lobby(const char* id, const Addr& addr) {
	if (lobbies.size() >= MAX_LOBBIES) {
		addr.gtfo(NPE_NoSuchLobby); // TODO: update bogus error code
		NP_Warn("Reached lobby limit");
		return false;
	}

	// Match against existing peers to prevent creating multiple lobbies
	// with the same master.
	for (const auto& [lobby_id, lobby] : lobbies)
		for (const auto& player : lobby.players)
			if (!player.dead() && player.addr == addr)
				return true; // fuck you...

	lobbies.insert({id, Lobby(id)});
	NP_Info("Created lobby '%s'", fmt_lobby_id(id));
	return true;
}

static void send_lobbies(Addr addr, const NutPunch_Filter* filters) {
	static uint8_t buf[sizeof(NP_Header) + sizeof(NP_Listing)] = "LIST";
	uint8_t* ptr = buf + sizeof(NP_Header);
	size_t filter_count = 0;

	for (; filter_count < NUTPUNCH_MAX_SEARCH_FILTERS; filter_count++) {
		static constexpr const NutPunch_Filter nully = {0};
		if (!std::memcmp(
			    &filters[filter_count], &nully, sizeof(*filters)))
			break;
	}
	if (!filter_count)
		return;

	std::memset(ptr, 0, sizeof(NP_Listing));
	for (const auto& [id, lobby] : lobbies) {
		if (!lobby.match_against(filters, filter_count))
			continue;
		*ptr++ = lobby.gamers(), *ptr++ = lobby.capacity;
		std::memset(ptr, 0, sizeof(NutPunch_Id));
		std::memcpy(ptr, id.data(), std::strlen(lobby.fmt_id()));
		ptr += sizeof(NutPunch_Id);
	}

	for (int i = 0; i < 5; i++)
		addr.send(buf, sizeof(buf));
}

static void kill_bro(Addr addr) {
	for (auto& [id, lobby] : lobbies)
		if (lobby.kill_bro(addr))
			break;
}

constexpr const int RecvKeepGoing = 0, RecvDone = -1;

static int receive() {
	if (sock == NUTPUNCH_INVALID_SOCKET)
		return RecvDone;

	char heartbeat[NUTPUNCH_BUFFER_SIZE] = {0};

	Addr addr;
	auto* c_addr = reinterpret_cast<sockaddr*>(&addr);
	socklen_t addr_size = sizeof(addr);

	int rcv = recvfrom(
		sock, heartbeat, sizeof(heartbeat), 0, c_addr, &addr_size);
	if (rcv < 0)
		switch (NP_SockError()) {
		case NP_TooFat:
			NP_Trace("FAT PACKET");
		case NP_ConnReset:
			return RecvKeepGoing;
		case NP_WouldBlock:
			return RecvDone;
		default:
			NP_Trace("RECV ERROR %d", NP_SockError());
			return NP_SockError();
		}

	rcv -= sizeof(NP_Header);
	if (rcv < 0)
		return RecvKeepGoing; // junk...

	const char* ptr = heartbeat + sizeof(NP_Header);

	if (!std::memcmp(heartbeat, "LIST", sizeof(NP_Header)))
		if (rcv == sizeof(NP_Filters)) {
			const auto* filters
				= reinterpret_cast<const NutPunch_Filter*>(ptr);
			send_lobbies(addr, filters);
			return RecvKeepGoing;
		}

	if (!std::memcmp(heartbeat, "DISC", sizeof(NP_Header))) {
		kill_bro(addr);
		return RecvKeepGoing;
	}

	if (rcv != sizeof(NP_Heartbeat))
		return RecvKeepGoing; // most likely junk...
	if (std::memcmp(heartbeat, "JOIN", sizeof(NP_Header)))
		return RecvKeepGoing;

	static char id[sizeof(NutPunch_Id) + 1] = {0};
	std::memcpy(id, ptr, sizeof(NutPunch_Id));
	ptr += sizeof(NutPunch_Id);

	auto flags = *reinterpret_cast<const NP_HeartbeatFlagsStorage*>(ptr);
	ptr += sizeof(flags);

	if (lobbies.count(id)) {
		if (!(flags & NP_HB_Join)) {
			addr.gtfo(NPE_Sybau); // TODO: update bogus error code
			return RecvKeepGoing;
		} else if ((flags & NP_HB_Create) && !lobbies[id].has(addr)) {
			addr.gtfo(NPE_LobbyExists);
			return RecvKeepGoing;
		}
	} else if (!(flags & NP_HB_Create)) {
		addr.gtfo(NPE_NoSuchLobby);
		return RecvKeepGoing;
	} else if (!create_lobby(id, addr))
		return RecvKeepGoing;

	lobbies[id].accept(addr, flags, ptr);
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

	constexpr const int64_t MIN_DELTA = CLOCKS_PER_SEC / BEATS_PER_SECOND;
	int result;

	NP_Info("Running!");
	for (;;) {
		const int64_t start = clock();

		if (sock == NUTPUNCH_INVALID_SOCKET) {
			NP_Warn("SOCKET DIED!!!");
			return EXIT_FAILURE;
		}

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
			const bool dead = lobby.dead();
			if (dead)
				NP_Info("Deleting lobby '%s'", lobby.fmt_id());
			return dead;
		});

		const int64_t delta = clock() - start, diff = MIN_DELTA - delta;
		if (diff > 0)
			NP_SleepMs((diff * 1000) / CLOCKS_PER_SEC);
	}

	return EXIT_SUCCESS;
}
