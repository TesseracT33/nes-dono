#include "PPU.h"

/* PPUCTRL
 7  bit  0
 ---- ----
 VPHB SINN
 |||| ||||
 |||| ||++- Base nametable address
 |||| ||    (0 = $2000; 1 = $2400; 2 = $2800; 3 = $2C00)
 |||| |+--- VRAM address increment per CPU read/write of PPUDATA
 |||| |     (0: add 1, going across; 1: add 32, going down)
 |||| +---- Sprite pattern table address for 8x8 sprites
 ||||       (0: $0000; 1: $1000; ignored in 8x16 mode)
 |||+------ Background pattern table address (0: $0000; 1: $1000)
 ||+------- Sprite size (0: 8x8 pixels; 1: 8x16 pixels)
 |+-------- PPU master/slave select
 |          (0: read backdrop from EXT pins; 1: output color on EXT pins)
 +--------- Generate an NMI at the start of the
			vertical blanking interval (0: off; 1: on)
*/
#define PPUCTRL_NMI_ENABLE_MASK         0x80
#define PPUCTRL_PPU_MASTER_MASK         0x40
#define PPUCTRL_SPRITE_HEIGHT_MASK      0x20
#define PPUCTRL_BG_TILE_SELECT_MASK     0x10
#define PPUCTRL_SPRITE_TILE_SELECT_MASK 0x08
#define PPUCTRL_INCR_MODE_MASK          0x04
#define PPUCTRL_NAMETABLE_SELECT_MASK   0x03
#define PPUCTRL_NMI_ENABLE         (PPUCTRL & PPUCTRL_NMI_ENABLE_MASK)
#define PPUCTRL_PPU_MASTER         (PPUCTRL & PPUCTRL_PPU_MASTER_MASK)
#define PPUCTRL_SPRITE_HEIGHT      (PPUCTRL & PPUCTRL_SPRITE_HEIGHT_MASK)
#define PPUCTRL_BG_TILE_SELECT     (PPUCTRL & PPUCTRL_BG_TILE_SELECT_MASK)
#define PPUCTRL_SPRITE_TILE_SELECT (PPUCTRL & PPUCTRL_SPRITE_TILE_SELECT_MASK)
#define PPUCTRL_INCR_MODE          (PPUCTRL & PPUCTRL_INCR_MODE_MASK)
#define PPUCTRL_NAMETABLE_SELECT   (PPUCTRL & PPUCTRL_NAMETABLE_SELECT_MASK)

/* PPUMASK
 7  bit  0
 ---- ----
 BGRs bMmG
 |||| ||||
 |||| |||+- Greyscale (0: normal color, 1: produce a greyscale display)
 |||| ||+-- 1: Show background in leftmost 8 pixels of screen, 0: Hide
 |||| |+--- 1: Show sprites in leftmost 8 pixels of screen, 0: Hide
 |||| +---- 1: Show background
 |||+------ 1: Show sprites
 ||+------- Emphasize red (green on PAL/Dendy)
 |+-------- Emphasize green (red on PAL/Dendy)
 +--------- Emphasize blue
*/
#define PPUMASK_EMPHASIZE_BLUE_MASK         0x80
#define PPUMASK_EMPHASIZE_GREEN_MASK        0x40
#define PPUMASK_EMPHASIZE_RED_MASK          0x20
#define PPUMASK_SPRITE_ENABLE_MASK          0x10
#define PPUMASK_BG_ENABLE_MASK              0x08
#define PPUMASK_SPRITE_LEFT_COL_ENABLE_MASK 0x04
#define PPUMASK_BG_LEFT_COL_ENABLE_MASK     0x02
#define PPUMASK_GREYSCALE_MASK              0x01
#define PPUMASK_EMPHASIZE_BLUE         (PPUMASK & PPUMASK_EMPHASIZE_BLUE_MASK)
#define PPUMASK_EMPHASIZE_GREEN        (PPUMASK & PPUMASK_EMPHASIZE_GREEN_MASK)
#define PPUMASK_EMPHASIZE_RED          (PPUMASK & PPUMASK_EMPHASIZE_RED_MASK)
#define PPUMASK_SPRITE_ENABLE          (PPUMASK & PPUMASK_SPRITE_ENABLE_MASK)
#define PPUMASK_BG_ENABLE              (PPUMASK & PPUMASK_BG_ENABLE_MASK)
#define PPUMASK_SPRITE_LEFT_COL_ENABLE (PPUMASK & PPUMASK_SPRITE_LEFT_COL_ENABLE_MASK)
#define PPUMASK_BG_LEFT_COL_ENABLE     (PPUMASK & PPUMASK_BG_LEFT_COL_ENABLE_MASK)
#define PPUMASK_GREYSCALE              (PPUMASK & PPUMASK_GREYSCALE_MASK)

/* PPUSTATUS
 7  bit  0
 ---- ----
 VSO. ....
 |||| ||||
 |||+-++++- Least significant bits previously written into a PPU register
 |||        (due to register not being updated for this address)
 ||+------- Sprite overflow. The intent was for this flag to be set
 ||         whenever more than eight sprites appear on a scanline, but a
 ||         hardware bug causes the actual behavior to be more complicated
 ||         and generate false positives as well as false negatives; see
 ||         PPU sprite evaluation. This flag is set during sprite
 ||         evaluation and cleared at dot 1 (the second dot) of the
 ||         pre-render line.
 |+-------- Sprite 0 Hit.  Set when a nonzero pixel of sprite 0 overlaps
 |          a nonzero background pixel; cleared at dot 1 of the pre-render
 |          line.  Used for raster timing.
 +--------- Vertical blank has started (0: not in vblank; 1: in vblank).
			Set at dot 1 of line 241 (the line *after* the post-render
			line); cleared after reading $2002 and at dot 1 of the
			pre-render line.
*/
#define PPUSTATUS_VBLANK_MASK           0x80
#define PPUSTATUS_SPRITE_0_HIT_MASK     0x40
#define PPUSTATUS_SPRITE_OVERFLOW_MASK  0x20
#define PPUSTATUS_VBLANK          (PPUSTATUS & PPUSTATUS_VBLANK_MASK)
#define PPUSTATUS_SPRITE_0_HIT    (PPUSTATUS & PPUSTATUS_SPRITE_0_HIT_MASK)
#define PPUSTATUS_SPRITE_OVERFLOW (PPUSTATUS & PPUSTATUS_SPRITE_OVERFLOW_MASK)


#define RENDERING_IS_ENABLED (PPUMASK_BG_ENABLE || PPUMASK_SPRITE_ENABLE)


PPU::~PPU()
{
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
}


void PPU::PowerOn(const System::VideoStandard standard)
{
	Reset();

	PPUSTATUS = OAMADDR = scroll.v = scroll.t = a12 = 0;
	palette_ram = palette_ram_on_powerup;

	switch (standard)
	{
	case System::VideoStandard::NTSC: this->standard = NTSC; break;
	case System::VideoStandard::PAL: this->standard = PAL; break;
	case System::VideoStandard::Dendy: this->standard = Dendy; break;
	}

	const size_t framebuffer_size = GetFrameBufferSize();
	this->framebuffer.resize(framebuffer_size);
}


void PPU::Reset()
{
	PPUCTRL = PPUMASK = PPUSCROLL = PPUDATA = scroll.w = 0;
	scanline_cycle = 0;
	odd_frame = true;
	scanline = 0;
	pixel_x_pos = 0;
	framebuffer_pos = 0;
}


bool PPU::CreateRenderer(const void* window_handle)
{
	this->window = SDL_CreateWindowFrom(window_handle);
	if (window == nullptr)
	{
		const char* error_msg = SDL_GetError();
		UserMessage::Show(std::format("Could not create the SDL window; {}", error_msg), UserMessage::Type::Error);
		return false;
	}

	this->renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (renderer == nullptr)
	{
		const char* error_msg = SDL_GetError();
		UserMessage::Show(std::format("Could not create the SDL renderer; {}", error_msg), UserMessage::Type::Error);
		SDL_DestroyWindow(window);
		window = nullptr;
		return false;
	}

	return true;
}


void PPU::Update()
{
#ifdef DEBUG
	LogState();
#endif

	/* PPU::Update() is called once each cpu cycle.
	   On NTSC/Dendy: 1 cpu cycle = 3 ppu cycles.
	   On PAL       : 1 cpu cycle = 3.2 ppu cycles. */
	if (standard.dots_per_cpu_cycle == 3) /* NTSC/Dendy */
	{
		StepCycle();
		StepCycle();
		// The NMI edge detector and IRQ level detector is polled during the second half of each cpu cycle. Here, we are polling 2/3 in.
		nes->cpu->PollInterruptInputs();
		StepCycle();

		/* Updated on a per-cpu-cycle basis, as precision isn't very important here. */
		open_bus_io.UpdateDecay(3 /* elapsed ppu cycles */);
	}
	else /* PAL */
	{
		StepCycle();
		StepCycle();
		nes->cpu->PollInterruptInputs();
		StepCycle();

		if (++cpu_cycle_counter == 5)
		{
			/* This makes for a total of 3 * 5 + 1 = 16 = 3.2 * 5 ppu cycles per every 5 cpu cycles. */
			StepCycle();
			cpu_cycle_counter = 0;
			open_bus_io.UpdateDecay(4);
		}
		else
			open_bus_io.UpdateDecay(3);
	}
	if (cpu_cycles_since_a12_set_low < 3 && a12 == 0)
		cpu_cycles_since_a12_set_low++;
}


void PPU::StepCycle()
{
	if (set_sprite_0_hit_flag && scanline_cycle >= 2)
	{
		PPUSTATUS |= PPUSTATUS_SPRITE_0_HIT_MASK;
		set_sprite_0_hit_flag = false;
	}
	if (scanline_cycle == 0)
	{
		// Idle cycle on every scanline, except for if cycle 340 on the previous scanline was skipped. Then, we perform another dummy nametable fetch.
		if (cycle_340_was_skipped_on_last_scanline)
		{
			if (RENDERING_IS_ENABLED)
				UpdateBGTileFetching();
			cycle_340_was_skipped_on_last_scanline = false;
		}
		scanline_cycle = 1;
		tile_fetcher.StartOver();
		return;
	}

	/* NTSC     : scanlines -1 (pre-render), 0-239
	*  PAL/Dendy: scanlines -1 (pre-render), 0-238 */
	if (scanline < standard.num_visible_scanlines)
	{
		const bool rendering_is_enabled = RENDERING_IS_ENABLED;

		if (scanline_cycle <= 256) // Cycles 1-256
		{
			// The shifters are reloaded during ticks 9, 17, 25, ..., 257, i.e., if tile_fetch_cycle_step == 0 && scanline_cycle >= 9
			// They are only reloaded on visible scanlines.
			if (tile_fetcher.cycle_step == 0 && scanline_cycle >= 9 && scanline != pre_render_scanline)
				ReloadBackgroundShiftRegisters();
			// Update the BG tile fetching every cycle (if rendering is enabled).s
			// Although no pixels are rendered on the pre-render scanline, the PPU still makes the same memory accesses it would for a regular scanline.
			if (rendering_is_enabled)
				UpdateBGTileFetching();
			// Shift one pixel per cycle during cycles 1-256 on visible scanlines
			// Sprite evaluation happens either if bg or sprite rendering is enabled, but not on the pre render scanline (oddly enough)
			// If one the pre render scanline, clear ppu status flags and render graphics on dot 1
			if (scanline == pre_render_scanline)
			{
				if (scanline_cycle == 1)
				{
					PPUSTATUS &= ~(PPUSTATUS_VBLANK_MASK | PPUSTATUS_SPRITE_0_HIT_MASK | PPUSTATUS_SPRITE_OVERFLOW_MASK);
					CheckNMI();
					RenderGraphics();
				}
			}
			else
			{
				if (rendering_is_enabled)
					UpdateSpriteEvaluation();
				ShiftPixel();
			}
		}
		else if (scanline_cycle <= 320) // Cycles 257-320
		{
			// OAMADDR is set to 0 at every cycle in this interval on visible scanlines + on the pre-render one (if rendering is enabled)
			if (rendering_is_enabled)
				OAMADDR = 0;

			if (scanline_cycle == 257)
			{
				ReloadBackgroundShiftRegisters(); // Update the bg shift registers at cycle 257
				if (rendering_is_enabled)
					scroll.v = scroll.v & ~0x41F | scroll.t & 0x41F; // copy all bits related to horizontal position from t to v:
				secondary_oam_sprite_index = 0;
			}

			// Consider an 8 cycle period (0-7) between cycles 257-320 (of which there are eight: one for each sprite)
			// On cycle 0-3: read the Y-coordinate, tile number, attributes, and X-coordinate of the selected sprite from secondary OAM.
			//    Note: All of this can be done on cycle 0, as none of this data is used until cycle 5 at the earliest (some of it is not used until the next scanline).
			// On each cycle, update the sprite tile fetching.
			// On cycle 8 (i.e. the cycle after each period: 265, 273, ..., 321), update the sprite shift registers with pattern data.
			if (rendering_is_enabled)
			{
				if (tile_fetcher.cycle_step == 0)
				{
					tile_fetcher.sprite_y_pos                          = secondary_oam[4 * secondary_oam_sprite_index];
					tile_fetcher.tile_num                              = secondary_oam[4 * secondary_oam_sprite_index + 1];
					tile_fetcher.sprite_attr                           = secondary_oam[4 * secondary_oam_sprite_index + 2];
					sprite_attribute_latch[secondary_oam_sprite_index] = secondary_oam[4 * secondary_oam_sprite_index + 2];
					sprite_x_pos_counter[secondary_oam_sprite_index]   = secondary_oam[4 * secondary_oam_sprite_index + 3];

					if (scanline_cycle >= 265)
						ReloadSpriteShiftRegisters(secondary_oam_sprite_index - 1); // Once we've hit this point for the first time, it's time to update for sprite 0, but sprite_index will be 1.

					secondary_oam_sprite_index++;
				}
				UpdateSpriteTileFetching();

				if (scanline == pre_render_scanline && scanline_cycle >= 280 && scanline_cycle <= 304)
				{
					// Copy the vertical bits of t to v
					scroll.v = scroll.v & ~0x7BE0 | scroll.t & 0x7BE0;
				}
			}
		}
		else // Cycles 321-340
		{
			// Reload the shift registers for the 7th and last sprite.
			if (scanline_cycle == 321)
				ReloadSpriteShiftRegisters(7);
			// Between cycles 322 and 337, the background shift registers are shifted.
			else if (scanline_cycle <= 337)
			{
				bg_pattern_shift_reg[0] <<= 1;
				bg_pattern_shift_reg[1] <<= 1;
				bg_palette_attr_reg[0] <<= 1;
				bg_palette_attr_reg[1] <<= 1;
			}
			// Reload the BG shift registers at cycle 329 and 337 (for a total of two tiles fetched)
			if (scanline_cycle == 329 || scanline_cycle == 337)
				ReloadBackgroundShiftRegisters();
			// Update BG tile fetching during each cycle. In total, two tiles are fetched + two nametable fetches.
			if (rendering_is_enabled)
				UpdateBGTileFetching();
		}
	}
	/* NTSC: scanline 241. PAL: scanline 240. Dendy: scanline 290 */
	else if (scanline == standard.nmi_scanline && scanline_cycle == 1)
	{
		PPUSTATUS |= PPUSTATUS_VBLANK_MASK;
		CheckNMI();
		SetA12(scroll.v & 0x1000); /* At the start of vblank, the bus address is set back to the video ram address. */
		scanline_cycle = 2;
		return;
	}

	// Increment the scanline cycle counter. Normally, each scanline is 341 clocks long.
	// On NTSC specifically:
	//   With rendering enabled, each odd PPU frame is one PPU cycle shorter than normal; specifically, the pre-render scanline is only 340 clocks long.
	//   The last nametable fetch, normally taking place on cycle 340, then takes place on cycle 0 the following scanline.
	if (scanline_cycle == 339)
	{
		if (standard.pre_render_line_is_one_dot_shorter_on_every_other_frame &&
			scanline == pre_render_scanline && odd_frame && RENDERING_IS_ENABLED)
		{
			scanline_cycle = 0;
			cycle_340_was_skipped_on_last_scanline = true;
			PrepareForNewScanline();
		}
		else
			scanline_cycle = 340;
	}
	else if (scanline_cycle == 340)
	{
		scanline_cycle = 0;
		PrepareForNewScanline();
	}
	else
	{
		scanline_cycle++;
	}
}


u8 PPU::ReadRegister(u16 addr)
{
	/* The following shows the effect of a read from each register:
	Addr    Open-bus bits
			7654 3210
	-----------------
	$2000   DDDD DDDD
	$2001   DDDD DDDD
	$2002   ---D DDDD
	$2003   DDDD DDDD
	$2004   ---- ----
	$2005   DDDD DDDD
	$2006   DDDD DDDD
	$2007   ---- ----   non-palette
			DD-- ----   palette

	A D means that this bit reads back as whatever is in the decay register
	at that bit, and doesn't refresh the decay register at that bit. A -
	means that this bit reads back as defined by the PPU, and refreshes the
	decay register at the corresponding bit. */

	switch (addr)
	{
	case Bus::Addr::PPUCTRL  : // $2000 (write-only)
	case Bus::Addr::PPUMASK  : // $2001 (write-only)
	case Bus::Addr::OAMADDR  : // $2003 (write-only)
	case Bus::Addr::PPUSCROLL: // $2005 (write-only)
	case Bus::Addr::PPUADDR  : // $2006 (write-only)
	case Bus::Addr::OAMDMA   : // $4014 (write-only)
		return open_bus_io.Read();

	case Bus::Addr::PPUSTATUS: // $2002 (read-only)
	{
		const u8 ret = PPUSTATUS & 0xE0 | open_bus_io.Read(0x1F); /* Bits 4-0 are unused and then return bits 4-0 of open bus */
		open_bus_io.UpdateValue(PPUSTATUS, 0xE0); /* Update bits 7-5 of open bus with the read value */
		PPUSTATUS &= ~PPUSTATUS_VBLANK_MASK; /* Reading this register clears the vblank flag */
		CheckNMI();
		scroll.w = 0;
		return ret;
	}

	case Bus::Addr::OAMDATA: // $2004 (read/write)
	{
		// TODO: according to nesdev: during cycles 1-64, all entries of secondary OAM are initialised to 0xFF, and an internal signal makes reading from OAMDATA always return 0xFF during this time
		// Is this actually true? blargg 'ppu_open_bus' and 'sprite_ram' tests fail if this is emulated, and Mesen does not seem to implement it.
		u8 ret = oam[OAMADDR];
		// Bits 2-4 of sprite attributes should always be clear when read (these are unimplemented).
		if ((OAMADDR & 3) == 2)
			ret &= 0xE3;
		open_bus_io.UpdateValue(ret, 0xFF); /* Update all bits of open bus with the read value */
		return ret;
	}

	case Bus::Addr::PPUDATA: // $2007 (read/write)
	{
		// Outside of rendering, read the value at address 'v' and add either 1 or 32 to 'v'.
		// During rendering, return $FF (?), and increment both coarse x and y.
		if (IsInVblank() || !RENDERING_IS_ENABLED)
		{
			u8 ret;
			u16 v_read = scroll.v & 0x3FFF; // Only bits 0-13 of v are used; the PPU memory space is 14 bits wide.
			// When reading while the VRAM address is in the range 0-$3EFF (i.e., before the palettes), the read will return the contents of an internal read buffer which is updated only when reading PPUDATA.
			// After the CPU reads and gets the contents of the internal buffer, the PPU will immediately update the internal buffer with the byte at the current VRAM address.
			if (v_read <= 0x3EFF)
			{
				ret = PPUDATA;
				PPUDATA = ReadMemory(v_read);
				open_bus_io.UpdateValue(ret, 0xFF); /* Update all bits of open bus with the read value */
			}
			// When reading palette data $3F00-$3FFF, the palette data is placed immediately on the data bus.
			// However, reading the palettes still updates the internal buffer, but the data is taken from a section of mirrored nametable data.
			else
			{
				// High 2 bits from palette should be from open bus. Reading palette shouldn't refresh high 2 bits of open bus.
				ret = ReadPaletteRAM(v_read) | open_bus_io.Read(0xC0); // Note: the result from ReadPaletteRAM is guaranteed to have bits 7-6 cleared.
				PPUDATA = ReadMemory(v_read & 0xFFF | 0x2000); // Read from vram at $2000-$2FFF
				open_bus_io.UpdateValue(ret, 0x3F); /* Update bits 5-0 of open bus with the read value */
			}
			scroll.v += (PPUCTRL_INCR_MODE ? 32 : 1);
			SetA12(scroll.v & 0x1000);
			return ret;
		}
		else
		{
			scroll.increment_coarse_x();
			scroll.increment_fine_y();
			return open_bus_io.Read();
		}
	}

	default:
		throw std::runtime_error(std::format("Invalid address ${:X} given as argument to PPU::ReadRegister(u16).", addr));
	}
}


void PPU::WriteRegister(const u16 addr, const u8 data)
{
	/* Writes to any PPU port, including the nominally read-only status port at $2002, load a value onto the entire PPU's I/O bus */
	open_bus_io.Write(data);

	switch (addr)
	{
	case Bus::Addr::PPUCTRL: // $2000 (write-only)
		PPUCTRL = data;
		CheckNMI();
		scroll.t = scroll.t & ~0xC00 | (data & 3) << 10; // Set bits 11-10 of 't' to bits 1-0 of 'data'
		break;

	case Bus::Addr::PPUMASK: // $2001 (write-only)
		PPUMASK = data;
		break;

	case Bus::Addr::PPUSTATUS: // $2002 (read-only)
		break;

	case Bus::Addr::OAMADDR: // $2003 (write-only)
		OAMADDR = data;
		break;

	case Bus::Addr::OAMDATA: // $2004 (read/write)
		// On NTSC/Dendy: OAM can only be written to during vertical (up to 20 scanlines after NMI) or forced blanking.
		// On PAL: OAM can only be written to during the first 20 scanlines after NMI
		if (scanline < standard.nmi_scanline + 20 ||
			standard.oam_can_be_written_to_during_forced_blanking && !RENDERING_IS_ENABLED)
		{
			oam[OAMADDR++] = data;
		}
		else
		{
			// Do not modify values in OAM, but do perform a glitchy increment of OAMADDR, bumping only the high 6 bits
			OAMADDR += 0b100;
		}
		break;

	case Bus::Addr::PPUSCROLL: // $2005 (write-only)
		if (scroll.w == 0) // Update x-scroll registers
		{
			scroll.t = scroll.t & ~0x1F | data >> 3; // Set bits 4-0 of 't' (coarse x-scroll) to bits 7-3 of 'data'
			scroll.x = data; // Set 'x' (fine x-scroll) to bits 2-0 of 'data'
		}
		else // Update y-scroll registers
		{
			// Set bits 14-12 of 't' (fine y-scroll) to bits 2-0 of 'data', and bits 9-5 of 't' (coarse y-scroll) to bits 7-3 of 'data'
			scroll.t = scroll.t & ~0x73E0 | (data & 0x07) << 12 | (data & 0xF8) << 2;
		}
		scroll.w = !scroll.w;
		break;

	case Bus::Addr::PPUADDR: // $2006 (write-only)
		if (scroll.w == 0)
		{
			scroll.t = scroll.t & 0xFF | (data & 0x3F) << 8; // Set bits 13-8 of 't' to bits 5-0 of 'data', and clear bit 14 of 't'
		}
		else
		{
			scroll.t = scroll.t & 0xFF00 | data; // Set the lower byte of 't' to 'data'
			scroll.v = scroll.t;
			SetA12(scroll.v & 0x1000);
		}
		scroll.w = !scroll.w;
		break;

	case Bus::Addr::PPUDATA: // $2007 (read/write)
		// Outside of rendering, write the value and add either 1 or 32 to v.
		// During rendering, the write is not done, unless it is to palette ram. Else, both coarse x and y are incremented.
		if (IsInVblank() || !RENDERING_IS_ENABLED)
		{
			WriteMemory(scroll.v & 0x3FFF, data); // Only bits 0-13 of v are used; the PPU memory space is 14 bits wide.
			scroll.v += (PPUCTRL_INCR_MODE ? 32 : 1);
			SetA12(scroll.v & 0x1000);
		}
		else if ((scroll.v & 0x3FFF) >= 0x3F00)
		{
			WritePaletteRAM(scroll.v, data);
			SetA12(scroll.v & 0x1000);
			// Do not increment scroll.v
		}
		else
		{
			scroll.increment_coarse_x();
			scroll.increment_fine_y();
		}
		break;

	case Bus::Addr::OAMDMA: // $4014 (write-only)
		// Perform OAM DMA transfer. Writing $XX will upload 256 bytes of data from CPU page $XX00-$XXFF to the internal PPU OAM.
		// It is done by the cpu, so the cpu will be suspended during this time.
		// The writes to OAM will start at the current value of OAMADDR (OAM will be cycled if OAMADDR > 0)
		// TODO: what happens if OAMDMA is written to while a transfer is already taking place?
		nes->cpu->StartOAMDMATransfer(data, oam.data(), OAMADDR);
		break;

	default:
		throw std::runtime_error(std::format("Invalid address ${:X} given as argument to PPU::WriteRegister(u16).", addr));
	}
}


void PPU::OpenBusIO::UpdateDecayOnIOAccess(u8 mask)
{
	/* Optimization; a lot of the time, the mask will be $FF. */
	if (mask == 0xFF)
	{
		ppu_cycles_since_refresh.fill(0);
		decayed.fill(false);
	}
	else
	{
		/* Refresh the bits given by the mask */
		for (int n = 0; n < 8; n++)
		{
			if (mask & 1 << n)
			{
				ppu_cycles_since_refresh[n] = 0;
				decayed[n] = false;
			}
		}
	}
}


void PPU::OpenBusIO::UpdateDecay(unsigned elapsed_ppu_cycles)
{
	/* Each bit of the open bus byte can decay at different points, depending on when a particular bit was read/written to last time. */
	for (int n = 0; n < 8; n++)
	{
		if (!decayed[n])
		{
			ppu_cycles_since_refresh[n] += elapsed_ppu_cycles;
			if (ppu_cycles_since_refresh[n] >= decay_ppu_cycle_length)
			{
				value &= ~(1 << n);
				decayed[n] = true;
			}
		}
	}
}


u8 PPU::ReadPaletteRAM(u16 addr)
{
	addr &= 0x1F;
	// Addresses $3F10/$3F14/$3F18/$3F1C are mirrors of $3F00/$3F04/$3F08/$3F0C
	// Note: bits 4-0 of all mirrors have the form 1xy00, and the redirected addresses have the form 0xy00
	if ((addr & 0x13) == 0x10)
		addr -= 0x10;
	// In greyscale mode, use colors only from the grey column: $00, $10, $20, $30.
	if (PPUMASK_GREYSCALE)
		return palette_ram[addr & 0x30];
	return palette_ram[addr];
}


void PPU::WritePaletteRAM(u16 addr, u8 data)
{
	addr &= 0x1F;
	data &= 0x3F; // Each value is 6 bits (0-63)
	if ((addr & 0x13) == 0x10)
		addr -= 0x10;
	if (PPUMASK_GREYSCALE)
		palette_ram[addr & 0x30] = data; 
	palette_ram[addr] = data;
}


void PPU::CheckNMI()
{
	/* The PPU pulls /NMI low only if both PPUCTRL.7 and PPUSTATUS.7 are set.
	   Do not call cpu->SetNMILow() if NMI is already low; this would cause multiple interrupts to be handled for the same signal. */
	if (PPUCTRL_NMI_ENABLE && PPUSTATUS_VBLANK)
	{
		if (NMI_line == 1)
		{
			nes->cpu->SetNMILow();
			NMI_line = 0;
		}
	}
	else if (NMI_line == 0)
	{
		nes->cpu->SetNMIHigh();
		NMI_line = 1;
	}
}


void PPU::UpdateSpriteEvaluation()
{
	/* Cycles   1-64: secondary OAM is initialized to $FF. Here: do everything at cycle 65.
	   Cycles 65-256: read oam, evaluate sprites and copy data into secondary oam.
	                  Read from oam on odd cycles, copy into secondary oam on even cycles.
					  Here: do both things on even cycles.
		TODO: make more accurate.
	*/
	if (scanline_cycle < 65)
		return;
	if (scanline_cycle == 65)
	{
		secondary_oam.fill(0xFF);
		OAMADDR_at_cycle_65 = OAMADDR;
		sprite_evaluation.Restart();
		return;
	}
	if ((scanline_cycle & 1) || sprite_evaluation.idle)
		return;

	// Fetch the next entry in OAM
	// The value of OAMADDR as it were at dot 65 is used as an offset to the address here.
	// If OAMADDR is unaligned and does not point to the y-position (first byte) of an OAM entry, then whatever it points to will be reinterpreted as a y position, and the following bytes will be similarly reinterpreted.
	// When the end of OAM is reached, no more sprites will be found (it will not wrap around to the start of OAM).
	const u32 addr = OAMADDR_at_cycle_65 + 4 * sprite_evaluation.sprite_index + sprite_evaluation.byte_index;
	if (addr >= oam.size())
	{
		sprite_evaluation.idle = true;
		return;
	}
	const u8 oam_entry = oam[addr];

	if (sprite_evaluation.num_sprites_copied < 8)
	{
		// Copy the read oam entry into secondary oam. Note that this occurs even if this is the first byte of a sprite, and we later decide not to copy the rest of it due to it not being in range!
		secondary_oam[4 * sprite_evaluation.num_sprites_copied + sprite_evaluation.byte_index] = oam_entry;

		if (sprite_evaluation.byte_index == 0) // Means that the read oam entry is being interpreted as a y-position.
		{
			// If the y-position is in range, copy the three remaining bytes for that sprite. Else move on to the next sprite.
			if (scanline >= oam_entry && scanline < oam_entry + (PPUCTRL_SPRITE_HEIGHT ? 16 : 8))
				sprite_evaluation.byte_index = 1;
			else
				sprite_evaluation.IncrementSpriteIndex();
		}
		else
			sprite_evaluation.IncrementByteIndex();
	}
	else
	{
		if (scanline >= oam_entry && scanline < oam_entry + (PPUCTRL_SPRITE_HEIGHT ? 16 : 8))
		{
			// If a ninth in-range sprite is found, set the sprite overflow flag.
			PPUSTATUS |= PPUSTATUS_SPRITE_OVERFLOW_MASK;
			// On real hw, the ppu will continue scanning oam after setting this.
			// However, none of it will have an effect on anything other than n and m, which is not visible from the rest of the ppu and system as a whole, so we can start idling from here.
			// Note also that the sprite overflow flag is not writeable by the cpu, and cleared only on the pre-render scanline. Thus, setting it more than one time will not be any different from setting it only once.
			sprite_evaluation.idle = true;
		}
		else
		{
			// hw bug: increment both n and m (instead of just n)
			sprite_evaluation.IncrementByteIndex();
			sprite_evaluation.IncrementSpriteIndex();
		}
	}
}


// Get an actual NES color (indexed 0-63) from a bg or sprite color id (0-3), given the palette id (0-3)
template<PPU::TileType tile_type>
u8 PPU::GetNESColorFromColorID(const u8 col_id, const u8 palette_id)
{
	if (RENDERING_IS_ENABLED)
	{
		// If the color ID is 0, then the 'universal background color', located at $3F00, is used.
		if (col_id == 0)
			return ReadPaletteRAM(0x3F00);
		// For bg tiles, two consecutive bits of an attribute table byte holds the palette number (0-3). These have already been extracted beforehand (see the updating of the '' variable)
		// For sprites, bits 1-0 of the 'attribute byte' (byte 2 from OAM) give the palette number.
		// Each bg and sprite palette consists of three bytes (describing the actual NES colors for color ID:s 1, 2, 3), starting at $3F01, $3F05, $3F09, $3F0D respectively for bg tiles, and $3F11, $3F15, $3F19, $3F1D for sprites
		if constexpr (tile_type == TileType::BG)
			return ReadPaletteRAM(0x3F00 + col_id + 4 * palette_id);
		else
			return ReadPaletteRAM(0x3F00 + col_id + 4 * palette_id + 0x10);
	}
	else
	{
		// If rendering is disabled, show the backdrop colour. 
		// Background palette hack: if the current vram address is in palette "territory", the colour at the current vram address is used, not $3F00.
		if (scroll.v >= 0x3F00 && scroll.v <= 0x3FFF)
			return ReadPaletteRAM(scroll.v);
		return ReadPaletteRAM(0x3F00);
	}
}


void PPU::PushPixelToFramebuffer(const u8 nes_col)
{
	// From the nes colour (0-63), get an RGB24 colour from the predefined palette
	// The palette from https://wiki.nesdev.org/w/index.php?title=PPU_palettes#2C02 was used for this
	const SDL_Color& sdl_col = palette[nes_col];
	framebuffer[framebuffer_pos    ] = sdl_col.r;
	framebuffer[framebuffer_pos + 1] = sdl_col.g;
	framebuffer[framebuffer_pos + 2] = sdl_col.b;
	framebuffer_pos += 3;

	pixel_x_pos++;
}


void PPU::RenderGraphics()
{
	void* pixels = framebuffer.data();
	int width = num_pixels_per_scanline;
	int height = standard.num_visible_scanlines;
	int depth = 8 * num_colour_channels;
	int pitch = num_pixels_per_scanline * num_colour_channels;
	unsigned Rmask = 0x0000FF, Gmask = 0x00FF00, Bmask = 0xFF0000, Amask = 0x000000;
	SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(pixels, width, height, depth, pitch, Rmask, Gmask, Bmask, Amask);

	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

	SDL_Rect rect;
	rect.w = GetWindowWidth();
	rect.h = GetWindowHeight();
	rect.x = window_pixel_offset_x;
	rect.y = window_pixel_offset_y;
	SDL_RenderCopy(renderer, texture, nullptr, &rect);

	SDL_RenderPresent(renderer);

	if (reset_graphics_after_render)
		ResetGraphics();

	SDL_FreeSurface(surface);
	SDL_DestroyTexture(texture);

	gui->frames_since_update++;
}


void PPU::ResetGraphics()
{
	window_scale = window_scale_temp;
	window_pixel_offset_x = window_pixel_offset_x_temp;
	window_pixel_offset_y = window_pixel_offset_y_temp;

	SDL_RenderClear(renderer);
	reset_graphics_after_render = false;
}


void PPU::ShiftPixel()
{
	/* TODO: not clear if pixel colours should be 0 if rendering is disabled */

	// Fetch one bit from each of the two bg shift registers containing pattern table data for the current tile, forming the colour id for the current bg pixel.
	// If the PPUMASK_bg_left_col_enable flag is not set, then the background is not rendered in the leftmost 8 pixel columns.
	const u8 bg_col_id = [&]() {
		if (PPUMASK_BG_ENABLE && (pixel_x_pos >= 8 || PPUMASK_BG_LEFT_COL_ENABLE))
			return ((bg_pattern_shift_reg[0] << scroll.x) & 0x8000) >> 15 | ((bg_pattern_shift_reg[1] << scroll.x) & 0x8000) >> 14;
		return 0;
	}();

	bg_pattern_shift_reg[0] <<= 1;
	bg_pattern_shift_reg[1] <<= 1;

	// Decrement the x-position counters for all 8 sprites. If a counter is 0, the sprite becomes 'active', and the shift registers for the sprite is shifted once every cycle
	// The current pixel for each 'active' sprite is checked, and the first non-transparent pixel moves on to a multiplexer, where it joins the BG pixel.
	u8 sprite_col_id = 0;
	u8 sprite_index = 0; // (0-7)
	if (PPUMASK_SPRITE_ENABLE)
	{
		bool opaque_pixel_found = false;
		for (int i = 0; i < 8; i++)
		{
			bool sprite_is_in_range = sprite_x_pos_counter[i] <= 0 && sprite_x_pos_counter[i] > -8;
			if (sprite_is_in_range)
			{
				// If the PPUMASK_sprite_left_col_enable flag is not set, then sprites are not rendered in the leftmost 8 pixel columns.
				if (!opaque_pixel_found && (pixel_x_pos >= 8 || PPUMASK_SPRITE_LEFT_COL_ENABLE))
				{
					u8 offset = -sprite_x_pos_counter[i]; // Which pixel of the sprite line to render.
					if (sprite_attribute_latch[i] & 0x40) // flip sprite horizontally 
						offset = 7 - offset;

					const u8 col_id = ((sprite_pattern_shift_reg[2 * i] << offset) & 0x80) >> 7 | ((sprite_pattern_shift_reg[2 * i + 1] << offset) & 0x80) >> 6;
					if (col_id != 0)
					{
						sprite_col_id = col_id;
						sprite_index = i;
						opaque_pixel_found = true;
					}
				}
			}
			sprite_x_pos_counter[i]--;
		}

		// Set the sprite zero hit flag if all conditions below are met. Sprites must be enabled.
		if (!PPUSTATUS_SPRITE_0_HIT                                                              && // The flag has not already been set this frame
			sprite_evaluation.sprite_0_included_current_scanline && sprite_index == 0            && // The current sprite is the 0th sprite in OAM
			bg_col_id != 0 && sprite_col_id != 0                                                 && // The bg and sprite colour IDs are not 0, i.e. both pixels are opaque
			PPUMASK_BG_ENABLE                                                                    && // Both bg and sprite rendering must be enabled
			(pixel_x_pos >= 8 || (PPUMASK_BG_LEFT_COL_ENABLE && PPUMASK_SPRITE_LEFT_COL_ENABLE)) && // If the pixel-x-pos is between 0 and 7, the left-side clipping window must be disabled for both bg tiles and sprites.
			pixel_x_pos != 255)                                                                     // The pixel-x-pos must not be 255
		{
			// Due to how internal rendering works, the sprite 0 hit flag will be set at the third tick of a scanline at the earliest.
			if (scanline_cycle >= 2)
				PPUSTATUS |= PPUSTATUS_SPRITE_0_HIT_MASK;
			else
				set_sprite_0_hit_flag = true;
		}
	}
	else
	{
		for (int i = 0; i < 8; i++)
			sprite_x_pos_counter[i]--;
	}

	// Mix the bg and sprite pixels, and get an actual NES color from the color id and palette attribute data
	// Decision table for mixing:
	/* BG pixel | Sprite pixel | Priority | Output
	  ---------------------------------------------
		  0     |       0      |    Any   |   BG
		  0     |      1-3     |    Any   | Sprite
		 1-3    |       0      |    Any   |   BG
		 1-3    |      1-3     |     0    | Sprite
		 1-3    |      1-3     |     1    |   BG
	*/
	const bool sprite_priority = sprite_attribute_latch[sprite_index] & 0x20;

	const u8 col = [&]() {
		if (sprite_col_id > 0 && (sprite_priority == 0 || bg_col_id == 0))
			return GetNESColorFromColorID<TileType::OBJ>(sprite_col_id, sprite_attribute_latch[sprite_index] & 3);
		// Fetch one bit from each of the two bg shift registers containing the palette id for the current tile.
		const u8 bg_palette_id = ((bg_palette_attr_reg[0] << scroll.x) & 0x8000) >> 15 | ((bg_palette_attr_reg[1] << scroll.x) & 0x8000) >> 14;
		return GetNESColorFromColorID<TileType::BG>(bg_col_id, bg_palette_id);
	}();

	bg_palette_attr_reg[0] <<= 1;
	bg_palette_attr_reg[1] <<= 1;

	PushPixelToFramebuffer(col);
}


void PPU::ReloadBackgroundShiftRegisters()
{
	// Reload the lower 8 bits of the two 16-bit background shifters with pattern data for the next tile.
	// The lower byte is already 0x00.
	bg_pattern_shift_reg[0] |= tile_fetcher.pattern_table_tile_low;
	bg_pattern_shift_reg[1] |= tile_fetcher.pattern_table_tile_high;

	// For bg tiles, an attribute table byte holds palette info. Each table entry controls a 32x32 pixel metatile.
	// The byte is divided into four 2-bit areas, which each control a 16x16 pixel metatile
	// Denoting the four 16x16 pixel metatiles by 'bottomright', 'bottomleft' etc, then: value = (bottomright << 6) | (bottomleft << 4) | (topright << 2) | (topleft << 0)
	// We find which quadrant our 8x8 tile lies in. Then, the two extracted bits give the palette number (0-3) used for the tile
	const u8 palette_id = tile_fetcher.attribute_table_byte >> (2 * tile_fetcher.attribute_table_quadrant) & 3;
	// The LSB of the attribute registers are filled with the palette id (0-3) (next tile to be rendered after the current one).
	// Note: the same palette id is used for an entire tile, so the LSB is either set to $00 or $FF
	if (palette_id & 0x01)
		bg_palette_attr_reg[0] |= 0xFF;
	if (palette_id & 0x02)
		bg_palette_attr_reg[1] |= 0xFF;
}


void PPU::ReloadSpriteShiftRegisters(unsigned sprite_index)
{
	// Reload the two 8-bit sprite shift registers (of index 'sprite_index') with pattern data for the next tile.
	// If 'sprite_index' is not less than the number of sprites copied from OAM, the registers are loaded with transparent data instead.
	if (sprite_index < sprite_evaluation.num_sprites_copied)
	{
		sprite_pattern_shift_reg[2 * sprite_index    ] = tile_fetcher.pattern_table_tile_low;
		sprite_pattern_shift_reg[2 * sprite_index + 1] = tile_fetcher.pattern_table_tile_high;
	}
	else
	{
		sprite_pattern_shift_reg[2 * sprite_index    ] = 0;
		sprite_pattern_shift_reg[2 * sprite_index + 1] = 0;
	}
}


void PPU::UpdateBGTileFetching()
{
	/* Each memory access is two cycles long. On the first one, the address is loaded.
	   On the second one, the read/write is made. https://www.nesdev.org/2C02%20technical%20reference.TXT */
	switch (tile_fetcher.cycle_step++)
	{
	case 0: /* Compose address for nametable byte. */
		/* Composition of the nametable address:
		  10 NN YYYYY XXXXX
		  || || ||||| +++++-- Coarse X scroll
		  || || +++++-------- Coarse Y scroll
		  || ++-------------- Nametable select
		  ++----------------- Nametable base address ($2000)
		*/
		tile_fetcher.addr = 0x2000 | (scroll.v & 0xFFF);
		SetA12(0);
		break;

	case 1: /* Fetch nametable byte. */
		tile_fetcher.tile_num = nes->mapper->ReadNametableRAM(tile_fetcher.addr);
		break;

	case 2: /* Compose address for attribute table byte. */
		/* Composition of the attribute address:
		  10 NN 1111 YYY XXX
		  || || |||| ||| +++-- High 3 bits of coarse X scroll (x/4)
		  || || |||| +++------ High 3 bits of coarse Y scroll (y/4)
		  || || ++++---------- Attribute offset (960 = $3c0 bytes)
		  || ++--------------- Nametable select
		  ++------------------ Nametable base address ($2000)
		*/
		tile_fetcher.addr = 0x23C0 | (scroll.v & 0x0C00) | ((scroll.v >> 4) & 0x38) | ((scroll.v >> 2) & 0x07);
		SetA12(0);
		// Determine in which quadrant (0-3) of the 32x32 pixel metatile that the current tile is in
		// topleft == 0, topright == 1, bottomleft == 2, bottomright = 3
		// scroll-x % 4 and scroll-y % 4 give the "tile-coordinates" of the current tile in the metatile
		tile_fetcher.attribute_table_quadrant = 2 * ((scroll.v & 0x60) > 0x20) + ((scroll.v & 0x03) > 0x01);
		break;

	case 3: /* Fetch atttribute table byte. */
		tile_fetcher.attribute_table_byte = nes->mapper->ReadNametableRAM(tile_fetcher.addr);
		break;

	case 4: /* Compose address for pattern table tile low. */
	{
		/* Composition of the pattern table address for BG tiles:
		  H RRRR CCCC P yyy
		  | |||| |||| | +++-- The row number within a tile: fine Y scroll
		  | |||| |||| +------ Bit plane (0: "lower"; 1: "upper")
		  | |||| ++++-------- Tile column
		  | ++++------------- Tile row
		  +------------------ Half of pattern table (0: "left"; 1: "right"); dependent on PPUCTRL flags
		  RRRR CCCC == the nametable byte fetched in step 1
		*/
		const u16 pattern_table_half = PPUCTRL_BG_TILE_SELECT ? 0x1000 : 0x0000;
		tile_fetcher.addr = pattern_table_half | tile_fetcher.tile_num << 4 | scroll.v >> 12;
		SetA12(pattern_table_half);
		break;
	}

	case 5: /* Fetch pattern table tile low. */
		tile_fetcher.pattern_table_tile_low = nes->mapper->ReadCHR(tile_fetcher.addr);
		break;

	case 6: /* Compose address for pattern table tile high. This could be done in step 7 instead; it does not affect A12. */
		// Technically, a game could change PPUCTRL_BG_TILE_SELECT here (?). What game would do that?
		tile_fetcher.addr |= 0x0008;
		SetA12(tile_fetcher.addr & 0x1000);
		break;

	case 7: /* Fetch pattern table tile high. */
		tile_fetcher.pattern_table_tile_high = nes->mapper->ReadCHR(tile_fetcher.addr);
		// Increment coarse x after fetching the tile.
		scroll.increment_coarse_x();
		// Increment the coarse Y scroll at cycle 256, after all BG tiles have been fetched (will be the case when 'cycle_step' is 7)
		if (scanline_cycle == 256)
			scroll.increment_fine_y();
		break;

	default: // impossible
		break;
	}
}


void PPU::UpdateSpriteTileFetching()
{
	switch (tile_fetcher.cycle_step++)
	{
	case 0: case 2: /* Prepare address for garbage nametable fetches. The important thing is to update A12. */
		SetA12(PPUCTRL_BG_TILE_SELECT); // TODO: should PPUCTRL_SPRITE_TILE_SELECT be used instead? Probably not.
		break;

	case 1: case 3: /* Garbage nametable fetches. */
		break;

	case 4: /* Compose address for pattern table tile low. */
	{
		/* Composition of the pattern table address for 8x8 sprites:
		  H RRRR CCCC P yyy
		  | |||| |||| | +++-- The row number within a tile: sprite_y_pos - fine_y_scroll
		  | |||| |||| +------ Bit plane (0: "lower"; 1: "upper")
		  | |||| ++++-------- Tile column
		  | ++++------------- Tile row
		  +------------------ Half of pattern table (0: "left"; 1: "right"); dependent on PPUCTRL flags
		  RRRR CCCC == the sprite tile index number fetched from secondary OAM during cycles 257-320

		Composition of the pattern table adress for 8x16 sprites:
		  H RRRR CCC S P yyy
		  | |||| ||| | | +++-- The row number within a tile: sprite_y_pos - fine_y_scroll. TODO probably not correct
		  | |||| ||| | +------ Bit plane (0: "lower"; 1: "upper")
		  | |||| ||| +-------- Sprite tile half (0: "top"; 1: "bottom")
		  | |||| +++---------- Tile column
		  | ++++-------------- Tile row
		  +------------------- Half of pattern table (0: "left"; 1: "right"); equal to bit 0 of the sprite tile index number fetched from secondary OAM during cycles 257-320
		  RRRR CCC == upper 7 bits of the sprite tile index number fetched from secondary OAM during cycles 257-320
		*/
		// TODO: not sure if scroll.v should be used instead of current_scanline
		const unsigned scanline_sprite_y_delta = scanline - tile_fetcher.sprite_y_pos; // delta between scanline and sprite position (0-15)
		const bool flip_sprite_y = tile_fetcher.sprite_attr & 0x80;
		const unsigned sprite_row_num = [&]() { // which row of the tile the scanline falls on (0-7)
			if (!flip_sprite_y)
				return scanline_sprite_y_delta & 0x07;
			return 7 - (scanline_sprite_y_delta & 0x07);
		}();

		if (PPUCTRL_SPRITE_HEIGHT) // 8x16 sprites
		{
			const bool sprite_table_half = tile_fetcher.tile_num & 0x01;
			u8 tile_num = tile_fetcher.tile_num & 0xFE; // Tile number of the top of sprite (0 to 254; bottom half gets the next tile)
			// Check if we are on the top or bottom tile of the sprite.
			// If sprites are flipped vertically, the top and bottom tiles are flipped.
			const bool on_bottom_tile = scanline_sprite_y_delta > 7;
			const bool fetch_bottom_tile = on_bottom_tile ^ flip_sprite_y;
			if (fetch_bottom_tile)
				tile_num++;
			tile_fetcher.addr = sprite_table_half << 12 | tile_num << 4 | sprite_row_num;
		}
		else // 8x8 sprites
		{
			tile_fetcher.addr = (PPUCTRL_SPRITE_TILE_SELECT ? 0x1000 : 0x0000) | tile_fetcher.tile_num << 4 | sprite_row_num;
		}
		SetA12(tile_fetcher.addr & 0x1000);
		break;
	}

	case 5: /* Fetch pattern table tile low. */
		tile_fetcher.pattern_table_tile_low = nes->mapper->ReadCHR(tile_fetcher.addr);
		break;

	case 6: /* Compose address for pattern table tile high. This could be done in step 7 instead. */
		tile_fetcher.addr |= 0x0008;
		SetA12(tile_fetcher.addr & 0x1000);
		break;

	case 7: /* Fetch pattern table tile high. */
		tile_fetcher.pattern_table_tile_high = nes->mapper->ReadCHR(tile_fetcher.addr);
		break;

	default: // impossible
		break;
	}
}


// Reading and writing done internally by the ppu
u8 PPU::ReadMemory(const u16 addr)
{
	switch (addr >> 12)
	{
	case 0: case 1: // $0000-$1FFF - Pattern tables; maps to CHR ROM/RAM on the game cartridge
		return nes->mapper->ReadCHR(addr);

	case 2: // $2000-$2FFF - Nametables; internal ppu vram.
		return nes->mapper->ReadNametableRAM(addr);

	case 3:
		if (addr < 0x3F00) // $3000-$3EFF - mirror of $2000-$2EFF
			return nes->mapper->ReadNametableRAM(addr);
		else
			return ReadPaletteRAM(addr); // $3F00-$3F1F - Palette RAM indeces. $3F20-$3FFF - mirrors of $3F00-$3F1F

	default:
		throw std::runtime_error(std::format("Invalid address ${:X} given as argument to PPU::ReadMemory(u16, u8). The range is $0000-$3FFF.", addr));
	}
}


void PPU::WriteMemory(const u16 addr, const u8 data)
{
	switch (addr >> 12)
	{
	case 0: case 1: // $0000-$1FFF - Pattern tables; maps to CHR ROM/RAM on the game cartridge
		nes->mapper->WriteCHR(addr, data);
		break;

	case 2: // $2000-$2FFF - Nametables; internal ppu vram.
		nes->mapper->WriteNametableRAM(addr, data);
		break;

	case 3:
		if (addr < 0x3F00) // $3000-$3EFF - mirror of $2000-$2EFF
			nes->mapper->WriteNametableRAM(addr, data);
		else
			WritePaletteRAM(addr, data); // $3F00-$3F1F - Palette RAM indeces. $3F20-$3FFF - mirrors of $3F00-$3F1F
		break;

	default:
		throw std::runtime_error(std::format("Invalid address ${:X} given as argument to PPU::ReadMemory(u16, u8). The range is $0000-$3FFF.", addr));
	}
}


void PPU::PrepareForNewFrame()
{
	odd_frame = !odd_frame;
	framebuffer_pos = 0;
}


void PPU::PrepareForNewScanline()
{
	if (scanline == standard.num_scanlines - 2) // E.g. on NTSC, num_scanlines == 262, and we jump straight from 260 to -1 (pre-render).
	{
		scanline = -1;
		PrepareForNewFrame();
	}
	else
	{
		scanline++;
	}
	pixel_x_pos = 0;
	sprite_evaluation.sprite_0_included_current_scanline = sprite_evaluation.sprite_0_included_next_scanline;
	sprite_evaluation.sprite_0_included_next_scanline = false;
}


void PPU::SetWindowScale(const unsigned scale)
{
	this->window_scale = scale;
}


void PPU::SetWindowSize(const unsigned width, const unsigned height)
{
	if (width > 0 && height > 0)
	{
		window_scale_temp = std::min(width / num_pixels_per_scanline, height / standard.num_visible_scanlines);
		window_pixel_offset_x_temp = 0.5 * (width - window_scale_temp * num_pixels_per_scanline);
		window_pixel_offset_y_temp = 0.5 * (height - window_scale_temp * standard.num_visible_scanlines);
		reset_graphics_after_render = true;
	}
}


void PPU::StreamState(SerializationStream& stream)
{
	/* I've tried to follow the order of the declarations in the class definition. */
	stream.StreamPrimitive(open_bus_io);
	stream.StreamPrimitive(scroll);
	stream.StreamPrimitive(sprite_evaluation);
	stream.StreamPrimitive(tile_fetcher);

	stream.StreamPrimitive(a12);
	stream.StreamPrimitive(cpu_cycles_since_a12_set_low);

	stream.StreamPrimitive(cycle_340_was_skipped_on_last_scanline);
	stream.StreamPrimitive(NMI_line);
	stream.StreamPrimitive(odd_frame);
	stream.StreamPrimitive(reset_graphics_after_render);
	stream.StreamPrimitive(set_sprite_0_hit_flag);

	stream.StreamPrimitive(pixel_x_pos);
	stream.StreamPrimitive(PPUCTRL);
	stream.StreamPrimitive(PPUMASK);
	stream.StreamPrimitive(PPUSTATUS);
	stream.StreamPrimitive(PPUSCROLL);
	stream.StreamPrimitive(PPUDATA);
	stream.StreamPrimitive(OAMADDR);
	stream.StreamPrimitive(OAMADDR_at_cycle_65);
	stream.StreamPrimitive(OAMDMA);

	stream.StreamPrimitive(scanline);

	stream.StreamPrimitive(cpu_cycle_counter);
	stream.StreamPrimitive(framebuffer_pos);
	stream.StreamPrimitive(scanline_cycle);
	stream.StreamPrimitive(secondary_oam_sprite_index);
	stream.StreamPrimitive(window_scale);
	stream.StreamPrimitive(window_scale_temp);
	stream.StreamPrimitive(window_pixel_offset_x);
	stream.StreamPrimitive(window_pixel_offset_x_temp);
	stream.StreamPrimitive(window_pixel_offset_y);
	stream.StreamPrimitive(window_pixel_offset_y_temp);

	stream.StreamArray(oam);
	stream.StreamArray(palette_ram);
	stream.StreamArray(secondary_oam);

	stream.StreamArray(sprite_attribute_latch);
	stream.StreamArray(sprite_pattern_shift_reg);
	stream.StreamArray(bg_palette_attr_reg);
	stream.StreamArray(bg_pattern_shift_reg);

	stream.StreamArray(sprite_x_pos_counter);

	stream.StreamVector(framebuffer);
}


void PPU::StreamConfig(SerializationStream& stream)
{
	stream.StreamPrimitive(window_scale);
}


void PPU::SetDefaultConfig()
{
	window_scale = default_window_scale;
}


void PPU::LogState()
{
	Logging::ppu_state.scanline = scanline;
	Logging::ppu_state.ppu_cycle_counter = scanline_cycle;
}