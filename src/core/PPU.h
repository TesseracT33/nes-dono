#pragma once

#include "SDL.h"

#include <array>
#include <format>
#include <limits>
#include <stdexcept>
#include <vector>

#include "../Observer.h"
#include "../Types.h"

#include "../gui/UserMessage.h"

#include "../debug/Logging.h"

#include "Bus.h"
#include "Component.h"
#include "CPU.h"
#include "System.h"

#include "mappers/BaseMapper.h"

class PPU final : public Component
{
public:
	using Component::Component;
	~PPU();
	PPU(const PPU& other) = delete;
	PPU(PPU&& other) = delete;

	PPU& operator=(const PPU& other) = delete;
	PPU& operator=(PPU&& other) = delete;

	Observer* gui;

	unsigned GetWindowScale()  const { return window_scale; }
	unsigned GetWindowHeight() const { return standard.num_visible_scanlines * window_scale; }
	unsigned GetWindowWidth()  const { return num_pixels_per_scanline * window_scale; }

	[[nodiscard]] bool CreateRenderer(const void* window_handle);
	void PowerOn(const System::VideoStandard standard);
	void Reset();
	void Update();

	// writing and reading done by the CPU to/from the registers at $2000-$2007, $4014
	u8 ReadRegister(u16 addr);
	void WriteRegister(u16 addr, u8 data);

	void SetWindowScale(unsigned scale);
	void SetWindowSize(unsigned width, unsigned height);

	void StreamState(SerializationStream& stream) override;
	void StreamConfig(SerializationStream& stream) override;
	void SetDefaultConfig() override;

private:
	enum class TileType { BG, OBJ };

	/* PPU operation details that are affected by the video standard (NTSC/PAL/Dendy): */
	struct Standard
	{
		bool oam_can_be_written_to_during_forced_blanking;
		bool pre_render_line_is_one_dot_shorter_on_every_other_frame;
		float dots_per_cpu_cycle;
		int nmi_scanline;
		int num_scanlines;
		int num_scanlines_per_vblank;
		int num_visible_scanlines;
	} standard = NTSC;

	static constexpr Standard NTSC  = {  true,  true, 3.0f, 241, 262, 20, 240 };
	static constexpr Standard PAL   = { false, false, 3.2f, 240, 312, 70, 239 };
	static constexpr Standard Dendy = {  true, false, 3.0f, 290, 312, 20, 239 };

	static constexpr int default_window_scale = 3;
	static constexpr int num_colour_channels = 3;
	static constexpr int num_cycles_per_scanline = 341; // On NTSC: is actually 340 on the pre-render scanline if on an odd-numbered frame
	static constexpr int num_pixels_per_scanline = 256; // Horizontal resolution
	static constexpr int pre_render_scanline = -1;

	// https://wiki.nesdev.org/w/index.php?title=PPU_palettes#2C02
	const std::array<SDL_Color, 64> palette = { {
		{ 84,  84,  84}, {  0,  30, 116}, {  8,  16, 144}, { 48,   0, 136}, { 68,   0, 100}, { 92,   0,  48}, { 84,   4,   0}, { 60,  24,   0},
		{ 32,  42,   0}, {  8,  58,   0}, {  0,  64,   0}, {  0,  60,   0}, {  0,  50,  60}, {  0,   0,   0}, {  0,   0,   0}, {  0,   0,   0},
		{152, 150, 152}, {  8,  76, 196}, { 48,  50, 236}, { 92,  30, 228}, {136,  20, 176}, {160,  20, 100}, {152,  34,  32}, {120,  60,   0},
		{ 84,  90,   0}, { 40, 114,   0}, {  8, 124,   0}, {  0, 118,  40}, {  0, 102, 120}, {  0,   0,   0}, {  0,   0,   0}, {  0,   0,   0},
		{236, 238, 236}, { 76, 154, 236}, {120, 124, 236}, {176,  98, 236}, {228,  84, 236}, {236,  88, 180}, {236, 106, 100}, {212, 136,  32},
		{160, 170,   0}, {116, 196,   0}, { 76, 208,  32}, { 56, 204, 108}, { 56, 180, 204}, { 60,  60,  60}, {  0,   0,   0}, {  0,   0,   0},
		{236, 238, 236}, {168, 204, 236}, {188, 188, 236}, {212, 178, 236}, {236, 174, 236}, {236, 174, 212}, {236, 180, 176}, {228, 194, 144},
		{204, 210, 120}, {180, 222, 120}, {168, 226, 144}, {152, 226, 180}, {160, 214, 228}, {160, 162, 160}, {  0,   0,   0}, {  0,   0,   0}
	} };

	const std::array<u8, 0x20> palette_ram_on_powerup = { /* Source: blargg_ppu_tests_2005.09.15b */
		0x09, 0x01, 0x00, 0x01, 0x00, 0x02, 0x02, 0x0D, 0x08, 0x10, 0x08, 0x24, 0x00, 0x00, 0x04, 0x2C,
		0x09, 0x01, 0x34, 0x03, 0x00, 0x04, 0x00, 0x14, 0x08, 0x3A, 0x00, 0x02, 0x00, 0x20, 0x2C, 0x08
	};

	// PPU IO open bus related. See https://wiki.nesdev.org/w/index.php?title=PPU_registers#Ports
	// and the 'NES PPU Open-Bus Test' test rom readme
	struct OpenBusIO
	{
		OpenBusIO() { decayed.fill(true); }

		const unsigned decay_ppu_cycle_length = 262 * 341 * 36; // roughly 600 ms = 36 frames; how long it takes for a bit to decay to 0.
		u8 value = 0; // the value read back when reading from open bus.
		std::array<bool, 8> decayed{}; // each bit can decay separately
		std::array<unsigned, 8> ppu_cycles_since_refresh{};

		u8 Read(u8 mask = 0xFF)
		{   /* Reading the bits of open bus with the bits determined by 'mask' does not refresh those bits. */
			return value & mask;
		}
		void Write(u8 data)
		{   /* Writing to any PPU register sets the entire decay register to the value written, and refreshes all bits. */
			UpdateDecayOnIOAccess(0xFF);
			value = data;
		}
		void UpdateValue(u8 data, u8 mask)
		{   /* Here, the bits of open bus determined by the mask are updated with the supplied data. Also, these bits are refreshed, but not the other ones. */
			UpdateDecayOnIOAccess(mask);
			value = data & mask | value & ~mask;
		}
		void UpdateDecay(unsigned elapsed_ppu_cycles);
		void UpdateDecayOnIOAccess(u8 mask);
	} open_bus_io;

	struct ScrollRegisters
	{
		/* Composition of 'v' (and 't'):
		  yyy NN YYYYY XXXXX
		  ||| || ||||| +++++-- coarse X scroll
		  ||| || +++++-------- coarse Y scroll
		  ||| ++-------------- nametable select
		  +++----------------- fine Y scroll
		*/
		unsigned v : 15; // Current VRAM address (15 bits): yyy NN YYYYY XXXXX
		unsigned t : 15; // Temporary VRAM address (15 bits); can also be thought of as the address of the top left onscreen tile.
		unsigned x : 3; // Fine X scroll (3 bits)
		bool w; // First or second $2005/$2006 write toggle (1 bit)

		void increment_coarse_x()
		{
			if ((v & 0x1F) == 0x1F) // if coarse X == 31
			{
				v &= ~0x1F; // set course x = 0
				v ^= 0x400; // switch horizontal nametable by toggling bit 10
			}
			else v++; // increment coarse X
		}
		void increment_fine_y()
		{
			if ((v & 0x7000) == 0x7000) // if fine y == 7
			{
				v &= ~0x7000; // set fine y = 0
				if ((v & 0x3A0) == 0x3A0) // if course y is 29 or 31
				{
					if ((v & 0x40) == 0) // if course y is 29
						v ^= 0x800; // switch vertical nametable
					v &= ~0x3E0; // set course y = 0
				}
				else v += 0x20; // increment coarse y
			}
			else v += 0x1000; // increment fine y
		}
	} scroll;

	struct SpriteEvaluation
	{
		unsigned num_sprites_copied = 0; // (0-8) the number of sprites copied from OAM into secondary OAM
		unsigned sprite_index; // (0-63) index of the sprite currently being checked in OAM
		unsigned byte_index; // (0-3) byte of this sprite
		bool idle = false; // whether the sprite evaluation is finished for the current scanline
		// Whether the 0th byte was copied from OAM into secondary OAM
		bool sprite_0_included_current_scanline = false;
		// Sprite evaluation is done for the *next* scanline. Set this to true during sprite evaluation, and then copy 'next' into 'current' when transitioning to a new scanline.
		bool sprite_0_included_next_scanline = false;

		void Restart() { num_sprites_copied = sprite_index = byte_index = idle = 0; }
		void Reset() { Restart(); sprite_0_included_current_scanline = sprite_0_included_next_scanline = false; }

		void IncrementSpriteIndex()
		{
			if (++sprite_index == 64)
				idle = true;
		}

		void IncrementByteIndex()
		{
			// Check whether we have copied all four bytes of a sprite yet.
			if (++byte_index == 4)
			{
				// Move to the next sprite in OAM (by incrementing n). 
				if (sprite_index == 0)
				{
					sprite_0_included_next_scanline = true;
					sprite_index = 1;
				}
				else
					IncrementSpriteIndex();
				byte_index = 0;
				num_sprites_copied++;
			}
		}
	} sprite_evaluation;

	struct TileFetcher
	{
		// fetched data of the tile currently being fetched
		u8 tile_num; // nametable byte; hex digits 2-1 of the address of the tile's pattern table entries. 
		u8 attribute_table_byte; // palette data for the tile. depending on which quadrant of a 16x16 pixel metatile this tile is in, two bits of this byte indicate the palette number (0-3) used for the tile
		u8 pattern_table_tile_low, pattern_table_tile_high; // actual colour data describing the tile. If bit n of tile_high is 'x' and bit n of tile_low is 'y', the colour id for pixel n of the tile is 'xy'

		// used only for background tiles
		u8 attribute_table_quadrant;

		// used only for sprites
		u8 sprite_y_pos;
		u8 sprite_attr;

		u16 addr;

		unsigned cycle_step : 3; // (0-7)

		void StartOver() { cycle_step = 0; }
	} tile_fetcher;

	/* "A12" refers to the 12th ppu address bus pin.
	   It is set/cleared by the PPU during rendering, specifically when fetching BG tiles / sprites.
	   It can also be set/cleared outside of rendering, when $2006/$2007 is read/written to,
	   for the reason that the address bus pins outside of rendering are set to the vram address (scroll.v).
	   MMC3 contains a scanline counter that gets clocked when A12 (0 -> 1), once A12 has remained low for 3 cpu cycles.
	   TODO: in the future: consider the entire address bus, not just A12? This is basically just to get MMC3 to work. */
	bool a12;
	unsigned cpu_cycles_since_a12_set_low = 0;
	void SetA12(bool new_val)
	{
		if (a12 ^ new_val)
		{
			if (new_val == 1)
			{
				if (cpu_cycles_since_a12_set_low >= 3)
					nes->mapper->ClockIRQ();
			}
			else
				cpu_cycles_since_a12_set_low = 0;
			a12 = new_val;
		}
	}

	bool cycle_340_was_skipped_on_last_scanline = false; // On NTSC, cycle 340 of the pre render scanline may be skipped every other frame.
	bool NMI_line = 1;
	bool odd_frame = false;
	bool reset_graphics_after_render = false;
	bool set_sprite_0_hit_flag = false;

	u8 pixel_x_pos = 0;
	u8 PPUCTRL;
	u8 PPUMASK;
	u8 PPUSTATUS;
	u8 PPUSCROLL;
	u8 PPUDATA;
	u8 OAMADDR;
	u8 OAMADDR_at_cycle_65;
	u8 OAMDMA;

	int scanline = 0;

	unsigned cpu_cycle_counter = 0; /* Used in PAL mode to sync ppu to cpu */
	unsigned framebuffer_pos = 0;
	unsigned scanline_cycle;
	unsigned secondary_oam_sprite_index /* (0-7) index of the sprite currently being fetched (ppu dots 257-320). */;
	unsigned window_scale;
	unsigned window_scale_temp;
	unsigned window_pixel_offset_x;
	unsigned window_pixel_offset_x_temp;
	unsigned window_pixel_offset_y;
	unsigned window_pixel_offset_y_temp;

	std::array<u8, 0x100 > oam          {}; /* Not mapped. Holds sprite data (four bytes each for up to 64 sprites). */
	std::array<u8, 0x20  > palette_ram  {}; /* Mapped to PPU $3F00-$3F1F (mirrored at $3F20-$3FFF). */
	std::array<u8, 0x20  > secondary_oam{}; /* Holds sprite data for sprites to be rendered on the next scanline. */

	std::array<u8 ,  8> sprite_attribute_latch  {};
	std::array<u8 , 16> sprite_pattern_shift_reg{};
	std::array<u16,  2> bg_palette_attr_reg     {}; // These are actually 8 bits on real HW, but it's easier this way. Similar to the pattern shift registers, the MSB contain data for the current tile, and the bottom LSB for the next tile.
	std::array<u16,  2> bg_pattern_shift_reg    {};

	std::array<int, 8> sprite_x_pos_counter{};

	std::vector<u8> framebuffer{};

	SDL_Renderer* renderer;
	SDL_Window* window;

	/* Note: vblank is counted to begin on the first "post-render" scanline, not on the same scanline as when NMI is triggered. */
	bool IsInVblank() const { return scanline >= standard.nmi_scanline - 1; }

	void CheckNMI();
	void LogState();
	void PrepareForNewFrame();
	void PrepareForNewScanline();
	void PushPixelToFramebuffer(u8 nes_col);
	void ReloadBackgroundShiftRegisters();
	void ReloadSpriteShiftRegisters(unsigned sprite_index);
	void RenderGraphics();
	void ResetGraphics();
	void ShiftPixel();
	void StepCycle();
	void UpdateBGTileFetching();
	void UpdateSpriteEvaluation();
	void UpdateSpriteTileFetching();
	void WriteMemory(u16 addr, u8 data);
	void WritePaletteRAM(u16 addr, u8 data);

	template<TileType tile_type>
	u8 GetNESColorFromColorID(u8 col_id, u8 palette_id);

	u8 ReadMemory(u16 addr);
	u8 ReadPaletteRAM(u16 addr);

	size_t GetFrameBufferSize() const { return num_pixels_per_scanline * standard.num_visible_scanlines * num_colour_channels; };
};