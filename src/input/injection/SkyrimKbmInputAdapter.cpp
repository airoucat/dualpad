#include "pch.h"

#include "input/injection/SkyrimKbmInputAdapter.h"

#include "input/Action.h"

#include <string_view>

namespace dualpad::input
{
    namespace
    {
        constexpr std::uint32_t kMoveForwardBit = 1u << 0;
        constexpr std::uint32_t kMoveBackBit = 1u << 1;
        constexpr std::uint32_t kMoveLeftBit = 1u << 2;
        constexpr std::uint32_t kMoveRightBit = 1u << 3;
        constexpr std::uint32_t kCombatLeftBit = 1u << 0;
        constexpr std::uint32_t kCombatRightBit = 1u << 1;
        constexpr std::uint32_t kTransientJumpBit = 1u << 0;
        constexpr std::uint32_t kTransientActivateBit = 1u << 1;
        constexpr std::uint32_t kSustainedSprintBit = 1u << 0;

        void HashByte(std::uint64_t& hash, std::uint8_t value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        }

        void HashU32(std::uint64_t& hash, std::uint32_t value)
        {
            for (std::uint32_t shift = 0; shift < 32; shift += 8) {
                HashByte(hash, static_cast<std::uint8_t>(value >> shift));
            }
        }

        std::uint64_t FingerprintBindings(
            const std::vector<input_v2::ingress::KbmBindingEntry>& entries)
        {
            std::uint64_t hash = 1469598103934665603ull;
            for (const auto& entry : entries) {
                HashByte(hash, static_cast<std::uint8_t>(entry.physical.device));
                HashU32(hash, entry.physical.idCode);
                HashByte(hash, static_cast<std::uint8_t>(entry.gameplayClass));
                HashU32(hash, entry.semanticBit);
                for (const auto character : entry.actionId) {
                    HashByte(hash, static_cast<std::uint8_t>(character));
                }
                HashByte(hash, 0xFF);
            }
            return hash;
        }
    }

    input_v2::ingress::KbmBindingSnapshot SkyrimKbmInputAdapter::CaptureBindingSnapshot(
        const input_v2::context::ResolvedContextSnapshot& context)
    {
        using namespace input_v2::ingress;
        KbmBindingSnapshot snapshot{
            .contextRevision = context.contextRevision,
            .complete = false
        };
        const auto* controlMap = RE::ControlMap::GetSingleton();
        const auto* userEvents = RE::UserEvents::GetSingleton();
        if (!controlMap || !userEvents) {
            snapshot.generation = _bindingGeneration;
            snapshot.controlMapFingerprint = _lastFingerprint;
            return snapshot;
        }

        const auto add = [&](const auto& eventId,
                             std::string_view actionId,
                             KbmGameplayClass gameplayClass,
                             std::uint32_t semanticBit,
                             KbmPhysicalDevice physicalDevice,
                             RE::INPUT_DEVICE skyrimDevice) {
            const auto idCode = controlMap->GetMappedKey(
                eventId,
                skyrimDevice,
                RE::ControlMap::InputContextID::kGameplay);
            if (idCode == RE::ControlMap::kInvalid) {
                return;
            }
            snapshot.entries.push_back(KbmBindingEntry{
                .physical = KbmPhysicalCode{ physicalDevice, idCode },
                .actionId = std::string(actionId),
                .gameplayClass = gameplayClass,
                .semanticBit = semanticBit
            });
        };

        for (const auto [device, skyrimDevice] : {
                 std::pair{ KbmPhysicalDevice::Keyboard, RE::INPUT_DEVICE::kKeyboard },
                 std::pair{ KbmPhysicalDevice::Mouse, RE::INPUT_DEVICE::kMouse } }) {
            add(userEvents->forward, "Game.Move", KbmGameplayClass::Move, kMoveForwardBit, device, skyrimDevice);
            add(userEvents->back, "Game.Move", KbmGameplayClass::Move, kMoveBackBit, device, skyrimDevice);
            add(userEvents->strafeLeft, "Game.Move", KbmGameplayClass::Move, kMoveLeftBit, device, skyrimDevice);
            add(userEvents->strafeRight, "Game.Move", KbmGameplayClass::Move, kMoveRightBit, device, skyrimDevice);
            add(userEvents->leftAttack, actions::Attack, KbmGameplayClass::Combat, kCombatLeftBit, device, skyrimDevice);
            add(userEvents->rightAttack, actions::Block, KbmGameplayClass::Combat, kCombatRightBit, device, skyrimDevice);
            add(userEvents->jump, actions::Jump, KbmGameplayClass::TransientDigital, kTransientJumpBit, device, skyrimDevice);
            add(userEvents->activate, actions::Activate, KbmGameplayClass::TransientDigital, kTransientActivateBit, device, skyrimDevice);
            add(userEvents->sprint, actions::Sprint, KbmGameplayClass::SustainedDigital, kSustainedSprintBit, device, skyrimDevice);
        }

        snapshot.entries.push_back(KbmBindingEntry{
            .physical = KbmPhysicalCode{ KbmPhysicalDevice::Mouse, kMouseDeltaPhysicalIdCode },
            .actionId = "Game.Look",
            .gameplayClass = KbmGameplayClass::Look
        });

        snapshot.controlMapFingerprint = FingerprintBindings(snapshot.entries);
        if (_bindingGeneration == 0) {
            _bindingGeneration = 1;
        } else if (snapshot.controlMapFingerprint != _lastFingerprint) {
            ++_bindingGeneration;
        }
        _lastFingerprint = snapshot.controlMapFingerprint;
        snapshot.generation = _bindingGeneration;
        snapshot.complete = true;
        return snapshot;
    }

    input_v2::ingress::KbmObservedBatch SkyrimKbmInputAdapter::ObserveEventList(
        RE::InputEvent* const* events,
        const input_v2::ingress::KbmBindingSnapshot& bindings,
        std::uint64_t ownerTickToken,
        std::uint64_t eventBatchToken,
        std::uint64_t ownerNowUs) const
    {
        using namespace input_v2::ingress;
        KbmObservedBatch observed{
            .ownerTickToken = ownerTickToken,
            .eventBatchToken = eventBatchToken,
            .rawCurrent = KbmRawCurrentState{
                .providerGeneration = eventBatchToken,
                .contextRevision = bindings.contextRevision,
                .controlMapRevision = bindings.controlMapRevision,
                .physicalOnlyProvenance = false,
                .complete = false
            },
            .eventListComplete = events != nullptr
        };

        if (!events || !*events) {
            return observed;
        }

        std::uint32_t ordinal = 0;
        for (auto* current = *events; current; current = current->next) {
            ++ordinal;
            if (const auto* button = current->AsButtonEvent()) {
                KbmPhysicalDevice device{};
                if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                    device = KbmPhysicalDevice::Keyboard;
                } else if (button->GetDevice() == RE::INPUT_DEVICE::kMouse) {
                    device = KbmPhysicalDevice::Mouse;
                } else {
                    continue;
                }
                if (!(button->IsPressed() || button->IsUp())) {
                    continue;
                }
                observed.events.push_back(KbmObservedEventDraft{
                    .eventOrdinal = ordinal,
                    .producerTimestampUs = ownerNowUs,
                    .physical = KbmPhysicalCode{ device, button->GetIDCode() },
                    .phase = button->IsPressed() ? KbmEdgePhase::Press : KbmEdgePhase::Release,
                    .origin = KbmEdgeOrigin::Physical,
                    .initialPress = button->IsDown()
                });
                continue;
            }
            if (const auto* move = current->AsMouseMoveEvent()) {
                if (move->mouseInputX == 0 && move->mouseInputY == 0) {
                    continue;
                }
                observed.events.push_back(KbmObservedEventDraft{
                    .eventOrdinal = ordinal,
                    .producerTimestampUs = ownerNowUs,
                    .physical = KbmPhysicalCode{
                        KbmPhysicalDevice::Mouse,
                        kMouseDeltaPhysicalIdCode
                    },
                    .phase = KbmEdgePhase::MouseDelta,
                    .origin = KbmEdgeOrigin::Physical,
                    .deltaX = move->mouseInputX,
                    .deltaY = move->mouseInputY
                });
            }
        }
        return observed;
    }
}
