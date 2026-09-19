#pragma once

#include "RowingDevice/IRowingMachine.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Real-device connection flow for the app (Phase 1 Milestone 7,
// docs/phase-1/07-milestone-7-real-pm5-app-wiring.md). Engine-independent,
// single-threaded and poll-driven, like FWorkoutSession: the caller owns the clock
// and calls Tick() on the same monotonic timebase as the device events.
//
// The connector never touches a transport until Start(): creating the discovery is
// what makes the platform adapter ask for Bluetooth permission, so a normal launch
// must not create one. After Start() the adapter reconnects to the remembered
// machine by itself once the transport is ready; if none arrives within the grace
// period, or the adapter reports the remembered machine unavailable, the connector
// scans. Apple types stop at the IRememberingMachineDiscovery seam.

enum class EDeviceConnectPhase : std::uint8_t
{
	// No discovery exists.
	Idle,
	// Discovery exists; waiting for the transport and the remembered machine.
	Starting,
	// Scanning (or finished scanning) with a candidate list.
	Scanning,
	// A machine is attached; its connection state is the session's to report.
	Attached
};

enum class EDeviceProblem : std::uint8_t
{
	None,
	// The user has not allowed Bluetooth for this app.
	BluetoothPermission,
	// Bluetooth is off, unavailable, or did not become ready.
	BluetoothNotReady,
	// A scan finished with no PM5 found.
	NoDeviceFound,
	// Any other discovery-level failure.
	DiscoveryFailed,
	// This build cannot use Bluetooth at all (the Unreal Editor has no Bluetooth
	// usage description, so touching CoreBluetooth would terminate it). Reported by
	// the controller before the journal or the transport is touched.
	TransportUnavailable
};

struct FDeviceCandidate
{
	FRowingMachineDescriptor Descriptor;
	// Stable for this candidate while the discovery lives (unlike its list position,
	// which changes whenever a signal reading re-sorts the list); what a click selects by.
	std::uint64_t Token = 0;
	// "PM5 #1 (-52 dBm)": index and signal strength only. The adapter's label and
	// identifier are deliberately not shown (no privacy change).
	std::string Label;
};

struct FDeviceConnectorConfig
{
	// How long Start() waits for the adapter's own remembered-machine reconnect
	// before scanning. Scanning before the transport is ready cancels that reconnect.
	std::uint64_t RememberedGraceNs = 3'000'000'000ULL;
	// Bound on the candidate list, so a crowded room cannot grow it without limit.
	std::size_t MaxCandidates = 16;
};

class FDeviceConnector final
{
  public:
	using FDiscoveryFactory = std::function<std::unique_ptr<IRememberingMachineDiscovery>()>;

	explicit FDeviceConnector(FDiscoveryFactory InFactory, FDeviceConnectorConfig InConfig = {});
	// Tears down in the order the adapter requires: machine, then discovery.
	~FDeviceConnector();

	FDeviceConnector(const FDeviceConnector &) = delete;
	FDeviceConnector &operator=(const FDeviceConnector &) = delete;

	// Idle -> Starting: creates the discovery. Returns false if it is not Idle or the
	// factory produced nothing.
	bool Start(std::uint64_t NowNs);
	// Drains discovery events and adopts a remembered machine. Never touches the
	// attached machine's event queue: the caller polls that.
	void Tick(std::uint64_t NowNs);

	// Scans for other PMs. From Attached this drops the current machine first.
	bool StartScan(std::uint64_t NowNs);
	// Connects to the candidate at the current (nearest-first) index.
	bool Select(std::size_t Index);
	// Connects to the candidate with this token; false when it has left the list.
	bool SelectByToken(std::uint64_t Token);
	// Disconnects and drops the machine and forgets the remembered preference. The
	// discovery stays, so the caller can scan again.
	void Forget();
	// Full teardown back to Idle.
	void Stop();

	EDeviceConnectPhase GetPhase() const noexcept
	{
		return Phase;
	}
	EDeviceProblem GetProblem() const noexcept
	{
		return Problem;
	}
	const std::vector<FDeviceCandidate> &GetCandidates() const noexcept
	{
		return Candidates;
	}
	// The attached machine, or null. Owned by the connector; valid until the next
	// call that can drop it (StartScan, Forget, Stop, destruction).
	IRowingMachine *GetMachine() const noexcept
	{
		return Machine.get();
	}
	// Increases whenever the attached machine appears or is dropped; the caller
	// creates its session when this changes and drops it before the machine goes away.
	std::uint64_t GetMachineGeneration() const noexcept
	{
		return MachineGeneration;
	}
	// Increases on any change a panel would render: phase, problem, candidates.
	std::uint64_t GetGeneration() const noexcept
	{
		return Generation;
	}

	static std::string MakeCandidateLabel(std::size_t OneBasedIndex, const std::optional<std::int32_t> &SignalStrengthDbm);

  private:
	void DropMachine();
	void BeginScan();
	void RebuildCandidateLabels();
	void HandleDiscoveryEvent(const FRowingMachineEvent &Event);
	void SetPhase(EDeviceConnectPhase NewPhase);
	void SetProblem(EDeviceProblem NewProblem);

	FDiscoveryFactory Factory;
	FDeviceConnectorConfig Config;
	// Declared before Machine so the machine is destroyed first.
	std::unique_ptr<IRememberingMachineDiscovery> Discovery;
	std::unique_ptr<IRowingMachine> Machine;

	EDeviceConnectPhase Phase = EDeviceConnectPhase::Idle;
	EDeviceProblem Problem = EDeviceProblem::None;
	std::vector<FDeviceCandidate> Candidates;
	std::uint64_t StartedNs = 0;
	std::uint64_t MachineGeneration = 0;
	std::uint64_t Generation = 0;
	std::uint64_t NextCandidateToken = 1;
};
