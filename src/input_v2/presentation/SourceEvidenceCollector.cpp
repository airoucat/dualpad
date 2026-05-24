#include "input_v2/presentation/SourceEvidenceCollector.h"

namespace dualpad::input_v2::presentation
{
    namespace
    {
        constexpr std::uint64_t kMouseMoveAccumulatorResetMs = 250;
    }

    DeviceFamilyIngressPublication DeviceFamilyIngressPublisher::Publish(
        DeviceFamily family,
        DeviceFamilyEvidenceSource source,
        std::uint64_t tick)
    {
        const bool changed =
            family != _published.family ||
            source != _published.source ||
            (_published.source == DeviceFamilyEvidenceSource::None &&
                source != DeviceFamilyEvidenceSource::None);

        if (changed) {
            _published.family = family;
            _published.source = source;
            _published.publishedTick = tick;
            ++_published.deviceFamilyRevision;
            return DeviceFamilyIngressPublication{
                .evidence = _published,
                .marker = DeviceFamilyChangedPayload{
                    .family = family,
                    .newRevision = _published.deviceFamilyRevision,
                    .source = source,
                    .publishedTick = tick
                }
            };
        }

        _published.publishedTick = tick;
        return DeviceFamilyIngressPublication{ .evidence = _published };
    }

    DeviceFamilyIngressPublication DeviceFamilyIngressPublisher::ExplicitResync(DeviceFamily family, std::uint64_t tick)
    {
        _published.family = family;
        _published.source = DeviceFamilyEvidenceSource::ExplicitResync;
        _published.publishedTick = tick;
        ++_published.deviceFamilyRevision;
        return DeviceFamilyIngressPublication{
            .evidence = _published,
            .marker = DeviceFamilyChangedPayload{
                .family = family,
                .newRevision = _published.deviceFamilyRevision,
                .source = DeviceFamilyEvidenceSource::ExplicitResync,
                .publishedTick = tick
            }
        };
    }

    const PublishedDeviceFamilyEvidence& DeviceFamilyIngressPublisher::GetPublished() const
    {
        return _published;
    }

    void DeviceFamilyIngressPublisher::ResetForTests()
    {
        _published = {};
    }

    SourceEvidenceFrame SourceEvidenceCollector::CollectAfterDeviceFamilyIngress(
        const DeviceFamilyIngressPublication& publication,
        const context::ResolvedContextSnapshot& contextSnapshot,
        std::uint64_t tick)
    {
        _latest.deviceFamilyEvidence = publication.evidence;
        _latest.syntheticKeyboardWindow = IsSyntheticKeyboardWindowActive(tick);
        _latest.gamepadLease = HasGamepadLeaseActive(tick);
        _latest.contextRevision = contextSnapshot.contextRevision;
        _latest.uiContextId = contextSnapshot.uiContextId;
        _latest.presentationPolicyId = contextSnapshot.presentationPolicyId;
        _latest.collectedTick = tick;

        SourceEvidenceFrame frame{};
        if (publication.marker) {
            frame.records.push_back(SourceEvidenceRecord{
                .kind = SourceEvidenceRecordKind::DeviceFamilyChanged,
                .deviceFamilyChanged = *publication.marker
            });
        }
        frame.records.push_back(SourceEvidenceRecord{
            .kind = SourceEvidenceRecordKind::SourceEvidenceSnapshot,
            .sourceEvidence = _latest
        });
        return frame;
    }

    void SourceEvidenceCollector::RecordKeyboardEvidence(
        bool active,
        bool syntheticSuppressed,
        std::uint64_t tick)
    {
        _latest.keyboardEvidence = active && !syntheticSuppressed;
        if (_latest.keyboardEvidence) {
            _latest.gamepadEvidence = false;
            ClearGamepadLease();
        }
        _latest.syntheticKeyboardWindow = IsSyntheticKeyboardWindowActive(tick);
        _latest.collectedTick = tick;
    }

    void SourceEvidenceCollector::RecordMouseButtonEvidence(bool active, std::uint64_t tick)
    {
        _latest.mouseButtonEvidence = active;
        if (active) {
            _latest.gamepadEvidence = false;
            ClearGamepadLease();
            _latest.pointerSignal = PointerSignal::PointerActive;
        }
        _latest.collectedTick = tick;
    }

    void SourceEvidenceCollector::RecordMouseMoveEvidence(std::int32_t dx, std::int32_t dy, std::uint64_t tick)
    {
        if (dx == 0 && dy == 0) {
            return;
        }
        if (_mouseMoveAccumulator.windowStartMs == 0 ||
            (_mouseMoveAccumulator.lastMoveAtMs != 0 &&
                tick - _mouseMoveAccumulator.lastMoveAtMs > kMouseMoveAccumulatorResetMs)) {
            _mouseMoveAccumulator = {};
            _mouseMoveAccumulator.windowStartMs = tick;
        }

        _mouseMoveAccumulator.dx += dx < 0 ? -dx : dx;
        _mouseMoveAccumulator.dy += dy < 0 ? -dy : dy;
        _mouseMoveAccumulator.lastMoveAtMs = tick;
        _latest.mouseMoveEvidence = true;
        _latest.gamepadEvidence = false;
        ClearGamepadLease();
        _latest.pointerSignal = PointerSignal::HoverOnly;
        _latest.collectedTick = tick;
    }

    void SourceEvidenceCollector::RecordGamepadEvidence(
        bool active,
        std::uint64_t tick,
        std::uint64_t leaseWindowMs)
    {
        _latest.gamepadEvidence = active;
        if (active) {
            _latest.keyboardEvidence = false;
            _latest.mouseButtonEvidence = false;
            _latest.mouseMoveEvidence = false;
            _gamepadLeaseExpiresAtMs = tick + leaseWindowMs;
            _latest.pointerSignal = PointerSignal::None;
        }
        _latest.gamepadLease = HasGamepadLeaseActive(tick);
        _latest.collectedTick = tick;
    }

    void SourceEvidenceCollector::MarkSyntheticKeyboardScancode(
        std::uint8_t scancode,
        std::uint8_t pendingEvents,
        std::uint64_t windowMs,
        std::uint64_t nowMs)
    {
        if (pendingEvents == 0) {
            return;
        }
        auto& state = _suppressedKeyboardScancodes[scancode];
        const auto accumulated = static_cast<unsigned int>(state.pendingEvents) + pendingEvents;
        state.pendingEvents = static_cast<std::uint8_t>((std::min)(accumulated, 0xFFu));
        state.expiresAtMs = nowMs + windowMs;
        if (_syntheticKeyboardWindowExpiresAtMs < state.expiresAtMs) {
            _syntheticKeyboardWindowExpiresAtMs = state.expiresAtMs;
        }
        _latest.syntheticKeyboardWindow = true;
    }

    bool SourceEvidenceCollector::ConsumeSyntheticKeyboardScancode(std::uint32_t scancode, std::uint64_t nowMs)
    {
        auto& state = _suppressedKeyboardScancodes[static_cast<std::uint8_t>(scancode & 0xFF)];
        if (state.pendingEvents == 0) {
            return false;
        }
        if (state.expiresAtMs != 0 && nowMs > state.expiresAtMs) {
            state = {};
            return false;
        }

        --state.pendingEvents;
        if (state.pendingEvents == 0) {
            state.expiresAtMs = 0;
        }
        return true;
    }

    bool SourceEvidenceCollector::IsSyntheticKeyboardWindowActive(std::uint64_t nowMs)
    {
        if (_syntheticKeyboardWindowExpiresAtMs == 0) {
            _latest.syntheticKeyboardWindow = false;
            return false;
        }
        if (nowMs > _syntheticKeyboardWindowExpiresAtMs) {
            _syntheticKeyboardWindowExpiresAtMs = 0;
            _latest.syntheticKeyboardWindow = false;
            return false;
        }
        _latest.syntheticKeyboardWindow = true;
        return true;
    }

    bool SourceEvidenceCollector::HasGamepadLeaseActive(std::uint64_t nowMs)
    {
        if (_gamepadLeaseExpiresAtMs == 0) {
            _latest.gamepadLease = false;
            return false;
        }
        if (nowMs > _gamepadLeaseExpiresAtMs) {
            _gamepadLeaseExpiresAtMs = 0;
            _latest.gamepadLease = false;
            return false;
        }
        _latest.gamepadLease = true;
        return true;
    }

    void SourceEvidenceCollector::ClearGamepadLease()
    {
        _gamepadLeaseExpiresAtMs = 0;
        _latest.gamepadLease = false;
    }

    bool SourceEvidenceCollector::ShouldPromoteMouseMove(
        std::int32_t thresholdPx,
        std::uint64_t promoteDelayMs,
        std::uint64_t nowMs) const
    {
        if (_mouseMoveAccumulator.windowStartMs == 0) {
            return false;
        }
        if (nowMs - _mouseMoveAccumulator.windowStartMs < promoteDelayMs) {
            return false;
        }
        return (_mouseMoveAccumulator.dx + _mouseMoveAccumulator.dy) >= thresholdPx;
    }

    void SourceEvidenceCollector::ResetMouseMoveEvidence()
    {
        _mouseMoveAccumulator = {};
        _latest.mouseMoveEvidence = false;
    }

    void SourceEvidenceCollector::ResetForContextBoundary(std::uint64_t tick)
    {
        ResetMouseMoveEvidence();
        _latest.keyboardEvidence = false;
        _latest.mouseButtonEvidence = false;
        _latest.gamepadEvidence = false;
        _latest.pointerSignal = PointerSignal::None;
        _latest.collectedTick = tick;
    }

    const SourceEvidenceSnapshot& SourceEvidenceCollector::GetLatestSnapshot() const
    {
        return _latest;
    }

    void SourceEvidenceCollector::ResetForTests()
    {
        _latest = {};
        _suppressedKeyboardScancodes = {};
        _mouseMoveAccumulator = {};
        _syntheticKeyboardWindowExpiresAtMs = 0;
        _gamepadLeaseExpiresAtMs = 0;
    }
}
