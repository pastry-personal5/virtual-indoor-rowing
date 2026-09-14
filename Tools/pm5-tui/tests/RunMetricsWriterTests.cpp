#include "RunMetricsWriter.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
	const std::filesystem::path TestDirectory =
		std::filesystem::temp_directory_path() /
		("vir-pm5-tui-metrics-" +
		 std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now().time_since_epoch())
							.count()));
	std::filesystem::path MetricsPath;
	{
		FPM5HardwareProbeConfiguration ProbeConfiguration;
		ProbeConfiguration.CaptureRawTelemetry = true;
		PM5Tui::FRunMetricsWriter Writer(
			TestDirectory, "test-revision", ProbeConfiguration);
		assert(Writer.IsAvailable());
		MetricsPath = Writer.CurrentMetricsPath();

		PM5Tui::FRunMetricsSummary Summary;
		Summary.SampleCount = 7;
		Summary.StrokeMetricsRecordCount = 2;
		Summary.CorrectionCount = 2;
		Summary.StaleEventCount = 1;
		Summary.FaultCount = 3;
		Summary.DuplicateSampleCount = 4;
		Summary.SourceGapSampleCount = 5;
		Summary.TimeRegressionSampleCount = 6;
		Summary.DistanceRegressionSampleCount = 7;
		Summary.MissingFieldSampleCount = 8;
		Summary.LastSampleSequence = 42;
		Summary.ReconnectCount = 2;
		Summary.TotalReconnectGapMs = 3100;
		Summary.LongestReconnectGapMs = 2400;
		Summary.AcquisitionQueue = PM5Tui::FQueueMetricsSummary{1, 512, 9, 0};
		Summary.EventQueue = PM5Tui::FQueueMetricsSummary{2, 512, 10, 1};
		FPM5RunDiagnostics PM5;
		PM5.RequestedStatusPeriodMs = 100;
		PM5.StatusRateWriteAttemptCount = 1;
		PM5.StatusRateWriteSuccessCount = 1;
		Concept2PM::FPM5CharacteristicDiagnostics GeneralStatus;
		GeneralStatus.Characteristic = Concept2PM::GeneralStatus;
		GeneralStatus.PropertiesObserved = true;
		GeneralStatus.ObservedProperties =
			Concept2PM::ToPM5CharacteristicProperties(
				Concept2PM::EPM5CharacteristicProperty::Notify);
		GeneralStatus.NotificationEnableAttemptCount = 1;
		GeneralStatus.NotificationEnableSuccessCount = 1;
		GeneralStatus.ApprovedPacketLengths = {19};
		GeneralStatus.RecordNotification(1'000'000'000ULL, 19);
		GeneralStatus.RecordNotification(1'100'000'000ULL, 19);
		GeneralStatus.RecordNotification(1'750'000'000ULL, 19);
		GeneralStatus.RecordParserError(
			Concept2PM::EPacketError::LengthNotApproved, 18, 1'800'000'000ULL);
		PM5.Characteristics.push_back(GeneralStatus);
		Concept2PM::FPM5CharacteristicDiagnostics StatusRate;
		StatusRate.Characteristic = Concept2PM::StatusSampleRate;
		StatusRate.PropertiesObserved = true;
		StatusRate.ObservedProperties =
			Concept2PM::ToPM5CharacteristicProperties(
				Concept2PM::EPM5CharacteristicProperty::Read) |
			Concept2PM::ToPM5CharacteristicProperties(
				Concept2PM::EPM5CharacteristicProperty::Write);
		PM5.Characteristics.push_back(StatusRate);
		FPM5CallbackErrorDiagnostic CallbackError;
		CallbackError.Stage = EPM5CallbackStage::NotificationSubscription;
		CallbackError.Characteristic = Concept2PM::GeneralStatus;
		CallbackError.Count = 1;
		CallbackError.HasError = true;
		CallbackError.LastErrorDomain = EPM5ErrorDomain::CoreBluetoothATT;
		CallbackError.LastErrorCode = 5;
		CallbackError.LastErrorMonotonicNs = 1'800'000'000ULL;
		PM5.CallbackErrors.push_back(CallbackError);
		PM5.ProbeCapture.Enabled = true;
		PM5.ProbeCapture.Active = false;
		PM5.ProbeCapture.StartedMonotonicNs = 900'000'000ULL;
		PM5.ProbeCapture.MaxCaptureDurationMs = 3'900'000;
		PM5.ProbeCapture.MaxCapturedPacketCount = 100'000;
		PM5.ProbeCapture.MaxCapturedPayloadBytes = 64;
		PM5.ProbeCapture.EvidenceQueueCapacity = 4'096;
		PM5.ProbeCapture.EvidenceQueueHighWaterMark = 2;
		PM5.ProbeCapture.ObservedPacketCount = 1;
		PM5.ProbeCapture.CapturedPacketCount = 1;
		PM5.ProbeCapture.CapturedPayloadByteCount = 15;
		PM5.ProbeCapture.StopReason = EPM5ProbeCaptureStopReason::RunStopped;
		Summary.PM5Diagnostics = PM5;
		FRowingStrokeMetrics Stroke;
		Stroke.Source = ERowingStrokeMetricsSource::KinematicsAndForce;
		Stroke.SourceElapsedMs = 123450;
		Stroke.StrokeCount = 1234;
		Stroke.CumulativeDistanceMm = 678900;
		Stroke.DriveLengthMm = 1490;
		Stroke.DriveTimeMs = 1280;
		Stroke.RecoveryTimeMs = 2580;
		Stroke.StrokeDistanceMm = 1000;
		Stroke.PeakDriveForceDeciLb = 250;
		Stroke.AverageDriveForceDeciLb = 150;
		Stroke.WorkPerStrokeDeciJoules = 10000;
		Writer.RecordStrokeMetrics(Stroke, 1'234'567'890ULL);
		FPM5ProbePacketEvidence Evidence;
		Evidence.PacketSequence = 17;
		Evidence.CharacteristicSequence = 3;
		Evidence.Characteristic = Concept2PM::AdditionalStrokeData;
		Evidence.ReceivedMonotonicNs = 1'900'000'000ULL;
		Evidence.ConnectionState = ERowingConnectionState::Ready;
		Evidence.ParserResult = Concept2PM::EPacketError::LengthNotApproved;
		Evidence.ApprovedPacketLengths = {18};
		Evidence.OriginalPayloadLength = 15;
		Evidence.PayloadBytes = {
			0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
		Writer.RecordProbePacket(Evidence);
		Writer.RecordAction(PM5Tui::EPM5TuiRunAction::ScanStarted);
		Writer.RecordRunStopped(Summary);
	}

	std::ifstream Input(MetricsPath);
	const std::string Contents(
		(std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
	assert(Contents.find("\"event\":\"run_started\"") != std::string::npos);
	assert(Contents.find("\"schema_version\":4") != std::string::npos);
	assert(Contents.find("\"raw_telemetry_capture\":true") != std::string::npos);
	assert(Contents.find("\"event\":\"raw_telemetry_packet\"") !=
		   std::string::npos);
	assert(Contents.find("\"packet_sequence\":17") != std::string::npos);
	assert(Contents.find("\"characteristic_sequence\":3") != std::string::npos);
	assert(Contents.find("\"characteristic_short_id\":\"0x0036\"") !=
		   std::string::npos);
	assert(Contents.find("\"parser_result\":\"length_not_approved\"") !=
		   std::string::npos);
	assert(Contents.find("\"approved_packet_lengths\":[18]") !=
		   std::string::npos);
	assert(Contents.find("\"payload_length\":15") != std::string::npos);
	assert(Contents.find(
			   "\"payload_hex\":\"0102030405060708090a0b0c0d0e0f\"") !=
		   std::string::npos);
	assert(Contents.find("\"event\":\"user_action\"") != std::string::npos);
	assert(Contents.find("\"action\":\"scan_started\"") != std::string::npos);
	assert(Contents.find("\"sample_count\":7") != std::string::npos);
	assert(Contents.find("\"stroke_metrics_record_count\":2") != std::string::npos);
	assert(Contents.find("\"correction_count\":2") != std::string::npos);
	assert(Contents.find("\"stale_event_count\":1") != std::string::npos);
	assert(Contents.find("\"fault_count\":3") != std::string::npos);
	assert(Contents.find("\"duplicate_sample_count\":4") != std::string::npos);
	assert(Contents.find("\"source_gap_sample_count\":5") != std::string::npos);
	assert(Contents.find("\"time_regression_sample_count\":6") != std::string::npos);
	assert(Contents.find("\"distance_regression_sample_count\":7") != std::string::npos);
	assert(Contents.find("\"missing_field_sample_count\":8") != std::string::npos);
	assert(Contents.find("\"last_sample_sequence\":42") != std::string::npos);
	assert(Contents.find("\"reconnect_count\":2") != std::string::npos);
	assert(Contents.find("\"total_reconnect_gap_ms\":3100") != std::string::npos);
	assert(Contents.find("\"longest_reconnect_gap_ms\":2400") != std::string::npos);
	assert(Contents.find(
			   "\"acquisition_queue\":{\"current_depth\":1,\"capacity\":512,\"high_water_mark\":9,\"overflow_count\":0}") !=
		   std::string::npos);
	assert(Contents.find(
			   "\"event_queue\":{\"current_depth\":2,\"capacity\":512,\"high_water_mark\":10,\"overflow_count\":1}") !=
		   std::string::npos);
	assert(Contents.find("\"event\":\"run_stopped\"") != std::string::npos);
	assert(Contents.find("\"event\":\"stroke_metrics\"") != std::string::npos);
	assert(Contents.find("\"source\":\"kinematics_and_force\"") != std::string::npos);
	assert(Contents.find("\"source_elapsed_ms\":123450") != std::string::npos);
	assert(Contents.find("\"stroke_count\":1234") != std::string::npos);
	assert(Contents.find("\"cumulative_distance_mm\":678900") != std::string::npos);
	assert(Contents.find("\"drive_length_mm\":1490") != std::string::npos);
	assert(Contents.find("\"peak_drive_force_deci_lb\":250") != std::string::npos);
	assert(Contents.find("\"work_per_stroke_deci_joules\":10000") != std::string::npos);
	assert(Contents.find("\"monotonic_timestamp_ns\":1234567890") != std::string::npos);
	assert(Contents.find("\"requested_status_period_ms\":100") != std::string::npos);
	assert(Contents.find("\"characteristic_short_id\":\"0x0031\"") != std::string::npos);
	assert(Contents.find("\"properties_observed\":true") != std::string::npos);
	assert(Contents.find("\"observed_properties\":[\"notify\"]") != std::string::npos);
	assert(Contents.find("\"characteristic_short_id\":\"0x0034\"") != std::string::npos);
	assert(Contents.find("\"observed_properties\":[\"read\",\"write\"]") != std::string::npos);
	assert(Contents.find(
			   "\"stage\":\"notification_subscription\",\"characteristic_short_id\":\"0x0031\",\"count\":1,\"error_domain\":\"core_bluetooth_att\",\"error_code\":5") !=
		   std::string::npos);
	assert(Contents.find("\"last_error_monotonic_ns\":1800000000") != std::string::npos);
	assert(Contents.find("\"notification_count\":3") != std::string::npos);
	assert(Contents.find("\"cadence_class\":\"continuous_status\"") !=
		   std::string::npos);
	assert(Contents.find(
			   "\"observed_packet_lengths\":[{\"packet_length\":19,\"count\":3}]") !=
		   std::string::npos);
	assert(Contents.find("\"notification_enable_attempt_count\":1") != std::string::npos);
	assert(Contents.find("\"notification_enable_success_count\":1") != std::string::npos);
	assert(Contents.find("\"min_interval_ms\":100") != std::string::npos);
	assert(Contents.find("\"max_interval_ms\":650") != std::string::npos);
	assert(Contents.find("\"p50_interval_ms\":105") != std::string::npos);
	assert(Contents.find("\"p95_interval_ms\":655") != std::string::npos);
	assert(Contents.find("\"long_gap_count\":1") != std::string::npos);
	assert(Contents.find("\"approved_packet_lengths\":[19]") != std::string::npos);
	assert(Contents.find(
			   "\"last_parser_error\":{\"code\":\"length_not_approved\",\"actual_packet_length\":18,\"monotonic_timestamp_ns\":1800000000}") !=
		   std::string::npos);
	assert(Contents.find("\"length_not_approved\":1") != std::string::npos);
	assert(Contents.find("\"probe_capture\":{\"enabled\":true") !=
		   std::string::npos);
	assert(Contents.find("\"captured_packet_count\":1") != std::string::npos);
	assert(Contents.find("\"stop_reason\":\"run_stopped\"") !=
		   std::string::npos);
	assert(Contents.find("\"observed_at_utc\":\"20") != std::string::npos);

	const auto FilePermissions = std::filesystem::status(MetricsPath).permissions();
	assert((FilePermissions & (std::filesystem::perms::group_all |
							   std::filesystem::perms::others_all)) ==
		   std::filesystem::perms::none);

	const std::filesystem::path OrdinaryDirectory = TestDirectory / "ordinary";
	std::filesystem::path OrdinaryMetricsPath;
	{
		PM5Tui::FRunMetricsWriter Writer(OrdinaryDirectory, "test-revision");
		OrdinaryMetricsPath = Writer.CurrentMetricsPath();
		FPM5ProbePacketEvidence Evidence;
		Evidence.PacketSequence = 1;
		Evidence.Characteristic = Concept2PM::AdditionalStrokeData;
		Evidence.OriginalPayloadLength = 3;
		Evidence.PayloadBytes = {0xaa, 0xbb, 0xcc};
		Writer.RecordProbePacket(Evidence);
		Writer.RecordRunStopped({});
	}
	std::ifstream OrdinaryInput(OrdinaryMetricsPath);
	const std::string OrdinaryContents(
		(std::istreambuf_iterator<char>(OrdinaryInput)),
		std::istreambuf_iterator<char>());
	assert(OrdinaryContents.find("\"raw_telemetry_capture\":false") !=
		   std::string::npos);
	assert(OrdinaryContents.find("\"event\":\"raw_telemetry_packet\"") ==
		   std::string::npos);
	std::filesystem::remove_all(TestDirectory);
	return 0;
}
