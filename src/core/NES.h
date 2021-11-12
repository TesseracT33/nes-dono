#pragma once

class APU;
class BaseMapper;
class Bus;
class CPU;
class Joypad;
class PPU;

struct NES
{
	/* Note: these will be constructed from the Emulator class;
	   this struct cannot call their constructors, for that would lead to cyclic dependencies. */
	std::unique_ptr<APU> apu;
	std::unique_ptr<Bus> bus;
	std::unique_ptr<CPU> cpu;
	std::unique_ptr<Joypad> joypad;
	std::unique_ptr<PPU> ppu;

	std::shared_ptr<BaseMapper> mapper;
};