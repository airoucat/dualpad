#pragma once

#include "input_v2/context/ContextResolver.h"

#include <cstdint>
#include <optional>
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
        SourceEvidenceFrame CollectAfterDeviceFamilyIngress(
            const DeviceFamilyIngressPublication& publication,
            const context::ResolvedContextSnapshot& contextSnapshot,
            std::uint64_t tick);
        const SourceEvidenceSnapshot& GetLatestSnapshot() const;
        void ResetForTests();

    private:
        SourceEvidenceSnapshot _latest{};
    };
}
