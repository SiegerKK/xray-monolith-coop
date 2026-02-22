// ============================================================
// game_sv_coop.cpp
// Серверный кооп-режим (Phase 1 — handshake skeleton).
// ============================================================

#include "stdafx.h"
#include "game_sv_coop.h"
#include "xrServer.h"
#include "Level.h"
#include "../xrNetServer/NET_Server.h"

// ============================================================
// Вспомогательные утилиты
// ============================================================

static const char* peer_state_name(ECoopPeerState s)
{
    switch (s)
    {
    case eCPS_Disconnected:    return "Disconnected";
    case eCPS_Connected:       return "Connected";
    case eCPS_HelloReceived:   return "HelloReceived";
    case eCPS_JoinRequested:   return "JoinRequested";
    case eCPS_JoinAccepted:    return "JoinAccepted";
    case eCPS_InSession:       return "InSession";
    case eCPS_Disconnecting:   return "Disconnecting";
    default:                   return "Unknown";
    }
}

static const char* reject_reason_name(ECoopRejectReason r)
{
    switch (r)
    {
    case eCRR_VersionMismatch: return "PROTOCOL_MISMATCH";
    case eCRR_ContentMismatch: return "CONTENT_MISMATCH";
    case eCRR_ServerFull:      return "SERVER_FULL";
    case eCRR_SessionNotReady: return "SESSION_NOT_READY";
    case eCRR_InvalidRequest:  return "INVALID_REQUEST";
    case eCRR_InternalError:   return "INTERNAL_ERROR";
    case eCRR_Timeout:         return "TIMEOUT";
    default:                   return "UNKNOWN";
    }
}

// ============================================================
// Constructor / Destructor
// ============================================================

game_sv_Coop::game_sv_Coop()
    : m_next_player_id(1)
{
    m_type = eGameIDCoop;
    Msg("[COOP][SV] game_sv_Coop created");
}

game_sv_Coop::~game_sv_Coop()
{
    Msg("[COOP][SV] game_sv_Coop destroyed");
}

// ============================================================
// Create — старт сессии
// ============================================================

void game_sv_Coop::Create(shared_str& options)
{
    inherited::Create(options);

    // Инициализация состояния сессии
    m_session.session_id        = (CoopSessionId)Device.TimerAsync();  // временный уникальный ID
    m_session.protocol_version  = COOP_PROTOCOL_VERSION;
    m_session.content_hash      = COOP_CONTENT_HASH;
    m_session.max_players       = COOP_MAX_PLAYERS;
    m_session.current_players   = 0;
    m_session.is_joinable       = true;

    // level_name из options если есть
    if (strstr(*options, "/level/"))
    {
        const char* lvl = strstr(*options, "/level/") + 7;
        const char* end = strpbrk(lvl, "/ \t\0");
        size_t len = end ? (size_t)(end - lvl) : strlen(lvl);
        if (len >= sizeof(m_session.level_name))
            len = sizeof(m_session.level_name) - 1;
        strncpy_s(m_session.level_name, sizeof(m_session.level_name), lvl, len);
        m_session.level_name[len] = '\0';
    }

    switch_Phase(GAME_PHASE_INPROGRESS);

    Msg("[COOP][SV] Host started | session_id=0x%08X protocol=%u content_hash=0x%08X max_players=%u level=%s",
        m_session.session_id, m_session.protocol_version,
        m_session.content_hash, m_session.max_players,
        m_session.level_name[0] ? m_session.level_name : "<none>");
}

// ============================================================
// Update — серверный тик
// ============================================================

void game_sv_Coop::Update()
{
    inherited::Update();
    tick_peer_timeouts();
}

// ============================================================
// Player connect / disconnect
// ============================================================

void game_sv_Coop::OnPlayerConnect(ClientID id_who)
{
    Msg("[COOP][SV] Transport peer connected | client_id=0x%08X", id_who.value());

    // Создать запись peer в состоянии Connected
    CoopPeerEntry entry;
    entry.client_id      = id_who;
    entry.player_id      = COOP_INVALID_PLAYER_ID;
    entry.state          = eCPS_Connected;
    entry.state_time_ms  = current_time_ms();
    m_peers.push_back(entry);
}

void game_sv_Coop::OnPlayerDisconnect(ClientID id_who, LPSTR /*Name*/, u16 /*GameID*/)
{
    CoopPeerEntry* peer = find_peer(id_who);
    if (peer)
    {
        Msg("[COOP][SV] Peer disconnected | client_id=0x%08X player_id=%u state=%s",
            id_who.value(), peer->player_id, peer_state_name(peer->state));

        if (peer->state == eCPS_InSession || peer->state == eCPS_JoinAccepted)
        {
            if (m_session.current_players > 0)
                --m_session.current_players;
        }

        // Удалить из списка
        for (auto it = m_peers.begin(); it != m_peers.end(); ++it)
        {
            if (it->client_id == id_who)
            {
                m_peers.erase(it);
                break;
            }
        }
    }
    else
    {
        Msg("[COOP][SV] Unknown peer disconnected | client_id=0x%08X", id_who.value());
    }
}

// ============================================================
// OnCoopPacket — точка входа для входящих coop-пакетов.
// Вызывается из xrServer::OnMessage case M_COOP_HANDSHAKE.
// ============================================================

void game_sv_Coop::OnCoopPacket(NET_Packet& P, ClientID sender)
{
    // Читаем id coop-пакета из начала payload
    u8 packet_id = 0;
    P.r_u8(packet_id);

    CoopPeerEntry* peer = find_peer(sender);
    if (!peer)
    {
        Msg("[COOP][SV] WARN: packet 0x%02X from unknown peer 0x%08X — ignored",
            packet_id, sender.value());
        return;
    }

    switch ((ECoopPacketID)packet_id)
    {
    case COOP_CL_HELLO:
        handle_cl_hello(P, sender, *peer);
        break;

    case COOP_CL_JOIN_REQUEST:
        handle_cl_join_request(P, sender, *peer);
        break;

    case COOP_CL_PING:
        handle_cl_ping(P, sender);
        break;

    default:
        // Protocol violation: неожиданный packet ID
        Msg("[COOP][SV] Protocol violation: unexpected packet 0x%02X in state %s | client_id=0x%08X — disconnect",
            packet_id, peer_state_name(peer->state), sender.value());
        // TODO_COOP: инициировать disconnect
        break;
    }
}

// ============================================================
// Handlers
// ============================================================

void game_sv_Coop::handle_cl_hello(NET_Packet& P, ClientID sender, CoopPeerEntry& peer)
{
    // Ожидаем только в состоянии Connected
    if (peer.state != eCPS_Connected)
    {
        Msg("[COOP][SV] Protocol violation: CL_HELLO in state %s | client_id=0x%08X — disconnect",
            peer_state_name(peer.state), sender.value());
        // TODO_COOP: disconnect
        return;
    }

    // Разбираем пакет
    u16 cl_protocol  = 0;
    u32 cl_content   = 0;
    char cl_nick[32] = {};

    P.r_u16(cl_protocol);
    P.r_u32(cl_content);
    P.r_stringZ(cl_nick);

    Msg("[COOP][SV] CL_HELLO received | client_id=0x%08X protocol=%u content_hash=0x%08X nick='%s'",
        sender.value(), cl_protocol, cl_content, cl_nick);

    // Проверяем версию протокола
    if (cl_protocol != COOP_PROTOCOL_VERSION)
    {
        Msg("[COOP][SV] Rejecting: protocol mismatch (got %u, expected %u)", cl_protocol, COOP_PROTOCOL_VERSION);
        send_join_reject(sender, eCRR_VersionMismatch);
        return;
    }

    // Проверяем content hash
    if (cl_content != COOP_CONTENT_HASH)
    {
        Msg("[COOP][SV] Rejecting: content mismatch (got 0x%08X, expected 0x%08X)", cl_content, COOP_CONTENT_HASH);
        send_join_reject(sender, eCRR_ContentMismatch);
        return;
    }

    // Проверяем вместимость сессии
    if (m_session.current_players >= m_session.max_players)
    {
        Msg("[COOP][SV] Rejecting: server full (%u/%u)", m_session.current_players, m_session.max_players);
        send_join_reject(sender, eCRR_ServerFull);
        return;
    }

    strncpy_s(peer.nickname, sizeof(peer.nickname), cl_nick, _TRUNCATE);
    peer.state         = eCPS_HelloReceived;
    peer.state_time_ms = current_time_ms();

    send_hello_ack(sender, peer);
}

void game_sv_Coop::handle_cl_join_request(NET_Packet& P, ClientID sender, CoopPeerEntry& peer)
{
    // Ожидаем только после HelloReceived
    if (peer.state != eCPS_HelloReceived)
    {
        Msg("[COOP][SV] Protocol violation: CL_JOIN_REQUEST in state %s | client_id=0x%08X — disconnect",
            peer_state_name(peer.state), sender.value());
        // TODO_COOP: disconnect
        return;
    }

    Msg("[COOP][SV] CL_JOIN_REQUEST received | client_id=0x%08X nick='%s'",
        sender.value(), peer.nickname);

    peer.state         = eCPS_JoinRequested;
    peer.state_time_ms = current_time_ms();

    // Принять — выдать player_id
    CoopPlayerId pid = alloc_player_id();
    if (pid == COOP_INVALID_PLAYER_ID)
    {
        Msg("[COOP][SV] Cannot allocate player_id — internal error");
        send_join_reject(sender, eCRR_InternalError);
        return;
    }

    peer.player_id     = pid;
    peer.state         = eCPS_JoinAccepted;
    peer.state_time_ms = current_time_ms();
    ++m_session.current_players;

    send_join_accept(sender, peer);
}

void game_sv_Coop::handle_cl_ping(NET_Packet& /*P*/, ClientID sender)
{
    send_pong(sender);
}

// ============================================================
// Send helpers
// ============================================================

void game_sv_Coop::send_hello_ack(ClientID to, const CoopPeerEntry& peer)
{
    NET_Packet P;
    P.w_begin(M_COOP_HANDSHAKE);
    P.w_u8(COOP_SV_HELLO_ACK);
    P.w_u16(m_session.protocol_version);
    P.w_u32(m_session.session_id);
    P.w_stringZ("xray-monolith-coop");  // server_name stub

    // TODO_COOP: заменить на реальный SendTo через CoopTransport
    if (m_server)
        m_server->SendTo(to, P);

    Msg("[COOP][SV] SV_HELLO_ACK sent | client_id=0x%08X", to.value());
}

void game_sv_Coop::send_join_accept(ClientID to, CoopPeerEntry& peer)
{
    NET_Packet P;
    P.w_begin(M_COOP_HANDSHAKE);
    P.w_u8(COOP_SV_JOIN_ACCEPT);
    P.w_u16(peer.player_id);
    P.w_u32(m_session.session_id);
    P.w_stringZ(m_session.level_name);
    P.w_u32(m_session.content_hash);

    if (m_server)
        m_server->SendTo(to, P);

    Msg("[COOP][SV] SV_JOIN_ACCEPT sent | client_id=0x%08X player_id=%u session=0x%08X level='%s'",
        to.value(), peer.player_id, m_session.session_id, m_session.level_name);

    peer.state         = eCPS_InSession;
    peer.state_time_ms = current_time_ms();
}

void game_sv_Coop::send_join_reject(ClientID to, ECoopRejectReason reason)
{
    NET_Packet P;
    P.w_begin(M_COOP_HANDSHAKE);
    P.w_u8(COOP_SV_JOIN_REJECT);
    P.w_u8((u8)reason);
    P.w_stringZ(reject_reason_name(reason));

    if (m_server)
        m_server->SendTo(to, P);

    Msg("[COOP][SV] SV_JOIN_REJECT sent | reason=%s", reject_reason_name(reason));
}

void game_sv_Coop::send_pong(ClientID to)
{
    NET_Packet P;
    P.w_begin(M_COOP_HANDSHAKE);
    P.w_u8(COOP_SV_PONG);
    P.w_u32(current_time_ms());

    if (m_server)
        m_server->SendTo(to, P);
}

// ============================================================
// Utilities
// ============================================================

CoopPeerEntry* game_sv_Coop::find_peer(ClientID id)
{
    for (auto& p : m_peers)
        if (p.client_id == id)
            return &p;
    return nullptr;
}

CoopPlayerId game_sv_Coop::alloc_player_id()
{
    if (m_next_player_id >= COOP_MAX_PLAYER_ID)
        return COOP_INVALID_PLAYER_ID;
    return m_next_player_id++;
}

u32 game_sv_Coop::current_time_ms() const
{
    return Device.TimerAsync();
}

bool game_sv_Coop::check_timeout(const CoopPeerEntry& peer, u32 timeout_ms) const
{
    return (current_time_ms() - peer.state_time_ms) > timeout_ms;
}

void game_sv_Coop::tick_peer_timeouts()
{
    for (auto& peer : m_peers)
    {
        if (peer.state == eCPS_InSession || peer.state == eCPS_Disconnected)
            continue;

        u32 timeout = COOP_CONNECT_TIMEOUT_MS;
        const char* step = "waiting_hello";

        switch (peer.state)
        {
        case eCPS_Connected:
            timeout = COOP_HELLO_TIMEOUT_MS;
            step    = "waiting_hello";
            break;
        case eCPS_HelloReceived:
            timeout = COOP_JOIN_ACCEPT_TIMEOUT_MS;
            step    = "waiting_join_request";
            break;
        default:
            break;
        }

        if (check_timeout(peer, timeout))
        {
            Msg("[COOP][SV] Timeout for peer 0x%08X at step '%s' | state=%s",
                peer.client_id.value(), step, peer_state_name(peer.state));
            send_join_reject(peer.client_id, eCRR_Timeout);
            // TODO_COOP: инициировать disconnect через transport
            peer.state = eCPS_Disconnecting;
        }
    }
}
