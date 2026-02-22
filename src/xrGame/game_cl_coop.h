#pragma once
// ============================================================
// game_cl_coop.h
// Клиентская игровая логика кооперативного режима.
// Phase 1: минимальный каркас + handshake state machine.
// ============================================================

#include "game_cl_base.h"
#include "coop_net_types.h"
#include "coop_packet_ids.h"

class game_cl_Coop : public game_cl_GameState
{
    typedef game_cl_GameState inherited;

    ECoopClientState   m_state;
    u32                m_state_time_ms;

    CoopPlayerId       m_player_id;
    CoopSessionId      m_session_id;
    char               m_level_name[128];

    // Последний код отказа (для UI/лога)
    ECoopRejectReason  m_last_reject_reason;

    // Отправка пакетов
    void send_hello();
    void send_join_request();
    void send_ping();

    // Обработчики входящих пакетов
    void handle_sv_hello_ack(NET_Packet& P);
    void handle_sv_join_accept(NET_Packet& P);
    void handle_sv_join_reject(NET_Packet& P);
    void handle_sv_pong(NET_Packet& P);

    // Утилиты
    u32  current_time_ms() const;
    bool check_timeout(u32 timeout_ms) const;
    void set_state(ECoopClientState new_state);
    void on_handshake_failed(ECoopRejectReason reason, const char* details);

public:
    game_cl_Coop();
    virtual ~game_cl_Coop();

    virtual CUIGameCustom* createGameUI();

    // Инициирует connect к хосту по IP (вызывается из UI/меню)
    // TODO_COOP_UI: вызвать из кнопки "Join Coop (LAN)"
    void StartConnect(const char* ip, u16 port = 0);

    // Вызывается из Level tick — проверка таймаутов
    virtual void shedule_Update(u32 dt);

    // Обработчик входящих coop-пакетов от сервера
    void OnCoopMessage(NET_Packet& P);

    ECoopClientState  GetCoopState() const { return m_state; }
    CoopPlayerId      GetPlayerId()  const { return m_player_id; }

    const char*       GetRejectReasonString() const;
};
