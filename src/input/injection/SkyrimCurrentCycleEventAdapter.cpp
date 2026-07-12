#include "pch.h"

#include "input/injection/SkyrimCurrentCycleEventAdapter.h"

#include <algorithm>

namespace dualpad::input
{
    namespace
    {
        input_v2::gameplay::CurrentCycleEventDisposition DispositionFor(
            const input_v2::gameplay::CurrentCycleGatePlan& plan,
            input_v2::gameplay::CurrentCycleChannel channel)
        {
            using input_v2::gameplay::CurrentCycleChannel;
            switch (channel) {
            case CurrentCycleChannel::Look:
                return plan.look;
            case CurrentCycleChannel::Move:
                return plan.move;
            case CurrentCycleChannel::Combat:
                return plan.combat;
            case CurrentCycleChannel::TransientDigital:
                return plan.transientDigital;
            default:
                return input_v2::gameplay::CurrentCycleEventDisposition::Keep;
            }
        }
    }

    CurrentCycleAdapterAudit SkyrimCurrentCycleEventAdapter::AuditDescriptors(
        std::vector<CurrentCycleEventDescriptor>& descriptors,
        const input_v2::gameplay::CurrentCycleGatePlan& plan,
        const CurrentCycleAdapterOptions& options) const
    {
        using namespace input_v2::gameplay;
        if (descriptors.size() > options.scratchCapacity) {
            return {
                .success = false,
                .shadowOnly = options.shadowOnly,
                .failure = CurrentCycleGateFailure::ScratchCapacityExceeded,
                .affectedChannels = plan.affectedChannels
            };
        }
        if (plan.failure != CurrentCycleGateFailure::None) {
            return {
                .success = false,
                .shadowOnly = options.shadowOnly,
                .failure = plan.failure,
                .affectedChannels = plan.affectedChannels
            };
        }
        if (!options.shadowOnly || ProductionMutationEnabled()) {
            return {
                .success = false,
                .shadowOnly = options.shadowOnly,
                .failure = CurrentCycleGateFailure::ConsumerOrderUnproven,
                .affectedChannels = plan.affectedChannels
            };
        }

        CurrentCycleAdapterAudit audit{
            .success = true,
            .shadowOnly = true,
            .affectedChannels = plan.affectedChannels,
            .currentEventWriterCount = plan.currentEventWriterCount
        };
        for (const auto& descriptor : descriptors) {
            if (descriptor.virtualEvent &&
                DispositionFor(plan, descriptor.channel) != CurrentCycleEventDisposition::Keep) {
                ++audit.wouldMutateCount;
            }
        }
        return audit;
    }

    CurrentCycleAdapterAudit SkyrimCurrentCycleEventAdapter::AuditEventListShadow(
        RE::InputEvent* const* events,
        const input_v2::gameplay::CurrentCycleGatePlan& plan,
        std::size_t scratchCapacity) const
    {
        using namespace input_v2::gameplay;
        if (!events) {
            return {
                .success = false,
                .failure = CurrentCycleGateFailure::AdapterFailure,
                .affectedChannels = plan.affectedChannels
            };
        }
        std::vector<const RE::InputEvent*> visited;
        visited.reserve(std::min<std::size_t>(scratchCapacity, 64));
        for (auto* current = *events; current; current = current->next) {
            if (visited.size() == scratchCapacity) {
                return {
                    .success = false,
                    .failure = CurrentCycleGateFailure::ScratchCapacityExceeded,
                    .affectedChannels = plan.affectedChannels
                };
            }
            if (std::find(visited.begin(), visited.end(), current) != visited.end()) {
                return {
                    .success = false,
                    .failure = CurrentCycleGateFailure::AdapterFailure,
                    .affectedChannels = plan.affectedChannels
                };
            }
            visited.push_back(current);
        }

        std::vector<CurrentCycleEventDescriptor> descriptors;
        descriptors.reserve(4);
        const auto append = [&](CurrentCycleChannel channel) {
            if ((plan.affectedChannels & CurrentCycleChannelMask(channel)) != 0) {
                descriptors.push_back(CurrentCycleEventDescriptor{
                    .channel = channel,
                    .virtualEvent = true
                });
            }
        };
        append(CurrentCycleChannel::Look);
        append(CurrentCycleChannel::Move);
        append(CurrentCycleChannel::Combat);
        append(CurrentCycleChannel::TransientDigital);
        return AuditDescriptors(
            descriptors,
            plan,
            CurrentCycleAdapterOptions{
                .shadowOnly = true,
                .consumerOrderProven = false,
                .scratchCapacity = scratchCapacity
            });
    }
}
