#include <LIEF/ELF.hpp>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

static constexpr std::string_view renamed_suffix = ".llvm.";

int main(int argc, char** argv)
{
	if (argc < 5)
	{
		std::fprintf(stderr, "usage: %s <program> <hook> <output> <library to load>...\n", argv[0]);
		return EXIT_FAILURE;
	}

	auto program = LIEF::ELF::Parser::parse(argv[1]);
	auto hook = LIEF::ELF::Parser::parse(argv[2]);
	if (!program || !hook) return EXIT_FAILURE;

	std::map<std::string, const LIEF::ELF::Symbol*> local;
	std::map<std::string, std::vector<const LIEF::ELF::Symbol*>> renamed;
	for (const auto& symbol : program->symtab_symbols())
	{
		const std::string& name = symbol.name();
		if (name.empty() || !symbol.value()) continue;
		if (const auto at = name.find(renamed_suffix); at != std::string::npos) renamed[name.substr(0, at)].push_back(&symbol);
		else local.emplace(name, &symbol);
	}

	std::set<std::string> exported;
	for (const auto& symbol : program->dynamic_symbols())
	{
		if (symbol.value()) exported.insert(symbol.name());
	}

	std::map<std::string, const LIEF::ELF::Symbol*> wanted;
	bool shadowed = false;
	for (const auto& symbol : hook->dynamic_symbols())
	{
		const std::string& name = symbol.name();
		if (symbol.value() && renamed.contains(name))
		{
			std::fprintf(stderr, "bind-live-save: the hook defines its own %s, which the program renamed\n", name.c_str());
			shadowed = true;
		}
		if (!symbol.value() && !local.contains(name) && renamed.contains(name))
		{
			const auto& candidates = renamed.at(name);
			if (candidates.size() != 1)
			{
				std::fprintf(stderr, "bind-live-save: the program has %zu renamed copies of %s\n", candidates.size(), name.c_str());
				shadowed = true;
			}
			else wanted.emplace(name, candidates.front());
		}
		if (local.contains(name) && !exported.contains(name)) wanted.emplace(name, local.at(name));
	}
	if (shadowed) return EXIT_FAILURE;

	program->header().section_headers_offset(program->eof_offset());
	if (!program->relocate_phdr_table(LIEF::ELF::Binary::PHDR_RELOC::PIE_SHIFT))
	{
		std::fprintf(stderr, "bind-live-save: cannot make room for more segments\n");
		return EXIT_FAILURE;
	}
	for (const auto& [name, symbol] : wanted)
	{
		LIEF::ELF::Symbol alias = *symbol;
		alias.name(name);
		program->export_symbol(alias);
	}
	for (int library = 4; library < argc; library++) program->add_library(argv[library]);
	program->write(argv[3]);
	std::printf("bind-live-save: exported %zu symbols\n", wanted.size());
	return EXIT_SUCCESS;
}
