#include "patty.hh"

#include <cmath>

Value Dynamic_Generator::take(Context &ctx, unsigned n)
{
	Value result;
	result.type = Value::Type::List;
	for (int64_t i = 0; i < n; ++i) {
		auto local_scope_guard = ctx.local_scope();
		ctx.assign("n", Value::integer(i + start));
		result.list.push_back(eval(ctx, expr));
	}
	return result;
}

Value Dynamic_Generator::len(Context &)
{
	return Value::nil();
}

Value Dynamic_Generator::pop(Context&, unsigned n)
{
	auto copy = *this;
	copy.start += n;

	Value retval;
	retval.type = Value::Type::Sequence;
	retval.sequence = std::make_shared<Dynamic_Generator>(copy);
	return retval;
}

Value Circular_Generator::take(Context &ctx, unsigned n)
{
	Value result;
	result.type = Value::Type::List;
	for (int64_t i = 0; i < n; ++i) {
		auto local_scope_guard = ctx.local_scope();
		ctx.assign("n", Value::integer(i));
		result.list.push_back(eval(ctx, value_set.at(i % value_set.list.size())));
	}
	return result;
}

Value Circular_Generator::len(Context&)
{
	return Value::integer(value_set.list.size());
}


Value Circular_Generator::pop(Context &ctx, unsigned n)
{
	auto copy = *this;

	if (n >= copy.value_set.list.size()) return Value::nil();
	do copy.value_set.list.pop_front(); while (n-- >= 1);

	Value retval;
	retval.type = Value::Type::Sequence;
	retval.sequence = std::make_shared<Circular_Generator>(copy);
	return retval;
}

Value Composed_Generator::take(Context &ctx, unsigned n)
{
	Value result;
	result.type = Value::Type::List;

	for (;;) {
		for (auto& gen : children) {
			if (auto circular = dynamic_cast<Circular_Generator*>(gen.get()); circular != nullptr) {
				unsigned copied = std::min(circular->value_set.list.size(), (size_t)n);

				auto it = circular->value_set.list.begin();
				for (auto i = 0u; i < copied; ++i, ++it)
					result.list.push_back(eval(ctx, *it));

				if (copied == n)
					return result;
				else
					n -= copied;
			} else {
				auto rest = gen->take(ctx, n);
				std::ranges::move(std::move(rest.list), std::back_inserter(result.list));
				return result;
			}
		}
	}
}

Value Composed_Generator::len(Context &ctx)
{
	auto sum = Value::integer(0);
	for (auto const& gen : children)
		if (auto r = gen->len(ctx); r.type == Value::Type::Nil)
			return Value::nil();
		else
			sum += r;
	return sum;
}


Value Composed_Generator::pop(Context &, unsigned )
{
	assert(false && "unimplemented");
}
