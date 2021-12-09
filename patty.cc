#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <map>
#include <optional>
#include <ranges>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <fmt/ostream.h>

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

void error(auto const& message)
{
	fmt::print(stderr, "{}: error: {}\n", program_name.c_str(), message);
}

[[noreturn]]
void error_fatal(auto const& ...message)
{
	error(message...);
	std::exit(1);
}

struct Value
{
	enum class Type
	{
		Nil,
		String,
		Symbol,
		Int,
		List,
		Cpp_Function,
	} type = Type::Nil;

	std::string sval = {};
	std::int64_t ival = 0;
	std::list<Value> list = {};
	std::function<Value(struct Context&, Value)> cpp_function = nullptr;

	static Value nil() { return {}; }
	static Value string(std::string_view src) { return Value { Type::String, std::string(src.data(), src.size()) }; }
	static Value symbol(std::string_view src) { return Value { Type::Symbol, std::string(src.data(), src.size()) }; }
	static Value cpp(char const *name, std::function<Value(struct Context&, Value)> &&function) { auto v = Value { Type::Cpp_Function }; v.cpp_function = std::move(function); v.sval = name; return v; }
	static Value integer(int64_t ival) { auto v = Value { Type::Int }; v.ival = ival; return v; }

	Value& at(unsigned index) &;
	Value&& at(unsigned index) &&;

	inline auto tail() { return std::ranges::subrange(std::next(list.begin()), list.end()); }
	inline auto tail() const { return std::ranges::subrange(std::next(list.cbegin()), list.cend()); }
	inline auto init() { return std::ranges::subrange(list.begin(), std::prev(list.end())); }
	inline auto init() const { return std::ranges::subrange(list.cbegin(), std::prev(list.cend())); }

	bool coarce_bool() const;

	void operator+=(Value const& other);
	void operator-=(Value const& other);
	void operator*=(Value const& other);
};

bool Value::coarce_bool() const
{
	switch (type) {
	case Type::Cpp_Function: return true;
	case Type::Int: return ival != 0;
	case Type::List: return !list.empty();
	case Type::Nil: return false;
	case Type::String: return !sval.empty();
	case Type::Symbol: return true;
	}

	return false;
}

Value& Value::at(unsigned index) &
{
	return *std::next(list.begin(), index);
}

Value&& Value::at(unsigned index) &&
{
	return std::move(*std::next(list.begin(), index));
}

void Value::operator+=(Value const& other)
{
	assert(type == Type::Int);
	assert(other.type == Type::Int);
	ival += other.ival;
}

void Value::operator-=(Value const& other)
{
	assert(type == Type::Int);
	assert(other.type == Type::Int);
	ival -= other.ival;
}

void Value::operator*=(Value const& other)
{
	assert(type == Type::Int);
	assert(other.type == Type::Int);
	ival *= other.ival;
}

struct Context
{
	std::vector<std::unordered_map<std::string, Value>> scopes;

	Value* operator[](std::string const& name)
	{
		for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
			if (scope->contains(name))
				return &(*scope)[name];
		return nullptr;
	}

	void assign(std::string const& name, Value value)
	{
		scopes.back().emplace(name, std::move(value));
	}

	auto define(char const* val)
	{
		struct Define_Descriptor
		{
			auto operator=(decltype(Value{}.cpp_function) func)
			{
				ctx->assign(name, Value::cpp(name, std::move(func)));
			}

			char const* name;
			Context *ctx;
		};

		return Define_Descriptor{val, this};
	}

	auto local_scope()
	{
		struct Scope_Guard
		{
			~Scope_Guard()
			{
				ctx->scopes.pop_back();
			}

			Context *ctx;
		};

		scopes.emplace_back();
		return Scope_Guard{this};
	}
};

template<>
struct fmt::formatter<Value> : fmt::formatter<std::string_view>
{
	thread_local static inline bool in_list = false;

	template<typename FC>
	auto format(Value const& value, FC &fc)
	{
		switch (value.type) {
		case Value::Type::Nil:
			return fmt::format_to(fc.out(), "nil");
		case Value::Type::String:
			if (in_list)
				return fmt::format_to(fc.out(), "{}", std::quoted(value.sval));
			else
				return fmt::format_to(fc.out(), "{}", value.sval);
		case Value::Type::Symbol:
			return fmt::format_to(fc.out(), "{}", value.sval);

		case Value::Type::Int:
			return fmt::format_to(fc.out(), "{}", value.ival);

		case Value::Type::List: {
			in_list = true;
			auto result = fmt::format_to(fc.out(), "({})", fmt::join(value.list, " "));
			in_list = false;
			return result;
		}
		case Value::Type::Cpp_Function:
			return fmt::format_to(fc.out(), "<cpp-function {}>", value.sval);
		}

		assert(false && "unreachable");
	}
};

void print(Value const& value)
{
	fmt::print("{}\n", value);
}

namespace shared
{
	Value read(std::string_view &source)
	{
		while (!source.empty()) {
			for (; !source.empty() && std::isspace(source.front()); source.remove_prefix(1)) {}
			if (source.starts_with('#')) {
				source.remove_prefix(source.find('\n'));
			} else {
				break;
			}
		}

		if (source.empty())
			return Value::nil();

		if (source.starts_with('"')) {
			auto end = std::adjacent_find(std::cbegin(source)+1, std::cend(source), [](char prev, char curr) {
				return prev != '\\' && curr == '"';
			});

			auto str = Value::string({ std::cbegin(source) + 1, end + 1 }); // TODO add escaping like \n
			source.remove_prefix(std::distance(std::cbegin(source), end + 2));
			return str;
		}

		if (std::isdigit(source.front()) || (source.front() == '-' && std::isdigit(source[1]))) {
			auto value = Value();
			value.type = Value::Type::Int;
			auto [p, ec] = std::from_chars(&source.front(), &source.back() + 1, value.ival);
			assert(p != &source.front());
			source.remove_prefix(p - &source.front());
			return value;
		}

		static constexpr std::string_view Valid_Symbol_Char = "+-*/%$@!^&[]:;<>,.|=";
		if (std::isalpha(source.front()) || Valid_Symbol_Char.find(source.front()) != std::string_view::npos) {
			auto end = std::find_if(source.cbegin()+1, source.cend(), [](char curr) {
				return !std::isalnum(curr) && Valid_Symbol_Char.find(curr) == std::string_view::npos;
			});
			auto symbol = Value::symbol({ std::cbegin(source), end });
			source.remove_prefix(std::distance(std::cbegin(source), end));
			return symbol;
		}

		if (source.starts_with('(')) {
			Value list, elem;
			list.type = Value::Type::List;
			source.remove_prefix(1);
			while ((elem = read(source)).type != Value::Type::Nil) {
				list.list.push_back(std::move(elem));
			}
			return list;
		}

		if (source.starts_with(')')) {
			source.remove_prefix(1);
		}

		return Value::nil();
	}

	Value eval(Context &ctx, Value value)
	{
		switch (value.type) {
		case Value::Type::Int:
		case Value::Type::Nil:
		case Value::Type::Cpp_Function:
		case Value::Type::String:
			return value;

		case Value::Type::Symbol:
			if (auto resolved = ctx[value.sval]; resolved) {
				return *resolved;
			} else {
				error_fatal("Cannot resolve symbol {}"_format(value.sval));
			}

		case Value::Type::List:
			{
				// assert(!value.list.empty());
				if (value.list.empty())
					return Value::nil();

				auto callable = eval(ctx, value.list.front());
				switch (callable.type) {
				case Value::Type::Cpp_Function:
					value.list.pop_front();
					return callable.cpp_function(ctx, std::move(value));

				case Value::Type::List:
					{
						// Local scope
						ctx.scopes.emplace_back();

						auto const& formal = callable.list.front();
						assert(formal.type == Value::Type::List);
						auto formal_it = formal.list.begin();
						for (auto arg = std::next(value.list.begin()); arg != value.list.end(); ++arg, ++formal_it) {
							assert(formal_it->type == Value::Type::Symbol);
							ctx.assign(formal_it->sval, eval(ctx, std::move(*arg)));
						}
						assert(formal_it == formal.list.end()); // TODO not all parameters were provided
						auto result = eval(ctx, *std::next(callable.list.begin()));
						ctx.scopes.pop_back();
						return result;
					}

				default:
					return value;
				}
			}
		}

		return Value::nil();
	}
}

void intrinsics(Context &ctx)
{
	if (ctx.scopes.empty())
		ctx.scopes.emplace_back();

	static constexpr auto Math_Operations = std::array {
		std::tuple { "+", &Value::operator+= },
		std::tuple { "-", &Value::operator-= },
		std::tuple { "*", &Value::operator*= }
	};


	for (auto [name, op] : Math_Operations) {
		ctx.define(name) = [op = op](auto& ctx, Value args) {
			auto result = shared::eval(ctx, args.at(0));
			for (auto val : args.tail()) { (result.*op)(shared::eval(ctx, std::move(val))); }
			return result;
		};
	}

	static constexpr auto Comparisons = std::array {
		std::tuple { "<",  +[](int64_t a, int64_t b) { return a < b; } },
		std::tuple { "<=", +[](int64_t a, int64_t b) { return a <= b; } },
		std::tuple { "!=", +[](int64_t a, int64_t b) { return a != b; } },
		std::tuple { "=", +[](int64_t a, int64_t b) { return a == b; } }
	};

	for (auto [name, op] : Comparisons) {
		ctx.define(name) = [op = op](auto& ctx, Value args) {
			auto prev = shared::eval(ctx, args.at(0));
			for (auto val : args.tail()) {
				auto curr = shared::eval(ctx, std::move(val));
				if (!op(prev.ival, curr.ival))
					return Value::integer(false);
				prev = curr;
			}
			return Value::integer(true);
		};
	}

	ctx.define("do") = [](auto& ctx, Value args) {
		for (auto val : args.init()) shared::eval(ctx, std::move(val));
		return shared::eval(ctx, args.list.back());
	};

	ctx.define("def") = [](auto& ctx, Value args) {
		assert(args.type == Value::Type::List);
		assert(args.list.front().type == Value::Type::Symbol);
		ctx.assign(args.list.front().sval, shared::eval(ctx, std::move(*std::next(args.list.begin()))));
		return Value::nil();
	};

	ctx.define("print") = [](auto& ctx, Value args) {
		for (auto& arg : args.list)
			arg = shared::eval(ctx, std::move(arg));

		fmt::print("{}\n", fmt::join(args.list, ""));
		return Value::nil();
	};

	ctx.define("fun") = [](auto&, Value args) { return args; };
	ctx.define("list") = [](auto&, Value args) { return args; };

	ctx.define("if") = [](auto& ctx, Value args) {
		auto condition = shared::eval(ctx, args.at(0));
		if (condition.coarce_bool())
			return shared::eval(ctx, args.at(1));
		if (args.list.size() > 2)
			return shared::eval(ctx, args.at(2));
		return Value::nil();
	};

	ctx.define("++") = [](auto& ctx, Value args) {
		Value list;
		list.type = Value::Type::List;
		for (auto arg : args.list) {
			Value v = shared::eval(ctx, std::move(arg));
			switch (v.type) {
			case Value::Type::Nil:
				continue;
			case Value::Type::List:
				if (v.list.empty())
					continue;
				for (auto e : std::move(v.list)) {
					list.list.push_back(std::move(e));
				}
				break;
			default:
				list.list.push_back(std::move(v));
			}
		}
		return list;
	};

	ctx.define("for") = [](Context &ctx, Value args) {
		for (auto arg : shared::eval(ctx, args.at(1)).list) {
			auto local_scope_guard = ctx.local_scope();

			switch (args.at(0).type) {
			case Value::Type::Symbol:
				ctx.assign(args.at(0).sval, arg);
				break;

			case Value::Type::List:
				{
					unsigned i = 0;
					for (auto const& name : args.at(0).list) {
						assert(name.type == Value::Type::Symbol);
						assert(i < arg.list.size());
						ctx.assign(name.sval, shared::eval(ctx, std::move(arg.at(i++))));
					}
				}
				break;

			default:
				assert(false && "wrong type");
			}
			shared::eval(ctx, args.at(2));
		}

		return Value::nil();
	};

	ctx.define("zip") = [](auto& ctx, Value args) {
		std::vector<decltype(args.list)> lists;
		std::vector<decltype(args.list.begin())> iters;
		for (auto arg : args.list) {
			auto list = shared::eval(ctx, std::move(arg));
			assert(list.type == Value::Type::List);
			auto &ref = lists.emplace_back(list.list);
			iters.emplace_back(ref.begin());
		}

		Value result;
		result.type = Value::Type::List;
		for (;;) {
			if (!std::ranges::all_of(iters, [lists = lists.begin()](auto it) mutable { return lists++->end() != it; }))
				break;

			auto &list = result.list.emplace_back();
			list.type = Value::Type::List;
			for (auto &it : iters) {
				list.list.push_back(std::move(*it++));
			}
		}

		return result;
	};

	ctx.define("take") = [](auto &ctx, Value args) {
		Value count = shared::eval(ctx, args.at(0));
		Value from = shared::eval(ctx, args.at(1));
		assert(count.type == Value::Type::Int);
		assert(from.type == Value::Type::List);
		assert(count.ival >= 0);

		auto to_remove = int64_t(from.list.size()) - count.ival;
		if (to_remove < 0)
			return from;
		from.list.erase(std::next(from.list.begin(), count.ival), from.list.end());
		return from;
	};

	ctx.define("fold") = [](auto &ctx, Value args) {
		Value collection = shared::eval(ctx, args.at(1));

		Value invoke;
		invoke.type = Value::Type::List;
		invoke.list.push_back(args.at(0));
		invoke.list.push_back(collection.at(0));
		for (auto el : collection.tail()) {
			invoke.list.push_back(std::move(el));
			invoke.at(1) = shared::eval(ctx, invoke);
			invoke.list.pop_back();
		}
		return invoke.at(1);
	};

	ctx.define("loop") = [](auto &ctx, Value args) {
		for (;;) {
			for (auto arg : args.list)
				shared::eval(ctx, arg);
		}
		return Value::nil();
	};

	ctx.define("read") = [](auto &, Value args) {
		assert(args.at(0).type == Value::Type::Symbol);

		if (args.at(0).sval == "int") {
			Value result = Value::integer(0);
			std::cin >> result.ival;
			return result;
		}

		assert(false && "unimplemented");
	};
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
	auto value = shared::read(source);

	Context ctx;
	intrinsics(ctx);

	if (!no_eval) {
		print(shared::eval(ctx, std::move(value)));
	} else {
		print(value);
	}
}
