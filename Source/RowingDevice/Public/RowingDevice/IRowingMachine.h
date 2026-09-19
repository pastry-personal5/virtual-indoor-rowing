#pragma once

#include "RowingDevice/RowingMachineTypes.h"

#include <memory>

class IRowingMachine
{
  public:
	virtual ~IRowingMachine() = default;

	virtual FRowingCommandResult Connect() = 0;
	virtual FRowingCommandResult Disconnect() = 0;
	virtual ERowingConnectionState GetConnectionState() const = 0;
	virtual FRowingMachineDiagnostics GetDiagnostics() const = 0;
	virtual bool TryPollEvent(FRowingMachineEvent &OutEvent) = 0;
};

class IRowingMachineDiscovery
{
  public:
	virtual ~IRowingMachineDiscovery() = default;

	virtual FRowingCommandResult StartScan() = 0;
	virtual FRowingCommandResult StopScan() = 0;
	virtual bool TryPollDiscoveryEvent(FRowingMachineEvent &OutEvent) = 0;
	virtual std::unique_ptr<IRowingMachine>
	CreateMachine(const FRowingMachineId &MachineId) = 0;
};

// Discovery whose adapter remembers the last machine and, once the transport is
// ready, starts reconnecting to it on its own (Phase 1 Milestone 7). The machine
// it starts is handed to exactly one owner through TryTakeRelaunchMachine; the
// discovery must outlive every machine it created or handed over.
class IRememberingMachineDiscovery : public IRowingMachineDiscovery
{
  public:
	virtual std::unique_ptr<IRowingMachine> TryTakeRelaunchMachine() = 0;
	// Clears only the adapter-private remembered-machine preference. The caller
	// owns and must disconnect any machine already transferred from discovery.
	virtual void ForgetRememberedMachine() = 0;
};
