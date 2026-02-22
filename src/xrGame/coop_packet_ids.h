#pragma once
// ============================================================
// coop_packet_ids.h
// Идентификаторы пакетов кооп-протокола (handshake + future).
//
// Диапазон: 0xC0..0xDF зарезервирован для коопа.
// Не пересекается с существующими M_* сообщениями в xrMessages.h
// (те используют малые целые 0..~60).
// ============================================================

enum ECoopPacketID : u8
{
    // ---- Клиент -> Сервер ----
    COOP_CL_HELLO             = 0xC1,   // начало handshake
    COOP_CL_JOIN_REQUEST      = 0xC2,   // запрос входа в сессию
    COOP_CL_PING              = 0xC3,   // диагностический ping

    // ---- Сервер -> Клиент ----
    COOP_SV_HELLO_ACK         = 0xD1,   // подтверждение hello
    COOP_SV_JOIN_ACCEPT       = 0xD2,   // вход в сессию разрешён
    COOP_SV_JOIN_REJECT       = 0xD3,   // вход отказан (с reason code)
    COOP_SV_PONG              = 0xD4,   // ответ на ping

    COOP_PACKET_INVALID       = 0x00,
};
