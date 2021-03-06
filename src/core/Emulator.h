#pragma once

#include <chrono>
#include <thread>
#include <vector>

#include "SDL.h"

#include "../Configurable.h"
#include "../Observer.h"
#include "../Snapshottable.h"

#include "../gui/AppUtils.h"
#include "../gui/UserMessage.h"

#include "APU.h"
#include "BusImpl.h"
#include "Cartridge.h"
#include "Component.h"
#include "CPU.h"
#include "Joypad.h"
#include "NES.h"
#include "PPU.h"

class Emulator final
{
public:
	Emulator();

	bool emu_is_paused = false, emu_is_running = false;

	Observer* gui;

	[[nodiscard]] bool PrepareLaunchOfGame(const std::string& rom_path);

	void LaunchGame();
	void Pause();
	void Reset();
	void Resume();
	void Stop();
	void LoadState();
	void SaveState();

	void AddObserver(Observer* observer);
	bool SetupSDLVideo(const void* window_handle);

	void SetWindowScale(unsigned scale) { nes.ppu->SetWindowScale(scale); }
	void SetWindowSize(unsigned width, unsigned height) { nes.ppu->SetWindowSize(width, height); }

	unsigned GetWindowScale() const { return nes.ppu->GetWindowScale(); }
	unsigned GetWindowHeight() const { return nes.ppu->GetWindowHeight(); }
	unsigned GetWindowWidth() const { return nes.ppu->GetWindowWidth(); }

	/* Currently, audio being enabled corresponds to capped framerate */
	bool FramerateIsCapped() { return nes.apu->AudioIsEnabled(); }
	void CapFramerate() { nes.apu->EnableAudio(); }
	void UncapFramerate() { nes.apu->DisableAudio(); }

	std::vector<Configurable*> GetConfigurableComponents() { return { nes.apu.get(), nes.joypad.get(), nes.ppu.get() }; }

private:
	const std::string save_state_path_postfix = "_SAVE_STATE.bin";

	bool load_state_on_next_cycle = false, save_state_on_next_cycle = false;

	NES nes;

	std::string current_rom_path;

	std::vector<Snapshottable*> snapshottable_components{};

	void EmulatorLoop();
};

