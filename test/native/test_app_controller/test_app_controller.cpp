#include <unity.h>

#include "AppControllerTestSupport.h"
#include "app/AppController.h"
#include "app/MeteringSchemeStore.h"
#include "../support/MemoryFileBackend.h"
#include "../support/MemoryRecordWriter.h"

#include <cstdio>
#include <vector>

using namespace faucet;
using namespace faucet_test;
using faucet_test::MemoryFileBackend;
using faucet_test::MemoryRecordWriter;

namespace {

bool gValveSinkSawClosedBeforeRecordAppend = false;
bool gRecordAppendObservedClosedValve = false;

class ObservingRecordWriter : public WaterRecordWriter {
public:
    std::vector<WaterRecord> records;

    bool append(const WaterRecord& record) override {
        gRecordAppendObservedClosedValve = gValveSinkSawClosedBeforeRecordAppend;
        records.push_back(record);
        return true;
    }
};

}  // namespace

#include "AppControllerSensorAndTdsTests.inc"

#include "AppControllerWaterRunTests.inc"

#include "AppControllerFlowCalibrationTests.inc"

#include "AppControllerRuntimeTests.inc"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_app_controller_uses_active_scheme_parameters_for_flow_meter);
    RUN_TEST(test_app_snapshot_contains_water_sensor_snapshot);
    RUN_TEST(test_temperature_reference_calibration_sets_offset_from_raw_temperature);
    RUN_TEST(test_temperature_reference_calibration_rejects_disabled_temperature_sensor);
    RUN_TEST(test_app_records_sensor_summary_on_completed_run);
    RUN_TEST(test_app_rejects_tds_calibration_when_running);
    RUN_TEST(test_app_tds_point_calibration_apply_persists_to_config);
    RUN_TEST(test_app_expires_idle_tds_calibration_session_from_tick);
    RUN_TEST(test_app_controller_successful_record_writes_scheme_id);
    RUN_TEST(test_app_controller_starts_after_double_ok_and_opens_valve);
    RUN_TEST(test_app_controller_stale_cancel_fast_path_does_not_block_confirm_ok_start);
    RUN_TEST(test_app_controller_cancel_raw_dominates_pending_ok_release);
    RUN_TEST(test_app_controller_confirm_and_running_start_volume_stays_zero_until_first_pulse);
    RUN_TEST(test_app_controller_completion_writes_record_statistics_and_filters);
    RUN_TEST(test_app_controller_pushes_closed_valve_output_before_record_persistence);
    RUN_TEST(test_app_controller_web_preset_switch_during_run_updates_next_preset_only);
    RUN_TEST(test_app_controller_local_plus_does_not_switch_preset_while_running);
    RUN_TEST(test_app_controller_offline_completion_marks_unknown_time_with_boot_id);
    RUN_TEST(test_app_controller_offline_start_sync_before_completion_writes_real_time);
    RUN_TEST(test_app_controller_pause_resume_then_completion_updates_persistence_once);
    RUN_TEST(test_app_controller_stop_down_closes_valve_and_records_user_stop);
    RUN_TEST(test_app_controller_normal_output_does_not_collect_ram_pulse_trace);
    RUN_TEST(test_app_controller_emergency_stop_closes_valve_without_debounce);
    RUN_TEST(test_app_controller_saves_active_task_config_for_next_task);
    RUN_TEST(test_app_controller_latest_pending_system_config_wins);
    RUN_TEST(test_app_controller_idle_save_supersedes_pending_system_config);
    RUN_TEST(test_app_controller_result_display_zero_exits_result_page_on_config_apply);
    RUN_TEST(test_app_controller_applied_valve_and_no_flow_settings_control_runtime);
    RUN_TEST(test_app_controller_emits_beep_patterns_for_actions_and_completion);
    RUN_TEST(test_app_controller_reports_record_write_failure_without_losing_statistics);
    RUN_TEST(test_app_controller_adjusts_volume_target_with_configured_step_without_ok_long_toggle);
    RUN_TEST(test_app_controller_adjusts_time_target_with_configured_step);
    RUN_TEST(test_app_controller_stopped_volume_does_not_clamp_next_confirm_adjustment);
    RUN_TEST(test_app_controller_starting_calibration_from_idle_waits_for_local_run);
    RUN_TEST(test_app_controller_starting_calibration_while_running_is_rejected);
    RUN_TEST(test_app_controller_starting_calibration_twice_is_rejected);
    RUN_TEST(test_app_controller_flow_calibration_session_does_not_time_out);
    RUN_TEST(test_app_controller_generated_calibration_restores_candidate_and_stays_active_without_idle_timeout);
    RUN_TEST(test_app_controller_reboot_drops_awaiting_actual_when_ram_trace_missing);
    RUN_TEST(test_app_controller_local_ok_starts_calibration_run_and_completion_awaits_actual);
    RUN_TEST(test_app_controller_local_cancel_exits_calibration_while_awaiting_actual);
    RUN_TEST(test_app_controller_saves_long_high_pulse_calibration_without_bucket_overflow);
    RUN_TEST(test_app_controller_bucket_overflow_completion_still_awaits_actual);
    RUN_TEST(test_app_controller_calibration_run_ignores_preset_target_until_local_cancel);
    RUN_TEST(test_app_controller_generates_calibration_session_candidate);
    RUN_TEST(test_app_controller_auto_generates_after_second_valid_calibration_sample);
    RUN_TEST(test_app_controller_saves_unstable_actual_without_counting_valid_sample);
    RUN_TEST(test_app_controller_accepts_bucket_overflow_calibration_when_summary_is_stable);
    RUN_TEST(test_app_controller_saves_incomplete_bucket_overflow_sample_without_generation_use);
    RUN_TEST(test_app_controller_generated_calibration_can_continue_collecting_samples);
    RUN_TEST(test_app_controller_submit_actual_succeeds_when_auto_refresh_cannot_generate);
    RUN_TEST(test_app_controller_pending_actual_sample_can_be_skipped);
    RUN_TEST(test_app_controller_applies_generated_session_scheme_and_keeps_old_scheme);
    RUN_TEST(test_app_controller_regenerates_applied_candidate_from_stored_traces);
    RUN_TEST(test_app_controller_pause_timeout_trace_is_not_marked_error_and_can_calibrate);
    RUN_TEST(test_app_controller_result_display_exits_after_configured_timeout);
    RUN_TEST(test_app_controller_result_ok_hold_stays_on_result);
    RUN_TEST(test_app_controller_snapshot_reports_current_flow_rate);
    RUN_TEST(test_app_controller_uses_window_flow_for_high_flow_safety);
    return UNITY_END();
}
