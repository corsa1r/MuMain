#pragma once

#include <cstdint>

// BloodlustMU client→server heartbeat. Detects a frozen / paused server that
// the TCP layer can't see (docker-paused container, server process suspended
// in a debugger, etc.) — keepalive ACKs still come from the host kernel, so
// the connection looks healthy at the network level even though no game
// packets ever arrive. We send a tiny ping every kPingIntervalMs and require
// a pong from the server within kPongTimeoutMs; otherwise the client treats
// the connection as lost and triggers the regular disconnect popup.
//
// Latency display: the most recent ping→pong round-trip is exposed via
// GetLatencyMs() so the HUD can show it (top-right corner by default).
namespace Heartbeat
{
    // ── Configuration ────────────────────────────────────────────────────
    // Short intervals so a frozen/paused server is obvious in a few seconds
    // instead of a half-minute. The packets are 6 bytes each direction so
    // the bandwidth cost is negligible (~3 packets/sec across all players
    // even in a 1000-CCU server).
    inline constexpr int kPingIntervalMs = 2000;   // send ping every 2s
    inline constexpr int kPongTimeoutMs  = 8000;   // declare dead after 8s of silence

    // ── Lifecycle ────────────────────────────────────────────────────────
    // Called when a connection is freshly established (e.g. after entering
    // MAIN scene) so timers and sequence start clean.
    void Reset();

    // Called once per frame from the main scene loop. Sends pings on
    // schedule and returns true if the link has gone silent past the
    // timeout — caller is expected to trigger the disconnect popup.
    bool TickAndCheckTimeout();

    // ── Wire callbacks ───────────────────────────────────────────────────
    // Server pong handler — called from ReceivePrimeStatus when sub-op
    // 0x07 arrives. Updates RTT measurement.
    void OnPong(uint16_t sequence);

    // ── HUD state ────────────────────────────────────────────────────────
    // Latest measured round-trip in milliseconds. Returns -1 when no pong
    // has been received yet (e.g. just connected).
    int GetLatencyMs();

    // ── Diagnostics ──────────────────────────────────────────────────────
    // Milliseconds since the last ping was sent (-1 = never).
    int GetMsSinceLastPing();
    // Milliseconds since the last pong was received (-1 = never).
    int GetMsSinceLastPong();
}
