#pragma once

#include "input_v2/context/ContextResolver.h"

#include <cstdint>
#include <optional>
#include <array>
#include <vector>

namespace dualpad::input_v2::presentation
{
    enum class DeviceFamily : std::uint8_t
    {
        KeyboardMouse = 0,
        Gamepad
    };

    enum class DeviceFamilyEvidenceSource : std::uint8_t
    {
        None = 0,
        RawInputIngress,
        PollBridge,
        ExplicitResync
    };

    enum class PointerSignal : std::uint8_t
    {
        None = 0,
        HoverOnly,
        PointerActive
    };

    enum class SourceEvidenceRecordKind : std::uint8_t
    {
        DeviceFamilyChanged = 0,
        SourceEvidenceSnapshot
    };

    struct PublishedDeviceFamilyEvidence
    {
        DeviceFamily family{ DeviceFamily::KeyboardMouse };
        std::uint32_t deviceFamilyRevision{ 0 };
        DeviceFamilyEvidenceSource source{ DeviceFamilyEvidenceSource::None };
        std::uint64_t publishedTick{ 0 };

        friend bool operator==(const PublishedDeviceFamilyEvidence&, const PublishedDeviceFamilyEvidence&) = default;
    };

    struct DeviceFamilyChangedPayload
    {
        DeviceFamily family{ DeviceFamily::KeyboardMouse };
        std::uint32_t newRevision{ 0 };
        DeviceFamilyEvidenceSource source{ DeviceFamilyEvidenceSource::None };
        std::uint64_t publishedTick{ 0 };
    };

    struct DeviceFamilyIngressPublication
    {
        PublishedDeviceFamilyEvidence evidence;
        std::optional<DeviceFamilyChangedPayload> marker;
    };

    struct SourceEvidenceSnapshot
    {
        PublishedDeviceFamilyEvidence deviceFamilyEvidence;
        bool keyboardEvidence{ false };
        bool mouseButtonEvidence{ false };
        bool mouseMoveEvidence{ false };
        bool gamepadEvidence{ false };
        bool syntheticKeyboardWindow{ false };
        bool gamepadLease{ false };
        PointerSignal pointerSignal{ PointerSignal::None };
        std::uint32_t contextRevision{ 0 };
        context::UiContextId uiContextId{ context::UiContextId::None };
        context::PresentationPolicyId presentationPolicyId;
        std::uint64_t collectedTick{ 0 };
    };

    struct SourceEvidenceRecord
    {
        SourceEvidenceRecordKind kind{ SourceEvidenceRecordKind::SourceEvidenceSnapshot };
        DeviceFamilyChangedPayload deviceFamilyChanged;
        SourceEvidenceSnapshot sourceEvidence;
    };

    struct SourceEvidenceFrame
    {
        std::vector<SourceEvidenceRecord> records;
    };

    class DeviceFamilyIngressPublisher
    {
    public:
        DeviceFamilyIngressPublication Publish(
            DeviceFamily family,
            DeviceFamilyEvidenceSource source,
            std::uint64_t tick);
        DeviceFamilyIngressPublication ExplicitResync(DeviceFamily family, std::uint64_t tick);
        const PublishedDeviceFamilyEvidence& GetPublished() const;
        void ResetForTests();

    private:
        PublishedDeviceFamilyEvidence _published{};
    };

    class SourceEvidenceCollector
    {
    public:
        struct SuppressedScancodeState
        {
            std::uint8_t pendingEvents{ 0 };
            std::uint64_t expiresAtMs{ 0 };
        };

        SourceEvidenceFrame CollectAfterDeviceFamilyIngress(
            const DeviceFamilyIngressPublication& publication,
            const context::ResolvedContextSnapshot& contextSnapshot,
            std::uint64_t tick);
        void RecordKeyboardEvidence(bool active, bool syntheticSuppressed, std::uint64_t tick);
        void RecordMouseButtonEvidence(bool active, std::uint64_t tick);
        void RecordMouseMoveEvidence(std::int32_t dx, std::int32_t dy, std::uint64_t tick);
        void RecordGamepadEvidence(bool active, std::uint64_t tick, std::uint64_t leaseWindowMs);
        void MarkSyntheticKeyboardScancode(
            std::uint8_t scancode,
            std::uint8_t pendingEvents,
            std::uint64_t windowMs,
            std::uint64_t nowMs);
        bool ConsumeSyntheticKeyboardScancode(std::uint32_t scancode, std::uint64_t nowMs);
        bool IsSyntheticKeyboardWindowActive(std::uint64_t nowMs);
        bool HasGamepadLeaseActive(std::uint64_t nowMs);
        void ClearGamepadLease();
        bool ShouldPromoteMouseMove(
            std::int32_t thresholdPx,
            std::uint64_t promoteDelayMs,
            std::uint64_t nowMs) const;
        void ResetMouseMoveEvidence();
        void ResetForContextBoundary(std::uint64_t tick);
        const SourceEvidenceSnapshot& GetLatestSnapshot() const;
        void ResetForTests();

    private:
        struct MouseMoveAccumulator
        {
            std::int32_t dx{ 0 };
            std::int32_t dy{ 0 };
            std::uint64_t windowStartMs{ 0 };
            std::uint64_t lastMoveAtMs{ 0 };
        };

        SourceEvidenceSnapshot _latest{};
        std::array<SuppressedScancodeState, 256> _suppressedKeyboardScancodes{};
        MouseMoveAccumulator _mouseMoveAccumulator{};
        std::uint64_t _syntheticKeyboardWindowExpiresAtMs{ 0 };
        std::uint64_t _gamepadLeaseExpiresAtMs{ 0 };
    };
}
