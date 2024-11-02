#pragma once
#include <iostream>
#include <set>
#include <functional>
using namespace std::literals;
extern char *program_invocation_short_name;
struct options {
    void help(bool error) const {
        auto &stream = error ? std::cerr : std::cout;
        stream << "usage:\n" << program_invocation_short_name << '\n';
        for (const auto &[set, _] : opts) {
            for (const auto &s : set)
                stream << '\t' << s;
            stream << '\n';
        }
        exit(error ? 1 : 0);
    }

    std::vector<std::pair<std::set<std::string_view>, std::function<void()>>> opts;

    options(decltype(opts) args)
        : opts(args) {
        opts.push_back({{"--help"sv, "-h"sv}, [this] { help(false); }});
    }

    void operator()(std::string_view arg) const {
        for (const auto &[set, lambda] : opts) {
            if (set.contains(arg))
                return lambda();
        }
        std::cout << "unknown option: " << arg << '\n';
        help(true);
    }
};
