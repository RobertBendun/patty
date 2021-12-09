patty: patty.cc
	g++ -std=c++20 -Wall -Wextra -lfmt -o $@ $< -ggdb

.PHONY: clean
clean:
	rm -f patty
