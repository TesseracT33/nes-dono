#include "Logging.h"


#ifdef DEBUG_LOG
std::ofstream Logging::log_ofs{ DEBUG_LOG_PATH, std::ofstream::out };
#endif


Logging::APUState Logging::apu_state{};
Logging::CPUState Logging::cpu_state{};
Logging::PPUState Logging::ppu_state{};


void Logging::Update()
{
#ifdef DEBUG_LOG
	LogLine();
#endif

#ifdef DEBUG_COMPARE_MESEN
	CompareMesenLogLine();
#endif
}


// Test the value of 'sub_str' as it is found on a Mesen trace log line 'log_line'.
// 'emu_value' is the corresponding value in our emu (should be either uint8 or uint16)
bool Logging::TestString(const std::string& log_line, unsigned line_num, 
	const std::string& substr, int emu_value, NumberFormat num_format)
{
	// Find the corresponding numerical value of 'sub_str', wherever it occurs on the line (e.g. A:FF)
	size_t start_pos_of_val = log_line.find(substr + ":") + substr.length() + 1;
	size_t end_pos_of_val = log_line.find(" ", start_pos_of_val);
	std::string val_str;
	if (end_pos_of_val == std::string::npos)
		val_str = log_line.substr(start_pos_of_val);
	else
		val_str = log_line.substr(start_pos_of_val, end_pos_of_val - start_pos_of_val);

	int base;
	switch (num_format)
	{
	case NumberFormat::uint8_hex: case NumberFormat::uint16_hex: base = 16; break;
	case NumberFormat::uint32_dec: case NumberFormat::uint64_dec: base = 10; break;
	}
	int val = std::stoi(val_str, nullptr, base);

	// Compare the Mesen value against our emu value
	if (val != emu_value)
	{
		std::string msg;
		switch (num_format)
		{
		case NumberFormat::uint8_hex : msg = "Incorrect {} at line {}; expected ${:02X}, got ${:02X}"; break;
		case NumberFormat::uint16_hex: msg = "Incorrect {} at line {}; expected ${:04X}, got ${:04X}"; break;
		case NumberFormat::uint32_dec:
		case NumberFormat::uint64_dec: msg = "Incorrect {} at line {}; expected {}, got {}"; break;
		}
		UserMessage::Show(std::format(msg, substr, line_num, val, emu_value), UserMessage::Type::Warning);
		return false;
	}
	return true;
}


#ifdef DEBUG_LOG
void Logging::LogLine()
{
	if (cpu_state.NMI)
	{
		log_ofs << "<<< NMI handled >>>" << '\n';
		return;
	}
	if (cpu_state.IRQ)
	{
		log_ofs << "<<< IRQ handled >>>" << '\n';
		return;
	}

	const std::string output = std::format(
		"CPU cycle {} \t PC:{:04X} \t OP:{:02X} \t SP:{:02X}  A:{:02X}  X:{:02X}  Y:{:02X}  P:{:02X}  SL:{}  PPU cycle:{}",
		cpu_state.cpu_cycle_counter, cpu_state.PC, cpu_state.opcode, cpu_state.SP,
		cpu_state.A, cpu_state.X, cpu_state.Y, cpu_state.P, ppu_state.scanline, ppu_state.ppu_cycle_counter);

	log_ofs << output << '\n';
}
#endif


#ifdef DEBUG_COMPARE_MESEN
void Logging::CompareMesenLogLine()
{
	// Each line in the mesen trace log should be something of the following form:
	// 8000 $78    SEI                A:00 X:00 Y:00 P:04 SP:FD CYC:27  SL:0   CPU Cycle:8

	static std::ifstream ifs{ MESEN_LOG_PATH, std::ifstream::in };
	static std::string current_line;
	static unsigned line_counter = 0;

	// Get the next line in the mesen trace log
	if (ifs.eof())
	{
		UserMessage::Show("Mesen trace log comparison passed.", UserMessage::Type::Success);
		return;
	}
	std::getline(ifs, current_line);
	line_counter++;

	// Some lines are of a different form: [NMI - Cycle: 206085]
	// Check whether an NMI occured here
	if (current_line.find("NMI") != std::string::npos)
	{
#ifdef DEBUG_COMPARE_MESEN_NMI
		if (!cpu_state.NMI)
			UserMessage::Show(std::format("Expected an NMI at line {}.", line_counter), UserMessage::Type::Warning);
#endif
		return;
	}
#ifdef DEBUG_COMPARE_MESEN_NMI
	else if (cpu_state.NMI)
		UserMessage::Show(std::format("Did not expect an NMI at line {}.", line_counter), UserMessage::Type::Warning);
#endif

	// Check whether an IRQ occured here
	if (current_line.find("IRQ") != std::string::npos)
	{
#ifdef DEBUG_COMPARE_MESEN_IRQ
		if (!cpu_state.IRQ)
			UserMessage::Show(std::format("Expected an IRQ at line {}.", line_counter), UserMessage::Type::Warning);
#endif
		return;
	}
#ifdef DEBUG_COMPARE_MESEN_IRQ
	else if (cpu_state.IRQ)
		UserMessage::Show(std::format("Did not expect an IRQ at line {}.", line_counter), UserMessage::Type::Warning);
#endif

	// Test PC
	std::string mesen_pc_str = current_line.substr(0, 4);
	u16 mesen_pc = std::stoi(mesen_pc_str, nullptr, 16);
	if (cpu_state.PC != mesen_pc)
	{
		UserMessage::Show(
			std::format("Incorrect PC at line {}; expected ${:04X}, got ${:04X}", line_counter, mesen_pc, cpu_state.PC), UserMessage::Type::Warning);
		return;
	}

	// Test cpu cycle
	TestString(current_line, line_counter, "CPU Cycle", cpu_state.cpu_cycle_counter, NumberFormat::uint32_dec);

	// Test cpu registers
	TestString(current_line, line_counter, "A", cpu_state.A, NumberFormat::uint8_hex);
	TestString(current_line, line_counter, "X", cpu_state.X, NumberFormat::uint8_hex);
	TestString(current_line, line_counter, "Y", cpu_state.Y, NumberFormat::uint8_hex);
	TestString(current_line, line_counter, "SP", cpu_state.SP, NumberFormat::uint8_hex);
	TestString(current_line, line_counter, "P", cpu_state.P, NumberFormat::uint8_hex);

	// Test ppu cycle counter and scanline
#ifdef DEBUG_COMPARE_MESEN_PPU
	TestString(current_line, line_counter, "CYC", ppu_state.ppu_cycle_counter, NumberFormat::uint32_dec);
	TestString(current_line, line_counter, "SL", ppu_state.scanline, NumberFormat::uint32_dec);
#endif
}
#endif