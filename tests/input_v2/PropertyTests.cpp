#include "pch.h"

#include "input/PadEvent.h"
#include "input_v2/actions/InteractionEngine.h"
#include "input_v2/ingress/FrameAssembler.h"
#include "input_v2/ingress/IngressHub.h"

#include <algorithm>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }
}

int main()
{
    using namespace dualpad::input_v2::ingress;
    namespace actions = dualpad::input_v2::actions;

    IngressHub hub{ 8 };
    for (std::uint64_t i = 0; i < 4; ++i) {
        IngressEvent event{};
        event.kind = IngressKind::PadSnapshot;
        event.source = IngressSource::LegacyDispatcher;
        event.monotonicUs = i + 1;
        Require(hub.PushEvent(std::move(event)), "push should fit bounded property hub");
    }

    auto drained = hub.Drain();
    Require(drained.size() == 4, "property hub should drain all pushed events");
    for (std::size_t i = 1; i < drained.size(); ++i) {
        Require(drained[i - 1].seq < drained[i].seq, "ingress seq must be strictly monotonic");
    }

    FrameAssembler assembler;
    const auto frames = assembler.Assemble(drained);
    Require(!frames.empty(), "property assembler should publish at least one frame");
    std::uint64_t lastStableTime = 0;
    for (const auto& frame : frames) {
        Require(frame.firstSeq <= frame.lastSeq, "assembled frame sequence range must be ordered");
        if (frame.kind == AssembledFrameKind::Transition) {
            Require(!ShouldDispatchToInteractionEngine(frame), "transition frames must not dispatch");
            continue;
        }
        if (frame.facts.monotonicUs != 0 && lastStableTime != 0) {
            Require(frame.facts.monotonicUs >= lastStableTime, "stable frame monotonic time must not regress");
        }
        if (frame.facts.monotonicUs != 0) {
            lastStableTime = frame.facts.monotonicUs;
        }
    }

    actions::CompiledActionGraph graph{};
    graph.manifestEpoch = 1;
    graph.actions.push_back(actions::ActionDefinition{
        .id = "Game.Look",
        .valueKind = actions::ActionValueKind::Axis2D
    });
    graph.bindings.push_back(actions::CompiledGraphBinding{
        .bindingId = 1,
        .actionId = "Game.Look",
        .actionSetId = "GameplayBase",
        .paths = { actions::ControlPath{
            .kind = actions::ControlPathKind::AnalogAxis1D,
            .code = static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickX),
            .component = actions::AxisComponent::X } },
        .interaction = actions::InteractionSpec{ .kind = actions::InteractionKind::Value }
    });
    graph.bindings.push_back(actions::CompiledGraphBinding{
        .bindingId = 2,
        .actionId = "Game.Look",
        .actionSetId = "GameplayBase",
        .paths = { actions::ControlPath{
            .kind = actions::ControlPathKind::AnalogAxis1D,
            .code = static_cast<std::uint32_t>(dualpad::input::PadAxisId::RightStickY),
            .component = actions::AxisComponent::Y } },
        .interaction = actions::InteractionSpec{ .kind = actions::InteractionKind::Value }
    });
    graph.lookups.bindingIndexById[1] = 0;
    graph.lookups.bindingIndexById[2] = 1;
    graph.lookups.bindingIdsByActionSetId["GameplayBase"] = { 1, 2 };

    actions::KernelFrame axisFrame{};
    axisFrame.facts.manifestEpoch = 1;
    axisFrame.facts.contextRevision = 1;
    axisFrame.facts.monotonicUs = 9'000;
    axisFrame.state.controlSamples = {
        actions::ControlSample{
            .path = graph.bindings[0].paths[0],
            .down = true,
            .scalar = 2.0f,
            .timestampUs = 8'990 },
        actions::ControlSample{
            .path = graph.bindings[1].paths[0],
            .down = true,
            .scalar = -2.0f,
            .timestampUs = 8'995 }
    };

    actions::ActionSetStack stack{};
    stack.baseSetId = "GameplayBase";
    actions::InteractionStateStore state;
    const auto resolved = actions::InteractionEngine{}.Resolve(graph, stack, axisFrame, state);
    Require(resolved.values.size() == 1, "property Axis2D coalescing must emit one value per action per frame");
    Require(resolved.values[0].x >= -1.0f && resolved.values[0].x <= 1.0f, "property Axis2D x must stay in range");
    Require(resolved.values[0].y >= -1.0f && resolved.values[0].y <= 1.0f, "property Axis2D y must stay in range");
    Require(resolved.values[0].timestampUs == axisFrame.facts.monotonicUs, "property Axis2D timestamp must coalesce to frame time");

    IngressHub latestHub{ 16 };
    bool reducerDown = false;
    std::uint32_t physicalMask = 0;
    std::size_t maxPending = 0;
    auto consumeEdges = [&](const IngressCapture& capture) {
        for (const auto& event : capture.events) {
            if (event.kind != IngressKind::PadSnapshot) {
                continue;
            }
            for (const auto& sample : event.pad.samples) {
                if (sample.pressed) {
                    Require(!reducerDown, "property ordered reducer must not observe a synthetic duplicate press");
                    reducerDown = true;
                }
                if (sample.released) {
                    Require(reducerDown, "property ordered reducer must not observe a release without a drained press");
                    reducerDown = false;
                }
            }
        }
    };

    for (std::uint64_t sequence = 1; sequence <= 512; ++sequence) {
        if ((sequence % 17) == 0) {
            physicalMask ^= 0x1u;
        }
        dualpad::input::PadEventSnapshot snapshot{};
        snapshot.type = dualpad::input::PadEventSnapshotType::Input;
        snapshot.firstSequence = sequence;
        snapshot.sequence = sequence;
        snapshot.sourceTimestampUs = sequence * 100;
        snapshot.state.sequence = sequence;
        snapshot.state.timestampUs = snapshot.sourceTimestampUs;
        snapshot.state.buttons.digitalMask = physicalMask;
        snapshot.state.rightStick.x = static_cast<float>(sequence % 101) / 100.0f;
        Require(latestHub.PushPadSnapshot(snapshot, false), "property latest-state report must fit bounded edge queue");
        maxPending = std::max(maxPending, latestHub.PendingCount());
        if ((sequence % 23) == 0) {
            consumeEdges(latestHub.Capture(2));
        }
    }
    while (latestHub.PendingCount() != 0) {
        consumeEdges(latestHub.Capture(1));
    }
    const auto latestCapture = latestHub.Capture(1);
    Require(maxPending <= 3, "property steady reports must not amplify into per-report ordered queue entries");
    Require(!reducerDown && physicalMask == 0, "property ordered reducer must converge after a clean release");
    Require(latestCapture.latestPadState && latestCapture.latestPadState->generation == 512, "property latest generation must advance once per complete report");

    return 0;
}
