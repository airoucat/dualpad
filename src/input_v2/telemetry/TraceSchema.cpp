#include "pch.h"
#include "input_v2/telemetry/TraceSchema.h"

#include <algorithm>

namespace dualpad::input_v2::telemetry
{
    namespace
    {
        constexpr std::array<TraceFileSpec, 10> kPhase0TraceFiles = { {
            {
                "dispatcher_schedule.csv",
                "step_index,op,sequence,budget,reason,route_state,last_poll_age_ms,hook_installed,pending_before,pending_after,drained_count"
            },
            {
                "ingress_snapshot_frames.csv",
                "sequence,first_sequence,source_timestamp_us,context,context_epoch,overflowed,coalesced,cross_context_mismatch,digital_mask,left_stick_x,left_stick_y,right_stick_x,right_stick_y,left_trigger,right_trigger"
            },
            {
                "ingress_snapshot_events.csv",
                "sequence,event_index,type,trigger_type,code,modifier_mask,axis,previous_value,value,timestamp_us,touch_id,touch_x,touch_y,touchpad_mode,touch_region,slide_direction"
            },
            {
                "processed_snapshot_frames.csv",
                "sequence,first_sequence,source_timestamp_us,context,context_epoch,overflowed,coalesced,cross_context_mismatch,digital_mask,left_stick_x,left_stick_y,right_stick_x,right_stick_y,left_trigger,right_trigger"
            },
            {
                "processed_snapshot_events.csv",
                "sequence,event_index,type,trigger_type,code,modifier_mask,axis,previous_value,value,timestamp_us,touch_id,touch_x,touch_y,touchpad_mode,touch_region,slide_direction"
            },
            {
                "expected_authoritative_poll.csv",
                "poll_sequence,context,context_epoch,source_timestamp_us,down_mask,pressed_mask,released_mask,pulse_mask,unmanaged_down_mask,unmanaged_pressed_mask,unmanaged_released_mask,unmanaged_pulse_mask,managed_mask,committed_down_mask,committed_pressed_mask,committed_released_mask,move_x,move_y,look_x,look_y,left_trigger,right_trigger,has_digital,has_analog,overflowed,coalesced"
            },
            {
                "expected_keyboard_bridge.csv",
                "sequence,command_index,command_type,scancode,action_id,contract,context"
            },
            {
                "expected_presentation_surface.csv",
                "sequence,context,context_epoch,is_using_gamepad,gamepad_controls_cursor,gamepad_device_enabled,presentation_owner,cursor_owner,gameplay_engine_owner,gameplay_menu_entry_owner"
            },
            {
                "glyph_queries.csv",
                "query_id,sequence,action_id,context_name"
            },
            {
                "expected_glyph_results.csv",
                "query_id,ok,button_art_token,semantic_id,context_name"
            }
        } };
    }

    std::span<const TraceFileSpec> Phase0TraceFiles()
    {
        return kPhase0TraceFiles;
    }

    const TraceFileSpec* FindPhase0TraceFile(std::string_view fileName)
    {
        const auto found = std::ranges::find_if(
            kPhase0TraceFiles,
            [fileName](const TraceFileSpec& spec) {
                return spec.name == fileName;
            });

        return found == kPhase0TraceFiles.end() ? nullptr : &*found;
    }
}
