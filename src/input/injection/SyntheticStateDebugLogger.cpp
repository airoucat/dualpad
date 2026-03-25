#include "pch.h"
#include "input/injection/SyntheticStateDebugLogger.h"
#include "input/RuntimeConfig.h"

namespace logger = SKSE::log;

namespace dualpad::input
{
    bool IsSyntheticStateDebugLogEnabled()
    {
        return RuntimeConfig::GetSingleton().LogSyntheticState();
    }

    void LogSyntheticPadFrame(const SyntheticPadFrame& frame)
    {
        if (!IsSyntheticStateDebugLogEnabled()) {
            return;
        }

        logger::debug(
            "[DualPad][Synthetic] seq={} firstSeq={} ctx={} down=0x{:08X} pressed=0x{:08X} released=0x{:08X} held=0x{:08X} transientPress=0x{:08X} transientRelease=0x{:08X} pulse=0x{:08X} tap=0x{:08X} hold=0x{:08X} layer=0x{:08X} combo=0x{:08X} coalesced={} overflowed={} gestures={} tpPress={} tpRelease={} tpSlide={} ls=({:.3f},{:.3f}) rs=({:.3f},{:.3f}) tr=({:.3f},{:.3f})",
            frame.sequence,
            frame.firstSequence,
            ToString(frame.context),
            frame.downMask,
            frame.pressedMask,
            frame.releasedMask,
            frame.heldMask,
            frame.transientPressedMask,
            frame.transientReleasedMask,
            frame.pulseMask,
            frame.tapMask,
            frame.holdMask,
            frame.layerMask,
            frame.comboMask,
            frame.coalesced,
            frame.overflowed,
            frame.gestureCount,
            frame.touchpadPressCount,
            frame.touchpadReleaseCount,
            frame.touchpadSlideCount,
            frame.leftStickX.value,
            frame.leftStickY.value,
            frame.rightStickX.value,
            frame.rightStickY.value,
            frame.leftTrigger.value,
            frame.rightTrigger.value);
    }
}
