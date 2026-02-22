#pragma once
// ============================================================
// game_sv_coop.h
// Серверная игровая логика кооперативного режима.
// Phase 1: минимальный каркас + handshake state machine.
// ============================================================

#include "game_sv_single.h"  // inherits ALife support
#include "coop_net_types.h"
#include "coop_packet_ids.h"

// Состояние подключённого peer (один клиент) на сервере
struct CoopPeerEntry
{
    ClientID         client_id;
    CoopPlayerId     player_id;
    ECoopPeerState   state;
    u32              state_time_ms;  // время перехода в текущее состояние
    char             nickname[32];

    CoopPeerEntry()
        : client_id(ClientID()), player_id(COOP_INVALID_PLAYER_ID)
        , state(eCPS_Disconnected), state_time_ms(0)
    {
        nickname[0] = '\0';
    }
};

// Минимальное состояние кооп-сессии
struct CoopSessionState
{
    CoopSessionId    session_id;
    u16              protocol_version;
    u32              content_hash;
    char             level_name[128];
    u32              max_players;
    u32              current_players;
    bool             is_joinable;

    CoopSessionState()
        : session_id(COOP_INVALID_SESSION_ID)
        , protocol_version(COOP_PROTOCOL_VERSION)
        , content_hash(COOP_CONTENT_HASH)
        , max_players(COOP_MAX_PLAYERS)
        , current_players(0)
        , is_joinable(true)
    {
        level_name[0] = '\0';
    }
};

class game_sv_Coop : public game_sv_Single
{
    typedef game_sv_Single inherited;

    CoopSessionState             m_session;
    xr_vector<CoopPeerEntry>     m_peers;
    CoopPlayerId                 m_next_player_id;

    // ---- Internals ----
    CoopPeerEntry*  find_peer(ClientID id);
    CoopPlayerId    alloc_player_id();
    u32             current_time_ms() const;
    bool            check_timeout(const CoopPeerEntry& peer, u32 timeout_ms) const;

    // Отправка пакетов сервера
    void send_hello_ack(ClientID to, const CoopPeerEntry& peer);
    void send_join_accept(ClientID to, CoopPeerEntry& peer);
    void send_join_reject(ClientID to, ECoopRejectReason reason);
    void send_pong(ClientID to);

    // Обработчики входящих пакетов
    void handle_cl_hello(NET_Packet& P, ClientID sender, CoopPeerEntry& peer);
    void handle_cl_join_request(NET_Packet& P, ClientID sender, CoopPeerEntry& peer);
    void handle_cl_ping(NET_Packet& P, ClientID sender);

    // Проверка таймаутов всех peer'ов
    void tick_peer_timeouts();

public:
    game_sv_Coop();
    virtual ~game_sv_Coop();

    virtual LPCSTR type_name() const { return "coop"; }
    virtual void Create(shared_str& options);
    virtual void Update();

    // Lifecycle
    virtual void OnPlayerConnect(ClientID id_who);
    virtual void OnPlayerDisconnect(ClientID id_who, LPSTR Name, u16 GameID);

    // Called from xrServer::OnMessage case M_COOP_HANDSHAKE
    void OnCoopPacket(NET_Packet& P, ClientID sender);

    // Sends REJECT + transport disconnect. Called on protocol violation/timeout.
    void disconnect_peer(ClientID id, ECoopRejectReason reason, const char* why);

    // Pure virtual overrides (inherited from game_sv_Single already)
    // OnTouch, OnDetach, CanHaveFriendlyFire — all satisfied by game_sv_Single

    const CoopSessionState& session() const { return m_session; }
};
