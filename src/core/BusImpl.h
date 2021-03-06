#pragma once

#include <array>

#include "../debug/Logging.h"

#include "APU.h"
#include "Bus.h"
#include "CPU.h"
#include "Joypad.h"
#include "PPU.h"

#include "mappers/BaseMapper.h"

class BusImpl final : public Bus, public Component
{
public:
	using Component::Component;

	void Reset() override;

	/* CPU reads/writes/waits, that also advances the state machine by one cycle */
	u8   ReadCycle(u16 addr)           override;
	void WaitCycle()                   override;
	void WriteCycle(u16 addr, u8 data) override;

	void StreamState(SerializationStream& stream) override;

private:
	std::array<u8, 0x800> ram{}; /* $0000-$07FF, mirrored until $1FFF */
	std::array<u8, 0x08> apu_io_test{}; /* $4018-$401F */

	u8 Read(u16 addr);
	void Write(u16 addr, u8 data);

	void UpdateLogging();
};

