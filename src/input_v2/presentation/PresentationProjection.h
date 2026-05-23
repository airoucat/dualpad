#pragma once

#include "input_v2/actions/ActionSetResolver.h"
#include "input_v2/context/ContextResolver.h"
#include "input_v2/presentation/SourceEvidenceCollector.h"

#include <cstdint>

namespace dualpad::input_v2::presentation
{
    enum class PresentationOwner : std::uint8_t
    {
        KeyboardMouse = 0,
        Gamepad
    };

    enum class NavigationOwner : std::uint8_t
    {
        None = 0,
        KeyboardMouse,
        Gamepad
    };

    enum class CursorOwner : std::uint8_t
    {
        KeyboardMouse = 0,
        Gamepad
    };

    enum class PointerIntent : std::uint8_t
    {
        None = 0,
        HoverOnly,
        PointerActive
    };

    enum class GameplayPresentationReasonCode : std::uint8_t
    {
        None = 0,
        CoordinatorPublished,
        ExplicitResync
    };

    enum class PresentationDecisionReason : std::uint8_t
    {
        None = 0,
        GameplayEngineOwner,
        GameplayMenuEntryOwner,
        MenuSourceEvidence,
        RollbackCompatibilitySurface
    };

    enum class PresentationDirtyFlags : std::uint8_t
    {
        None = 0,
        Family = 1 << 0,
        Owner = 1 << 1,
        Cursor = 1 << 2,
        Context = 1 << 3,
        ActionSets = 1 << 4,
        Policy = 1 << 5
    };

    struct PublishedGameplayPresentation
    {
        PresentationOwner engineOwner{ PresentationOwner::KeyboardMouse };
        PresentationOwner menuEntryOwner{ PresentationOwner::KeyboardMouse };
        std::uint32_t gameplayPresentationRevision{ 0 };
        GameplayPresentationReasonCode reason{ GameplayPresentationReasonCode::None };
        std::uint64_t publishedTick{ 0 };
    };

    struct PublishedPresentationState
    {
        DeviceFamily family{ DeviceFamily::KeyboardMouse };
        std::uint32_t deviceFamilyRevision{ 0 };
        PresentationOwner owner{ PresentationOwner::KeyboardMouse };
        NavigationOwner navigationOwner{ NavigationOwner::KeyboardMouse };
        CursorOwner cursorOwner{ CursorOwner::KeyboardMouse };
        PointerIntent pointerIntent{ PointerIntent::None };
        context::UiContextId uiContextId{ context::UiContextId::None };
        actions::ActionSetStack actionSetStack;
        context::PresentationPolicyId presentationPolicyId;
        std::uint32_t contextRevision{ 0 };
        std::uint32_t gameplayPresentationRevision{ 0 };
        std::uint32_t epoch{ 0 };
        PresentationDirtyFlags dirty{ PresentationDirtyFlags::None };
        PresentationDecisionReason reason{ PresentationDecisionReason::None };
    };

    PresentationDirtyFlags operator|(PresentationDirtyFlags lhs, PresentationDirtyFlags rhs);
    PresentationDirtyFlags& operator|=(PresentationDirtyFlags& lhs, PresentationDirtyFlags rhs);
    bool HasDirtyFlag(PresentationDirtyFlags flags, PresentationDirtyFlags flag);

    class PresentationProjection
    {
    public:
        PublishedPresentationState Project(
            const SourceEvidenceSnapshot& evidence,
            const context::ResolvedContextSnapshot& contextSnapshot,
            const PublishedGameplayPresentation& gameplay);
        const PublishedPresentationState& GetPublished() const;
        void ResetForTests();

    private:
        PublishedPresentationState _published{};
    };
}
