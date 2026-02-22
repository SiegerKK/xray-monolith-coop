#pragma once
// ============================================================
// coop_net_types.h
// Базовые сетевые типы кооп-режима.
// Все coop-подсистемы используют эти типы — не адреса памяти.
// ============================================================

#include "../xrCore/xrCore.h"

// Уникальный ID игрока в кооп-сессии (0 = invalid)
typedef u16 CoopPlayerId;
static const CoopPlayerId COOP_INVALID_PLAYER_ID = 0;

// ID кооп-сессии
typedef u32 CoopSessionId;
static const CoopSessionId COOP_INVALID_SESSION_ID = 0;

// Ревизия мирового состояния (монотонно возрастает)
typedef u32 CoopWorldRevision;

// Версия протокола коопа
// Увеличивать при любом изменении формата handshake-пакетов
static const u16 COOP_PROTOCOL_VERSION = 1;

// Сигнатура билда/контента (временный hardcoded marker для Phase 1)
// TODO_COOP_DEFINE_CONTENT_HASH_POLICY: заменить на реальный хэш модсета
static const u32 COOP_CONTENT_HASH = 0x00000001u;

// Состояния peer на сервере.
// Типичный переход: Disconnected → Connected → HelloReceived → JoinRequested → JoinAccepted → InSession
enum ECoopPeerState : u8
{
    eCPS_Disconnected    = 0,
    eCPS_Connected,       // транспортное соединение установлено
    eCPS_HelloReceived,   // получен CL_COOP_HELLO
    eCPS_JoinRequested,   // получен CL_COOP_JOIN_REQUEST
    eCPS_JoinAccepted,    // отправлен SV_COOP_JOIN_ACCEPT
    eCPS_InSession,       // игрок в сессии
    eCPS_Disconnecting,
};

// Состояния клиента.
// Типичный переход: Idle → Connecting → ConnectedTransport → HelloSent → JoinRequested → JoinAccepted → InSession
// При ошибке: любое состояние → Failed → (пользователь нажимает Connect) → Idle
enum ECoopClientState : u8
{
    eCCS_Idle             = 0,
    eCCS_Connecting,
    eCCS_ConnectedTransport,
    eCCS_HelloSent,
    eCCS_JoinRequested,
    eCCS_JoinAccepted,
    eCCS_InSession,
    eCCS_Failed,
    eCCS_Disconnected,
};

// Коды причин отказа JOIN_REJECT.
// VersionMismatch: несовпадение COOP_PROTOCOL_VERSION
// ContentMismatch: несовпадение COOP_CONTENT_HASH (разные моды/контент)
// ServerFull: достигнут лимит COOP_MAX_PLAYERS
// InvalidRequest: пакет пришёл вне допустимого состояния (protocol violation)
// InternalError: ошибка на сервере (нет ресурсов, etc.)
// Timeout: peer не завершил handshake вовремя
enum ECoopRejectReason : u8
{
    eCRR_None             = 0,
    eCRR_VersionMismatch,     // несовпадение COOP_PROTOCOL_VERSION
    eCRR_ContentMismatch,     // несовпадение COOP_CONTENT_HASH
    eCRR_ServerFull,
    eCRR_SessionNotReady,
    eCRR_InvalidRequest,
    eCRR_InternalError,
    eCRR_Timeout,
};

// Таймауты handshake в миллисекундах
static const u32 COOP_CONNECT_TIMEOUT_MS      = 8000;
static const u32 COOP_HELLO_TIMEOUT_MS        = 5000;
static const u32 COOP_JOIN_ACCEPT_TIMEOUT_MS  = 5000;

// Максимум игроков в Phase 1 (host + 1 client)
static const u32 COOP_MAX_PLAYERS = 2;

// Максимальное значение CoopPlayerId (0xFFFF зарезервирован как INVALID)
static const CoopPlayerId COOP_MAX_PLAYER_ID = 0xFFFEu;
