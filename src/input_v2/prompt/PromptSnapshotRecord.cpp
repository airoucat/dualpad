#include "pch.h"

#include "input_v2/prompt/PromptSnapshotRecord.h"

namespace dualpad::input_v2::prompt
{
    PromptSnapshotRecord MakePromptSnapshotRecord(const PromptQuery& query, const PromptDescriptor& descriptor)
    {
        return PromptSnapshotRecord{
            .actionId = std::string(query.actionId),
            .status = descriptor.status,
            .resolvedSet = descriptor.resolvedSet,
            .resolvedContext = descriptor.resolvedContext,
            .primary = descriptor.primary,
            .alternates = descriptor.alternates,
            .resolutionSource = descriptor.resolutionSource,
            .fallback = descriptor.fallback,
            .deviceProfile = descriptor.deviceProfile,
            .promptScopeRevision = descriptor.promptScopeRevision,
            .manifestEpoch = descriptor.manifestEpoch
        };
    }

    std::string_view ToString(PromptQueryStatus status)
    {
        switch (status) {
        case PromptQueryStatus::Ok:
            return "Ok";
        case PromptQueryStatus::ScopeUnavailable:
            return "ScopeUnavailable";
        case PromptQueryStatus::UnknownAction:
            return "UnknownAction";
        case PromptQueryStatus::UnknownContext:
            return "UnknownContext";
        case PromptQueryStatus::ContextOutOfScope:
            return "ContextOutOfScope";
        case PromptQueryStatus::NoVisibleBinding:
            return "NoVisibleBinding";
        case PromptQueryStatus::HiddenOnly:
            return "HiddenOnly";
        case PromptQueryStatus::DeviceFamilyMismatch:
        default:
            return "DeviceFamilyMismatch";
        }
    }

    std::string_view ToString(PromptResolutionSource source)
    {
        switch (source) {
        case PromptResolutionSource::ExactScope:
            return "ExactScope";
        case PromptResolutionSource::AncestorScope:
        default:
            return "AncestorScope";
        }
    }

    std::string_view ToString(PromptFallbackKind fallback)
    {
        switch (fallback) {
        case PromptFallbackKind::None:
            return "None";
        case PromptFallbackKind::AncestorScope:
        default:
            return "AncestorScope";
        }
    }
}
