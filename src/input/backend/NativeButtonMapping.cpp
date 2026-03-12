#include "pch.h"
#include "input/backend/NativeButtonMapping.h"

#include <memory>

namespace dualpad::input::backend
{
    namespace
    {
        struct NativeButtonBinding
        {
            NativeControlCode control{ NativeControlCode::None };
            std::string_view userEvent{};
            RE::ControlMap::InputContextID context{ RE::UserEvents::INPUT_CONTEXT_ID::kGameplay };
            std::uint16_t fallbackGamepadButton{ RE::ControlMap::kInvalid };
        };

        constexpr NativeButtonBinding kBindings[] = {
            { NativeControlCode::Jump, "Jump", RE::UserEvents::INPUT_CONTEXT_ID::kGameplay, 0x8000 },
            { NativeControlCode::Attack, "Right Attack/Block", RE::UserEvents::INPUT_CONTEXT_ID::kGameplay, 0x1000 },
            { NativeControlCode::Block, "Left Attack/Block", RE::UserEvents::INPUT_CONTEXT_ID::kGameplay, 0x2000 },
            { NativeControlCode::Activate, "Activate", RE::UserEvents::INPUT_CONTEXT_ID::kGameplay, 0x1000 },
            { NativeControlCode::Sprint, "Sprint", RE::UserEvents::INPUT_CONTEXT_ID::kGameplay, 0x0100 },
            { NativeControlCode::Sneak, "Sneak", RE::UserEvents::INPUT_CONTEXT_ID::kGameplay, 0x0040 },
            { NativeControlCode::Shout, "Shout", RE::UserEvents::INPUT_CONTEXT_ID::kGameplay, 0x0200 },
            { NativeControlCode::MenuConfirm, "Accept", RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode, 0x1000 },
            { NativeControlCode::MenuCancel, "Cancel", RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode, 0x2000 },
            { NativeControlCode::MenuScrollUp, "Up", RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode, 0x0001 },
            { NativeControlCode::MenuScrollDown, "Down", RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode, 0x0002 },
            { NativeControlCode::MenuLeft, "Left", RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode, 0x0004 },
            { NativeControlCode::MenuRight, "Right", RE::UserEvents::INPUT_CONTEXT_ID::kMenuMode, 0x0008 },
            { NativeControlCode::MenuPageUp, "PrevPage", RE::UserEvents::INPUT_CONTEXT_ID::kBook, 0x0100 },
            { NativeControlCode::MenuPageDown, "NextPage", RE::UserEvents::INPUT_CONTEXT_ID::kBook, 0x0200 }
        };

        const NativeButtonBinding* FindBinding(NativeControlCode control)
        {
            for (const auto& binding : kBindings) {
                if (binding.control == control) {
                    return std::addressof(binding);
                }
            }

            return nullptr;
        }
    }

    std::uint16_t ResolveMappedGamepadButton(NativeControlCode control)
    {
        const auto* binding = FindBinding(control);
        if (!binding) {
            return 0;
        }

        auto* controlMap = RE::ControlMap::GetSingleton();
        if (!controlMap) {
            return binding->fallbackGamepadButton;
        }

        const auto mapped = controlMap->GetMappedKey(
            binding->userEvent,
            RE::INPUT_DEVICE::kGamepad,
            binding->context);
        if (mapped != RE::ControlMap::kInvalid && mapped != 0xFF) {
            return static_cast<std::uint16_t>(mapped);
        }

        return binding->fallbackGamepadButton;
    }
}
