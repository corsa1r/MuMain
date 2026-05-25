#include "stdafx.h"
#include "Heartbeat.h"

#include "Dotnet/Connection.h"
#include "Network/Server/WSclient.h"
#include "Scenes/SceneCommon.h"

#include <chrono>
#include <windows.h>

extern EGameScene SceneFlag;

namespace
{
    // ── Wire constants ───────────────────────────────────────────────────
    // Ping packet — 0xC1 0x06 0xE7 0x01 seq_hi seq_lo. Sub-op 0x01 is the
    // only one defined today, reserved for future expansion (e.g. tagged
    // pings carrying client telemetry).
    constexpr BYTE kPingOpcode     = 0xE7;
    constexpr BYTE kSubOpPing      = 0x01;
    constexpr int  kPingPacketSize = 6;

    // ── Module state ─────────────────────────────────────────────────────
    // Microsecond-resolution timestamps so sub-millisecond localhost RTTs
    // don't truncate to "0 ms" — GetTickCount()'s ~15ms quantum was lying
    // about every ping on a docker-loopback connection.
    using SteadyClock = std::chrono::steady_clock;
    using Microseconds = std::chrono::microseconds;

    int64_t  s_lastPingSentUs   = 0;
    int64_t  s_lastPongRecvUs   = 0;
    uint16_t s_nextSequence     = 1;
    uint16_t s_inflightSequence = 0;
    int64_t  s_inflightSentUs   = 0;
    int      s_latencyMs        = -1;

    int64_t NowUs()
    {
        return std::chrono::duration_cast<Microseconds>(
            SteadyClock::now().time_since_epoch()).count();
    }
}

namespace Heartbeat
{
    void Reset()
    {
        s_lastPingSentUs   = 0;
        s_lastPongRecvUs   = NowUs();
        s_nextSequence     = 1;
        s_inflightSequence = 0;
        s_inflightSentUs   = 0;
        s_latencyMs        = -1;
    }

    bool TickAndCheckTimeout()
    {
        if (SocketClient == nullptr || !SocketClient->IsConnected())
        {
            return false; // existing IsConnected-based detection handles this
        }

        // Heartbeat is a Game-Server-only feature. The Connect Server (which
        // the client talks to during the server-select phase) doesn't know
        // our 0xE7 opcode and would disconnect us with "unknown packet". Gate
        // sending and timing on actually being in the world.
        if (SceneFlag != MAIN_SCENE)
        {
            // Keep the silence timer fresh while we're not in-game, so the
            // timeout doesn't immediately fire on the first frame after we
            // land in MAIN_SCENE if Reset() hasn't run yet.
            s_lastPongRecvUs = NowUs();
            return false;
        }

        const int64_t now = NowUs();
        const int64_t kPingIntervalUs = static_cast<int64_t>(kPingIntervalMs) * 1000;
        const int64_t kPongTimeoutUs  = static_cast<int64_t>(kPongTimeoutMs) * 1000;

        // Lazy-init on first tick after Reset so the timeout doesn't trip
        // before the player has had a chance to receive their first pong.
        if (s_lastPongRecvUs == 0)
        {
            s_lastPongRecvUs = now;
        }

        // Send a ping if we're past the interval and no ping is in flight.
        const bool intervalElapsed = (now - s_lastPingSentUs) >= kPingIntervalUs;
        if (intervalElapsed)
        {
            const uint16_t seq = s_nextSequence++;
            if (s_nextSequence == 0) s_nextSequence = 1; // skip 0 (sentinel)

            BYTE packet[kPingPacketSize];
            packet[0] = 0xC1;
            packet[1] = kPingPacketSize;
            packet[2] = kPingOpcode;
            packet[3] = kSubOpPing;
            packet[4] = static_cast<BYTE>((seq >> 8) & 0xFF);
            packet[5] = static_cast<BYTE>(seq & 0xFF);
            SocketClient->Send(packet, kPingPacketSize);

            s_lastPingSentUs   = now;
            s_inflightSequence = seq;
            s_inflightSentUs   = now;
        }

        // Frozen server: no pong in over kPongTimeoutMs. Caller will trigger
        // the disconnect popup.
        return (now - s_lastPongRecvUs) > kPongTimeoutUs;
    }

    void OnPong(uint16_t sequence)
    {
        const int64_t now = NowUs();
        s_lastPongRecvUs = now;

        // Only the matching in-flight ping contributes to the RTT — out-of-
        // order pongs (rare, but possible if a ping was retried elsewhere)
        // just refresh the silence timer above without skewing the display.
        if (sequence != 0 && sequence == s_inflightSequence && s_inflightSentUs != 0)
        {
            const int64_t rttUs = now - s_inflightSentUs;
            // Convert µs → ms with proper rounding so a 1500µs RTT shows as
            // "2 ms" instead of truncating to 1. Sub-microsecond paths (rare,
            // loopback worst case) still surface as 1 ms rather than 0.
            const int rttMs = static_cast<int>((rttUs + 500) / 1000);
            s_latencyMs        = rttMs > 0 ? rttMs : (rttUs > 0 ? 1 : 0);
            s_inflightSequence = 0;
            s_inflightSentUs   = 0;
        }
    }

    int GetMsSinceLastPing()
    {
        if (s_lastPingSentUs == 0) return -1;
        return static_cast<int>((NowUs() - s_lastPingSentUs) / 1000);
    }

    int GetMsSinceLastPong()
    {
        if (s_lastPongRecvUs == 0) return -1;
        return static_cast<int>((NowUs() - s_lastPongRecvUs) / 1000);
    }

    int GetLatencyMs()
    {
        // While a ping is in flight, surface its growing age as a lower-bound
        // latency estimate. That way a paused/frozen server makes the HUD
        // climb in real time (50ms → 200ms → 2000ms → ...) instead of sitting
        // at the last completed RTT until the 30s disconnect popup fires.
        // When the ping finally completes (or a fresh one starts), OnPong /
        // the next tick replaces this with a true measurement.
        if (s_inflightSentUs != 0)
        {
            const int64_t inflightAgeUs = NowUs() - s_inflightSentUs;
            const int inflightAgeMs = static_cast<int>((inflightAgeUs + 500) / 1000);
            if (inflightAgeMs > s_latencyMs)
            {
                return inflightAgeMs;
            }
        }
        return s_latencyMs;
    }
}
