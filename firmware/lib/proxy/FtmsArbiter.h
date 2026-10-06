#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "Ftms.h"

// The SB20 full proxy's FTMS control-point arbiter (code/findings/sb20-full-proxy.md §3c, issue #291).
//
// Under the full proxy our board is the bike's ONLY BLE central, so it is the only writer to the
// bike's control point 0x2AD9. Two parties want to write through it: an FTMS app connected to the
// proxy (qz, Zwift, MyWhoosh, ...: "a client") and our own head unit's workout engine. The arbiter
// owns exactly one question, WHO may write, and otherwise relays bytes unchanged in both directions.
//
// A pure state machine: the BLE seam pumps EVENTS in (a client write, a bike indication, a tick, the
// bike link going up/down, a workout starting/stopping) and performs the ACTIONS that come back
// (write to the bike, indicate to a client, notify the head unit). No radio, no clock, no Arduino.
//
// The rules (owner decisions 2026-10-02, §9):
//   * Owner is none, a client (by connection id) or the head unit. A client takes ownership with
//     Request Control (0x00); the head unit takes it when a workout starts. The FIRST owner keeps it;
//     the other is refused: a client with the indication 0x80 <op> 0x05 (control not permitted), the
//     head unit with a Refused notice it can show.
//   * One request in flight to the bike at a time; later ones queue (FIFO, bounded). The owner's write
//     is forwarded unchanged and the bike's indication is relayed back to the requester unchanged
//     (the SB20 echoes the target in its Set Target Power reply, 80 05 01 96 00, so "unchanged"
//     matters). Every op is relayed: erg 0x05, resistance 0x04, simulation 0x11, start/stop, reset.
//   * No indication within the timeout, or the bike link is down: the proxy answers 0x80 <op> 0x04
//     (operation failed) itself, so nobody waits forever.
//   * The owner's client disconnecting releases ownership and sends NOTHING to the bike: the bike
//     holds its last target.
//   * After a bike-link drop and reconnect the arbiter re-requests control, re-sends Start if the
//     owner had started, and re-sends the owner's last target-setting write. Those replies go to
//     nobody (the owner did not ask); a failure is counted.
//
// Notes on behaviour the design leaves to the implementation:
//   * A client's Request Control CLAIMS ownership the moment it is accepted (arrival order decides
//     "first"); the claim is confirmed by the bike's success and reverted on anything else.
//   * A write from a party that does not own control (other than Request Control) is answered
//     0x80 <op> 0x05 locally, per the FTMS spec, and never reaches the bike.
//   * A successful Reset (0x01) from the owning client releases its ownership (FTMS semantics: reset
//     returns control); the head unit's ownership is tied to its workout, so it keeps it.
//   * The re-asserted target is the last SUCCESSFUL target write (0x04, 0x05 or 0x11): a write the
//     owner was told had failed (timeout, link drop) is not replayed.
//   * A bike indication with nothing in flight, or for a different op, is stale and is dropped.
namespace sb20proxy {

enum class CpOwner : uint8_t { None, Client, HeadUnit };

enum class CpActionKind : uint8_t {
    WriteBike,       // write `bytes` to the bike's control point 0x2AD9
    IndicateClient,  // indicate `bytes` on the proxy's 0x2AD9 to client `clientId`
    NotifyHeadUnit,  // tell the head unit `notice` (+ the response `bytes` for Response)
};

enum class HeadUnitNotice : uint8_t {
    None,
    Granted,   // the workout start took control
    Refused,   // a client already owns control (show it)
    Response,  // the control-point response to a head-unit write, bytes as the bike sent them
};

struct CpAction {
    CpActionKind kind = CpActionKind::WriteBike;
    uint16_t clientId = 0;
    HeadUnitNotice notice = HeadUnitNotice::None;
    std::vector<uint8_t> bytes;
};
using CpActions = std::vector<CpAction>;

class FtmsArbiter {
 public:
    // The SB20 answered every captured control-point write within 321 ms (G-sb20-ftms-erg*.jsonl,
    // 2026-06-21: 0.09-0.32 s); 2 s is six times the slowest, and well inside the ATT 30 s limit.
    static constexpr uint32_t kDefaultTimeoutMs = 2000;
    static constexpr size_t kMaxQueue = 4;   // queued behind the one in flight
    static constexpr size_t kMaxWrite = 20;  // an FTMS CP write fits the default ATT MTU (23 - 3)

    explicit FtmsArbiter(uint32_t timeoutMs = kDefaultTimeoutMs) : timeoutMs_(timeoutMs) {}

    // ---- events ---------------------------------------------------------------------------------

    // A client wrote the proxy's control point.
    CpActions onClientWrite(uint16_t clientId, const uint8_t* d, size_t len, uint32_t nowMs) {
        CpActions out;
        if (d == nullptr || len == 0) return out;
        const uint8_t op = d[0];
        if (len > kMaxWrite) {
            answerClient(out, clientId, op, FTMS_CP_INVALID_PARAMETER);
            return out;
        }
        const bool isOwner = owner_ == CpOwner::Client && ownerClient_ == clientId;
        const bool othersOwn = owner_ != CpOwner::None && !isOwner;
        if (othersOwn || (owner_ == CpOwner::None && op != FTMS_CP_REQUEST_CONTROL)) {
            ++refusals_;
            answerClient(out, clientId, op, FTMS_CP_CONTROL_NOT_PERMITTED);
            return out;
        }
        if (!linkUp_ || queue_.size() >= kMaxQueue) {
            answerClient(out, clientId, op, FTMS_CP_OPERATION_FAILED);
            return out;
        }
        Request r;
        r.from = From::Client;
        r.clientId = clientId;
        r.bytes.assign(d, d + len);
        if (op == FTMS_CP_REQUEST_CONTROL && owner_ == CpOwner::None) {
            owner_ = CpOwner::Client;  // the claim: arrival order decides who was first
            ownerClient_ = clientId;
            r.claims = true;
        }
        queue_.push_back(r);
        pump(out, nowMs);
        return out;
    }

    // The client's link to the proxy closed. Releases its ownership; nothing goes to the bike.
    CpActions onClientDisconnect(uint16_t clientId, uint32_t nowMs) {
        CpActions out;
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->from == From::Client && it->clientId == clientId) it = queue_.erase(it);
            else ++it;
        }
        if (inFlight_ && current_.from == From::Client && current_.clientId == clientId) {
            current_.from = From::Gone;  // its reply, when it comes, goes to nobody
        }
        if (owner_ == CpOwner::Client && ownerClient_ == clientId) releaseOwnership();
        pump(out, nowMs);
        return out;
    }

    // The bike indicated on its control point (0x80 <op> <result> [params]).
    CpActions onBikeIndication(const uint8_t* d, size_t len, uint32_t nowMs) {
        CpActions out;
        if (d == nullptr || len < 3 || d[0] != FTMS_CP_RESPONSE) { ++stale_; return out; }
        if (!inFlight_ || d[1] != current_.bytes[0]) { ++stale_; return out; }
        complete(out, std::vector<uint8_t>(d, d + len), d[2] == FTMS_CP_SUCCESS);
        pump(out, nowMs);
        return out;
    }

    // Time passes. Fails the request in flight once it has waited the timeout.
    CpActions onTick(uint32_t nowMs) {
        CpActions out;
        if (inFlight_ && (uint32_t)(nowMs - sentAtMs_) >= timeoutMs_) {
            ++timeouts_;
            complete(out, encodeControlPointResponse(current_.bytes[0], FTMS_CP_OPERATION_FAILED), false);
            pump(out, nowMs);
        }
        return out;
    }

    // The proxy's link to the bike came up (first time, or after a drop). Re-asserts the owner.
    CpActions onBikeLinkUp(uint32_t nowMs) {
        CpActions out;
        linkUp_ = true;
        if (owner_ != CpOwner::None) {
            queueReassert(encodeRequestControl());
            if (started_) queueReassert(encodeStart());
            if (!lastTarget_.empty()) queueReassert(lastTarget_);
        }
        pump(out, nowMs);
        return out;
    }

    // The proxy's link to the bike dropped. Everything waiting is answered "operation failed".
    CpActions onBikeLinkDown(uint32_t /*nowMs*/) {
        CpActions out;
        linkUp_ = false;
        if (inFlight_) {
            complete(out, encodeControlPointResponse(current_.bytes[0], FTMS_CP_OPERATION_FAILED), false);
        }
        while (!queue_.empty()) {
            Request r = queue_.front();
            queue_.pop_front();
            current_ = r;
            inFlight_ = true;
            complete(out, encodeControlPointResponse(r.bytes[0], FTMS_CP_OPERATION_FAILED), false);
        }
        return out;
    }

    // The head unit's workout started: it takes control unless a client already holds it.
    CpActions onWorkoutStart(uint32_t nowMs) {
        CpActions out;
        if (owner_ == CpOwner::Client) {
            ++refusals_;
            notifyHeadUnit(out, HeadUnitNotice::Refused, {});
            return out;
        }
        const bool wasOwner = owner_ == CpOwner::HeadUnit;
        owner_ = CpOwner::HeadUnit;
        notifyHeadUnit(out, HeadUnitNotice::Granted, {});
        if (!wasOwner && linkUp_) {
            Request r;
            r.from = From::HeadUnit;
            r.bytes = encodeRequestControl();
            queue_.push_back(r);
        }
        pump(out, nowMs);
        return out;
    }

    // The head unit's workout ended: release control and send nothing (the bike holds).
    CpActions onWorkoutStop(uint32_t nowMs) {
        CpActions out;
        if (owner_ != CpOwner::HeadUnit) return out;
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->from == From::HeadUnit) it = queue_.erase(it);
            else ++it;
        }
        releaseOwnership();
        pump(out, nowMs);
        return out;
    }

    // The head unit's workout engine wrote a control-point op (e.g. Set Target Power).
    CpActions onHeadUnitWrite(const uint8_t* d, size_t len, uint32_t nowMs) {
        CpActions out;
        if (d == nullptr || len == 0) return out;
        const uint8_t op = d[0];
        if (len > kMaxWrite) {
            notifyHeadUnit(out, HeadUnitNotice::Response, encodeControlPointResponse(op, FTMS_CP_INVALID_PARAMETER));
            return out;
        }
        if (owner_ != CpOwner::HeadUnit) {
            ++refusals_;
            notifyHeadUnit(out, HeadUnitNotice::Response,
                           encodeControlPointResponse(op, FTMS_CP_CONTROL_NOT_PERMITTED));
            return out;
        }
        if (!linkUp_ || queue_.size() >= kMaxQueue) {
            notifyHeadUnit(out, HeadUnitNotice::Response, encodeControlPointResponse(op, FTMS_CP_OPERATION_FAILED));
            return out;
        }
        Request r;
        r.from = From::HeadUnit;
        r.bytes.assign(d, d + len);
        queue_.push_back(r);
        pump(out, nowMs);
        return out;
    }

    // ---- state (for /status and the screen) ----------------------------------------------------
    CpOwner owner() const { return owner_; }
    uint16_t ownerClient() const { return ownerClient_; }
    bool bikeLinkUp() const { return linkUp_; }
    bool busy() const { return inFlight_; }
    size_t queued() const { return queue_.size(); }
    bool started() const { return started_; }
    const std::vector<uint8_t>& lastTarget() const { return lastTarget_; }  // empty = none
    uint32_t refusals() const { return refusals_; }
    uint32_t timeouts() const { return timeouts_; }
    uint32_t reassertFailures() const { return reassertFailures_; }
    uint32_t staleIndications() const { return stale_; }

 private:
    enum class From : uint8_t { Client, HeadUnit, Reassert, Gone };
    struct Request {
        From from = From::Client;
        uint16_t clientId = 0;
        bool claims = false;  // a client's Request Control that claimed ownership
        std::vector<uint8_t> bytes;
    };

    void pump(CpActions& out, uint32_t nowMs) {
        if (inFlight_ || !linkUp_ || queue_.empty()) return;
        current_ = queue_.front();
        queue_.pop_front();
        inFlight_ = true;
        sentAtMs_ = nowMs;
        CpAction a;
        a.kind = CpActionKind::WriteBike;
        a.bytes = current_.bytes;
        out.push_back(a);
    }

    // Finish the request in flight with `reply` (the bike's bytes, or one we synthesised).
    void complete(CpActions& out, const std::vector<uint8_t>& reply, bool success) {
        inFlight_ = false;
        const Request r = current_;
        const bool fromOwner =
            (r.from == From::Client && owner_ == CpOwner::Client && ownerClient_ == r.clientId) ||
            (r.from == From::HeadUnit && owner_ == CpOwner::HeadUnit) || r.from == From::Reassert;
        const bool claimFailed =
            r.claims && !success && owner_ == CpOwner::Client && ownerClient_ == r.clientId;
        if (success && fromOwner) applySuccess(r);
        switch (r.from) {
            case From::Client: {
                CpAction a;
                a.kind = CpActionKind::IndicateClient;
                a.clientId = r.clientId;
                a.bytes = reply;
                out.push_back(a);
                break;
            }
            case From::HeadUnit:
                notifyHeadUnit(out, HeadUnitNotice::Response, reply);
                break;
            case From::Reassert:
                if (!success) ++reassertFailures_;
                break;
            case From::Gone:
                break;
        }
        if (claimFailed) {  // after the reply, so the client hears its refusal first
            releaseOwnership();
            purgeClient(out, r.clientId);
        }
    }

    // What a successful op changes in the state we re-assert after a bike-link drop.
    void applySuccess(const Request& r) {
        switch (r.bytes[0]) {
            case FTMS_CP_START_RESUME: started_ = true; break;
            case FTMS_CP_STOP_PAUSE: started_ = false; break;
            case FTMS_CP_RESET:
                started_ = false;
                lastTarget_.clear();
                if (r.from == From::Client) releaseOwnership();  // FTMS: reset returns control
                break;
            case FTMS_CP_SET_TARGET_RESISTANCE:
            case FTMS_CP_SET_TARGET_POWER:
            case FTMS_CP_SET_INDOOR_BIKE_SIM:
                lastTarget_ = r.bytes;
                break;
            default: break;
        }
    }

    // A failed claim: the client's writes queued behind it never had control.
    void purgeClient(CpActions& out, uint16_t clientId) {
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->from == From::Client && it->clientId == clientId) {
                answerClient(out, clientId, it->bytes[0], FTMS_CP_CONTROL_NOT_PERMITTED);
                it = queue_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void releaseOwnership() {
        owner_ = CpOwner::None;
        ownerClient_ = 0;
        started_ = false;
        lastTarget_.clear();
        if (inFlight_ && current_.from == From::Reassert) current_.from = From::Gone;  // not ours now
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->from == From::Reassert) it = queue_.erase(it);
            else ++it;
        }
    }

    void queueReassert(const std::vector<uint8_t>& bytes) {
        Request r;
        r.from = From::Reassert;
        r.bytes = bytes;
        queue_.push_back(r);
    }

    static void answerClient(CpActions& out, uint16_t clientId, uint8_t op, uint8_t result) {
        CpAction a;
        a.kind = CpActionKind::IndicateClient;
        a.clientId = clientId;
        a.bytes = encodeControlPointResponse(op, result);
        out.push_back(a);
    }

    static void notifyHeadUnit(CpActions& out, HeadUnitNotice n, const std::vector<uint8_t>& bytes) {
        CpAction a;
        a.kind = CpActionKind::NotifyHeadUnit;
        a.notice = n;
        a.bytes = bytes;
        out.push_back(a);
    }

    uint32_t timeoutMs_;
    CpOwner owner_ = CpOwner::None;
    uint16_t ownerClient_ = 0;
    bool linkUp_ = false;
    bool started_ = false;
    std::vector<uint8_t> lastTarget_;
    std::deque<Request> queue_;
    bool inFlight_ = false;
    Request current_;
    uint32_t sentAtMs_ = 0;
    uint32_t refusals_ = 0;
    uint32_t timeouts_ = 0;
    uint32_t reassertFailures_ = 0;
    uint32_t stale_ = 0;
};

}  // namespace sb20proxy
