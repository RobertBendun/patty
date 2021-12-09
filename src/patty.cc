#include "patty.hh"
#include <iostream>
#include <fstream>


namespace fs = std::filesystem;
using namespace std::string_view_literals;
using namespace fmt::literals;

fs::path program_name;
fs::path filename;

void usage()
{
	std::cout << "usage: " << program_name.c_str() << " [options] <filename>\n";
	std::cout << "  where \n";
	std::cout << "    filename is path to Patty program\n\n";
	std::cout << "    options is one of:\n";
	std::cout << "      --ast       print ast\n";
	std::cout << "      --no-eval   don't evaluate\n";
	std::cout << "      --tokens    print tokens\n";
	std::cout << "      -h,--help   print usage info\n";
	std::cout << std::flush;
	std::exit(1);
}
int main(int, char **argv)
{
	program_name = fs::path(*argv++).filename();

	[[maybe_unused]] bool no_eval = false;

	for (; *argv != nullptr; ++argv) {
		if (*argv == "-h"sv || *argv == "--help"sv) {
			usage();
		}

		if (*argv == "--no-eval"sv) { no_eval = true; continue; }

		if (filename.empty()) {
			filename = *argv;
		} else {
			error_fatal("more then one filename was specified");
		}
	}

	if (filename.empty()) {
		error_fatal("REPL mode is not implemented yet");
	}

	std::ifstream source_file(filename);
	if (!source_file) {
		error_fatal("cannot open file '{}'"_format(filename.c_str()));
	}

	std::string code(std::istreambuf_iterator<char>(source_file), {});

	std::string_view source = code;
	auto value = read(source);

	Context ctx;
	intrinsics(ctx);

	if (!no_eval) {
		print(eval(ctx, std::move(value)));
	} else {
		print(value);
	}
}
