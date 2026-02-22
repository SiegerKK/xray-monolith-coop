// ============================================================
// game_cl_coop.cpp
// Клиентский кооп-режим (Phase 1 — handshake skeleton).
// ============================================================

#include "stdafx.h"
#include "game_cl_coop.h"
#include "Level.h"
#include "UIGameSP.h"      // временно: используем SP UI как заглушку
#include "clsid_game.h"

// ============================================================
// Static helpers
// ============================================================

static const char* client_state_name(ECoopClientState s)
{
    switch (s)
    {
    case eCCS_Idle:               return "Idle";
    case eCCS_Connecting:         return "Connecting";
    case eCCS_ConnectedTransport: return "ConnectedTransport";
    case eCCS_HelloSent:          return "HelloSent";
    case eCCS_JoinRequested:      return "JoinRequested";
    case eCCS_JoinAccepted:       return "JoinAccepted";
    case eCCS_InSession:          return "InSession";
    case eCCS_Failed:             return "Failed";
    case eCCS_Disconnected:       return "Disconnected";
    default:                      return "Unknown";
    }
}

static const char* reject_reason_str(ECoopRejectReason r)
{
    switch (r)
    {
    case eCRR_None:            return "None";
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

game_cl_Coop::game_cl_Coop()
    : m_state(eCCS_Idle)
    , m_state_time_ms(0)
    , m_player_id(COOP_INVALID_PLAYER_ID)
    , m_session_id(COOP_INVALID_SESSION_ID)
    , m_last_reject_reason(eCRR_None)
{
    m_level_name[0] = '\0';
    Msg("[COOP][CL] game_cl_Coop created");
}

game_cl_Coop::~game_cl_Coop()
{
    Msg("[COOP][CL] game_cl_Coop destroyed");
}

// ============================================================
// UI — временная заглушка: используем SP UI
// TODO_COOP_UI: создать CLSID_GAME_UI_COOP и CoopGameUI
// ============================================================

CUIGameCustom* game_cl_Coop::createGameUI()
{
    CLASS_ID clsid = CLSID_GAME_UI_SINGLE;
    CUIGameSP* pUIGame = smart_cast<CUIGameSP*>(NEW_INSTANCE(clsid));
    R_ASSERT(pUIGame);
    pUIGame->Load();
    pUIGame->SetClGame(this);
    pUIGame->Init(0);
    pUIGame->Init(1);
    pUIGame->Init(2);
    return pUIGame;
}

// ============================================================
// Init — вызывается сразу после создания game_cl_Coop.
// Transport-соединение уже установлено (CL получил M_SV_CONFIG_NEW_CLIENT).
// Отправляем HELLO чтобы запустить handshake.
// ============================================================

void game_cl_Coop::Init()
{
    inherited::Init();
    Msg("[COOP][CL] Init: transport connected, sending HELLO");
    set_state(eCCS_ConnectedTransport);
    send_hello();
}

// ============================================================
// Update (schedule tick) — проверка таймаутов
// ============================================================

void game_cl_Coop::shedule_Update(u32 dt)
{
    inherited::shedule_Update(dt);

    switch (m_state)
    {
    case eCCS_HelloSent:
        if (check_timeout(COOP_HELLO_TIMEOUT_MS))
        {
            Msg("[COOP][CL] Timeout: waiting_hello_ack");
            on_handshake_failed(eCRR_Timeout, "timeout waiting HELLO_ACK");
        }
        break;

    case eCCS_JoinRequested:
        if (check_timeout(COOP_JOIN_ACCEPT_TIMEOUT_MS))
        {
            Msg("[COOP][CL] Timeout: waiting_join_accept");
            on_handshake_failed(eCRR_Timeout, "timeout waiting JOIN_ACCEPT");
        }
        break;

    default:
        break;
    }
}

// ============================================================
// OnCoopMessage — входящие пакеты от сервера
// ============================================================

void game_cl_Coop::OnCoopMessage(NET_Packet& P)
{
    u8 packet_id = 0;
    P.r_u8(packet_id);

    switch ((ECoopPacketID)packet_id)
    {
    case COOP_SV_HELLO_ACK:
        handle_sv_hello_ack(P);
        break;

    case COOP_SV_JOIN_ACCEPT:
        handle_sv_join_accept(P);
        break;

    case COOP_SV_JOIN_REJECT:
        handle_sv_join_reject(P);
        break;

    case COOP_SV_PONG:
        handle_sv_pong(P);
        break;

    default:
        // Protocol violation: неожиданный packet от сервера
        Msg("[COOP][CL] Protocol violation: unexpected server packet 0x%02X in state %s",
            packet_id, client_state_name(m_state));
        on_handshake_failed(eCRR_InvalidRequest, "unexpected server packet");
        break;
    }
}

// ============================================================
// Handlers
// ============================================================

void game_cl_Coop::handle_sv_hello_ack(NET_Packet& P)
{
    if (m_state != eCCS_HelloSent)
    {
        Msg("[COOP][CL] Protocol violation: SV_HELLO_ACK in state %s", client_state_name(m_state));
        on_handshake_failed(eCRR_InvalidRequest, "HELLO_ACK in wrong state");
        return;
    }

    u16 sv_protocol  = 0;
    u32 sv_session   = 0;
    char sv_name[64] = {};

    P.r_u16(sv_protocol);
    P.r_u32(sv_session);
    P.r_stringZ(sv_name);

    Msg("[COOP][CL] SV_HELLO_ACK received | server='%s' protocol=%u session=0x%08X",
        sv_name, sv_protocol, sv_session);

    m_session_id = sv_session;
    set_state(eCCS_JoinRequested);
    send_join_request();
}

void game_cl_Coop::handle_sv_join_accept(NET_Packet& P)
{
    if (m_state != eCCS_JoinRequested)
    {
        Msg("[COOP][CL] Protocol violation: SV_JOIN_ACCEPT in state %s", client_state_name(m_state));
        on_handshake_failed(eCRR_InvalidRequest, "JOIN_ACCEPT in wrong state");
        return;
    }

    u16 player_id    = 0;
    u32 session_id   = 0;
    char level[128]  = {};
    u32 content_hash = 0;

    P.r_u16(player_id);
    P.r_u32(session_id);
    P.r_stringZ(level);
    P.r_u32(content_hash);

    m_player_id  = player_id;
    m_session_id = session_id;
    strncpy_s(m_level_name, sizeof(m_level_name), level, _TRUNCATE);

    Msg("[COOP][CL] SV_JOIN_ACCEPT received | player_id=%u session=0x%08X level='%s'",
        m_player_id, m_session_id, m_level_name);

    set_state(eCCS_JoinAccepted);
    set_state(eCCS_InSession);

    Msg("[COOP][CL] Handshake complete — in session | player_id=%u", m_player_id);
    // TODO_COOP_UI: уведомить UI об успешном подключении
}

void game_cl_Coop::handle_sv_join_reject(NET_Packet& P)
{
    u8 reason_code = 0;
    char reason_str[64] = {};

    P.r_u8(reason_code);
    P.r_stringZ(reason_str);

    ECoopRejectReason reason = (ECoopRejectReason)reason_code;
    Msg("[COOP][CL] SV_JOIN_REJECT received | reason=%s (%s)",
        reject_reason_str(reason), reason_str);

    on_handshake_failed(reason, reason_str);
}

void game_cl_Coop::handle_sv_pong(NET_Packet& P)
{
    u32 sv_time = 0;
    P.r_u32(sv_time);
    // TODO_COOP: вычислить RTT
    Msg("[COOP][CL] PONG | sv_time=%u", sv_time);
}

// ============================================================
// Send helpers
// ============================================================

void game_cl_Coop::send_hello()
{
    NET_Packet P;
    P.w_begin(M_COOP_HANDSHAKE);
    P.w_u8(COOP_CL_HELLO);
    P.w_u16(COOP_PROTOCOL_VERSION);
    P.w_u32(COOP_CONTENT_HASH);
    P.w_stringZ("player");  // TODO_COOP_UI: использовать реальный ник из настроек

    sv_EventSend(P);
    Msg("[COOP][CL] CL_HELLO sent | protocol=%u content_hash=0x%08X elapsed_ms=%u",
        COOP_PROTOCOL_VERSION, COOP_CONTENT_HASH,
        current_time_ms() - m_state_time_ms);

    set_state(eCCS_HelloSent);
}

void game_cl_Coop::send_join_request()
{
    NET_Packet P;
    P.w_begin(M_COOP_HANDSHAKE);
    P.w_u8(COOP_CL_JOIN_REQUEST);
    P.w_u32(m_session_id);

    sv_EventSend(P);
    Msg("[COOP][CL] CL_JOIN_REQUEST sent | session=0x%08X elapsed_ms=%u",
        m_session_id, current_time_ms() - m_state_time_ms);
}

void game_cl_Coop::send_ping()
{
    NET_Packet P;
    P.w_begin(M_COOP_HANDSHAKE);
    P.w_u8(COOP_CL_PING);
    P.w_u32(current_time_ms());

    sv_EventSend(P);
}

// ============================================================
// Utilities
// ============================================================

void game_cl_Coop::set_state(ECoopClientState new_state)
{
    Msg("[COOP][CL] State: %s -> %s | elapsed_ms=%u",
        client_state_name(m_state), client_state_name(new_state),
        current_time_ms() - m_state_time_ms);

    m_state          = new_state;
    m_state_time_ms  = current_time_ms();
}

void game_cl_Coop::on_handshake_failed(ECoopRejectReason reason, const char* details)
{
    m_last_reject_reason = reason;
    Msg("[COOP][CL] Handshake FAILED | reason=%s details='%s'",
        reject_reason_str(reason), details ? details : "");
    set_state(eCCS_Failed);
    // TODO_COOP_UI: показать сообщение об ошибке пользователю
}

u32 game_cl_Coop::current_time_ms() const
{
    return Device.TimerAsync();
}

bool game_cl_Coop::check_timeout(u32 timeout_ms) const
{
    return (current_time_ms() - m_state_time_ms) > timeout_ms;
}

const char* game_cl_Coop::GetRejectReasonString() const
{
    return reject_reason_str(m_last_reject_reason);
}
