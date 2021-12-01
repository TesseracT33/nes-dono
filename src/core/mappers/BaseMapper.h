#pragma once

#include <algorithm>
#include <format>
#include <vector>

#include "MapperProperties.h"

#include "../Component.h"
#include "../System.h"

#include "../../Types.h"

#include "../../gui/AppUtils.h"

class BaseMapper : public Component
{
public:
	using Component::Component;

	BaseMapper(const std::vector<u8> chr_prg_rom, MapperProperties properties) : properties(properties)
	{
		/* These must be calculated here, and cannot be part of the properties passed to the submapper constructor,
		   as bank sizes are not known before the submapper constructors have been called. */
		this->properties.num_chr_banks = properties.chr_size / properties.chr_bank_size;
		this->properties.num_prg_ram_banks = properties.prg_ram_size / properties.prg_ram_bank_size;
		this->properties.num_prg_rom_banks = properties.prg_rom_size / properties.prg_rom_bank_size;

		/* Resize all vectors */
		chr.resize(properties.chr_size);
		prg_ram.resize(properties.prg_ram_size);
		prg_rom.resize(properties.prg_rom_size);

		/* Fill vectors with either rom data or $00 */
		std::copy(chr_prg_rom.begin(), chr_prg_rom.begin() + properties.prg_rom_size, prg_rom.begin());

		if (!properties.has_chr_ram)
			std::copy(chr_prg_rom.begin() + properties.prg_rom_size, chr_prg_rom.end(), chr.begin());
		else
			std::fill(chr.begin(), chr.end(), 0x00);

		if (!prg_ram.empty())
			std::fill(prg_ram.begin(), prg_ram.end(), 0x00);
	}

	const System::VideoStandard GetVideoStandard() const { return properties.video_standard; };

	void ReadPRGRAMFromDisk()
	{
		if (properties.has_persistent_prg_ram)
		{
			const std::string save_data_path = properties.rom_path + save_file_postfix;
			if (AppUtils::FileExists(save_data_path))
			{
				std::ifstream ifs{ save_data_path, std::ifstream::in | std::ofstream::binary };
				if (!ifs)
				{
					UserMessage::Show("Save file loading failed!");
					return;
				}
				ifs.read((char*)prg_ram.data(), prg_ram.size());
			}
		}
	}

	void WritePRGRAMToDisk() const
	{
		if (properties.has_persistent_prg_ram)
		{
			static bool save_data_creation_has_failed = false;
			if (!save_data_creation_has_failed) /* Avoid the spamming of user messages, since this function is called regularly. */
			{
				const std::string save_data_path = properties.rom_path + save_file_postfix;
				std::ofstream ofs{ save_data_path, std::ofstream::out | std::ofstream::binary };
				if (!ofs)
				{
					save_data_creation_has_failed = true;
					UserMessage::Show("Save file creation failed!");
					return;
				}
				ofs.write((const char*)prg_ram.data(), prg_ram.size());
			}
		}
	}

	virtual u8 ReadPRG(u16 addr) = 0;
	virtual u8 ReadCHR(u16 addr) = 0;

	virtual void WritePRG(u16 addr, u8 data) {};
	virtual void WriteCHR(u16 addr, u8 data) {};

	virtual u16 TransformNametableAddr(u16 addr) = 0;

	virtual void ClockIRQ() {};

protected:
	const std::string save_file_postfix = "_SAVE_DATA.bin";

	MapperProperties properties;

	std::vector<u8> chr; /* Either RAM or ROM (a cart cannot have both). */
	std::vector<u8> prg_ram;
	std::vector<u8> prg_rom;

	// Horizontal mirroring; addresses in $2400-$27FF and $2C00-$2FFF are transformed into $2000-$23FF and $2800-$2BFF, respectively.
	u16 NametableAddrHorizontal(u16 addr) const { return addr & ~0x400; }

	// Vertical mirroring; addresses in $2800-$2FFF are transformed into $2000-$27FF.
	u16 NametableAddrVertical(u16 addr) const { return addr & ~0x800; }

	// Single screen, lower; addresses in $2000-$2FFF are transformed into $2000-$23FF
	u16 NametableAddrSingleLower(u16 addr) const { return addr & ~0xC00; }

	// Single screen, upper; addresses in $2000-$2FFF are transformed into $2400-$27FF
	u16 NametableAddrSingleUpper(u16 addr) const { return addr & ~0x800 | 0x400; }

	// 4-Screen: address are not transformed
	u16 NametableAddrFourScreen(u16 addr) const { return addr; }

	/* The following static functions may be called from submapper constructors.
	   The submapper classes must apply these properties themselves; they cannot be deduced from the rom header. */
	static void SetCHRBankSize(MapperProperties& properties, size_t size)
	{
		properties.chr_bank_size = size;
	}

	static void SetPRGRAMBankSize(MapperProperties& properties, size_t size)
	{
		properties.prg_ram_bank_size = size;
	}

	static void SetPRGROMBankSize(MapperProperties& properties, size_t size)
	{
		properties.prg_rom_bank_size = size;
	}

	/* A submapper constructor must call this function if it has CHR RAM, because if it has RAM instead of ROM,
	   the CHR size specified in the rom header will always (?) be 0. */
	static void SetCHRRAMSize(MapperProperties& properties, size_t size)
	{
		if (properties.has_chr_ram && properties.chr_size == 0)
			properties.chr_size = size;
	}

	/* The PRG RAM size (or PRG RAM presence) may or may not be specified in the rom header,
	   in particular if using iNES and not NES 2.0.
	   For now, let games with mappers that support PRG RAM, always have PRG RAM of some predefined size. */
	static void SetPRGRAMSize(MapperProperties& properties, size_t size)
	{
		if (properties.prg_ram_size == 0)
		{
			properties.has_prg_ram = true;
			properties.prg_ram_size = size;
		}
	}
};

