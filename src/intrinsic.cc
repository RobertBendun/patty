#include "patty.hh"

#include <iostream>

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
			auto result = eval(ctx, args.at(0));
			for (auto val : args.tail()) { (result.*op)(eval(ctx, std::move(val))); }
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
			auto prev = eval(ctx, args.at(0));
			for (auto val : args.tail()) {
				auto curr = eval(ctx, std::move(val));
				if (!op(prev.ival, curr.ival))
					return Value::integer(false);
				prev = curr;
			}
			return Value::integer(true);
		};
	}

	ctx.define("do") = [](auto& ctx, Value args) {
		for (auto val : args.init()) eval(ctx, std::move(val));
		return eval(ctx, args.list.back());
	};

	ctx.define("def") = [](auto& ctx, Value args) {
		assert(args.type == Value::Type::List);
		assert(args.list.front().type == Value::Type::Symbol);
		ctx.assign(args.list.front().sval, eval(ctx, std::move(*std::next(args.list.begin()))));
		return Value::nil();
	};

	ctx.define("print") = [](auto& ctx, Value args) {
		for (auto& arg : args.list)
			arg = eval(ctx, std::move(arg));

		fmt::print("{}\n", fmt::join(args.list, ""));
		return Value::nil();
	};

	ctx.define("fun") = [](auto&, Value args) { return args; };
	ctx.define("list") = [](auto&, Value args) { return args; };

	ctx.define("if") = [](auto& ctx, Value args) {
		auto condition = eval(ctx, args.at(0));
		if (condition.coarce_bool())
			return eval(ctx, args.at(1));
		if (args.list.size() > 2)
			return eval(ctx, args.at(2));
		return Value::nil();
	};

	ctx.define("++") = [](auto& ctx, Value args) {
		Value list;
		list.type = Value::Type::List;
		for (auto arg : args.list) {
			Value v = eval(ctx, std::move(arg));
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

	ctx.define("index") = [](Context &ctx, Value args) {
		auto index = eval(ctx, args.at(0));
		auto list = eval(ctx, args.at(1));
		assert(index.type == Value::Type::Int);
		assert(list.type == Value::Type::List);
		return list.at(index.ival);
	};

	ctx.define("for") = [](Context &ctx, Value args) {
		for (auto arg : eval(ctx, args.at(1)).list) {
			auto local_scope_guard = ctx.local_scope();

			switch (args.at(0).type) {
			case Value::Type::Symbol:
				ctx.assign(args.at(0).sval, eval(ctx, arg));
				break;

			case Value::Type::List:
				{
					unsigned i = 0;
					for (auto const& name : args.at(0).list) {
						assert(name.type == Value::Type::Symbol);
						assert(i < arg.list.size());
						ctx.assign(name.sval, eval(ctx, std::move(arg.at(i++))));
					}
				}
				break;

			default:
				assert(false && "wrong type");
			}
			eval(ctx, args.at(2));
		}

		return Value::nil();
	};

	ctx.define("zip") = [](auto& ctx, Value args) {
		std::vector<decltype(args.list)> lists;
		std::vector<decltype(args.list.begin())> iters;
		for (auto arg : args.list) {
			auto list = eval(ctx, std::move(arg));
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

	ctx.define("zip-with") = [](auto& ctx, Value args) {
		std::vector<decltype(args.list)> lists;
		std::vector<decltype(args.list.begin())> iters;
		auto op = args.at(0);

		for (auto arg : args.tail()) {
			auto list = eval(ctx, std::move(arg));
			assert(list.type == Value::Type::List);
			auto &ref = lists.emplace_back(list.list);
			iters.emplace_back(ref.begin());
		}

		Value result;
		result.type = Value::Type::List;
		for (;;) {
			if (!std::ranges::all_of(iters, [lists = lists.begin()](auto it) mutable { return lists++->end() != it; }))
				break;


			Value call;
			call.type = Value::Type::List;
			call.list.push_front(args.at(0));
			for (auto &it : iters) {
				call.list.push_back(std::move(*it++));
			}

			result.list.emplace_back(eval(ctx, call));
		}

		return result;
	};

	ctx.define("take") = [](auto &ctx, Value args) {
		Value count = eval(ctx, args.at(0));
		Value from = eval(ctx, args.at(1));
		assert(count.type == Value::Type::Int);
		assert(count.ival >= 0);
		return from.take(ctx, count.ival);
	};

	ctx.define("tail") = [](auto &ctx, Value args) {
		Value tail;
		tail.type = Value::Type::List;
		auto source = eval(ctx, args.at(0));
		tail.list.assign(std::next(source.list.begin()), source.list.end());
		return tail;
	};

	ctx.define("fold") = [](auto &ctx, Value args) {
		Value collection = eval(ctx, args.at(1));

		Value invoke;
		invoke.type = Value::Type::List;
		invoke.list.push_back(args.at(0));
		invoke.list.push_back(collection.at(0));
		for (auto el : collection.tail()) {
			invoke.list.push_back(std::move(el));
			invoke.at(1) = eval(ctx, invoke);
			invoke.list.pop_back();
		}
		return invoke.at(1);
	};

	ctx.define("loop") = [](auto &ctx, Value args) {
		for (;;) {
			for (auto arg : args.list)
				eval(ctx, arg);
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


	ctx.define("seq") = [](auto &ctx, Value args) {
		Value seq;
		seq.type = Value::Type::Sequence;

		if (args.is_static_expression(ctx)) {
			auto gen = std::make_shared<Circular_Generator>();
			gen->value_set = std::move(args);
			seq.sequence = std::move(gen);
		} else {
			auto end_of_statics = std::find_if(args.list.begin(), args.list.end(), [&ctx](auto &val) {
				return !val.is_static_expression(ctx);
			});

			if (end_of_statics == args.list.begin()) {
				auto gen = std::make_shared<Dynamic_Generator>();
				gen->expr = std::move(args.at(0));
				seq.sequence = std::move(gen);
			} else {
				auto composed = std::make_shared<Composed_Generator>();

				auto circular = std::make_shared<Circular_Generator>();
				for (auto statics = args.list.begin(); statics != end_of_statics; ++statics) {
					circular->value_set.list.push_back(std::move(*statics));
				}
				composed->children.push_back(std::move(circular));

				auto dynamic = std::make_shared<Dynamic_Generator>();
				dynamic->expr = std::move(*end_of_statics);
				composed->children.push_back(std::move(dynamic));

				seq.sequence = std::move(composed);
			}
		}
		return seq;
	};

	ctx.define("seq!") = [](auto &ctx, Value args) {
		args.subst(ctx);
		return ctx.scopes.front()["seq"].cpp_function(ctx, args);
	};

	// TODO string support
	ctx.define("pop") = [](Context &ctx, Value args)
	{
		auto count = eval(ctx, args.at(0));
		auto collection = eval(ctx, args.at(1));
		assert(count.type == Value::Type::Int);

		switch (collection.type) {
		case Value::Type::Sequence:
			return collection.sequence->pop(ctx, count.ival);
		case Value::Type::List:
			{
				auto to_pop = std::min((uint64_t)count.ival, collection.list.size());
				for (auto i = 0u; i < to_pop; ++i) {
					collection.list.pop_front();
				}
				return collection;
			}

		default:
			error_fatal("pop only supports list and sequences");
		}
	};
}
