#include "input_v2/presentation/SourceEvidenceCollector.h"

namespace dualpad::input_v2::presentation
{
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

    const SourceEvidenceSnapshot& SourceEvidenceCollector::GetLatestSnapshot() const
    {
        return _latest;
    }

    void SourceEvidenceCollector::ResetForTests()
    {
        _latest = {};
    }
}
