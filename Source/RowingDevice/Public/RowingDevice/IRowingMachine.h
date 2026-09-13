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
