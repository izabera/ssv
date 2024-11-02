#include "opts.hpp"
#include "ssv.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <tuple>
#include <unistd.h>
#include <utility>
#include <vector>

// so this ended up looking a bit complicated
// it just runs the tests exhaustively for all types

// clang-format off
template <unsigned size>
using ssv_types = std::tuple<
    ssv<size, uint8_t>,
    ssv<size, uint16_t>,
    ssv<size, uint32_t>,
    ssv<size, uint64_t>
>;
// clang-format on

template <typename... tuples>
struct tuple_cat_helper {
    using type = decltype(std::tuple_cat(std::declval<tuples>()...));
};

template <typename>
struct generate_ssvs;

template <unsigned... i>
struct generate_ssvs<std::index_sequence<i...>> {
    using type = typename tuple_cat_helper<ssv_types<i * 4 + 16>...>::type;
};

template <typename T>
constexpr auto type_name() {
    auto name = std::string_view{__PRETTY_FUNCTION__};
    auto start = name.find("T = ") + 4;
    auto end = name.rfind("]");
    return name.substr(start, end - start);
}

// these unfortunately can't be part of the unit object
bool tty;
bool verbose = false;
bool errexit = false;
size_t total;
size_t current;

struct unit {
    size_t passcount{}, failcount{};
    unit operator+(const unit &rhs) {
        return {passcount + rhs.passcount, failcount + rhs.failcount};
    }

    void assert_equal(const auto &exprstr, const auto &wantstr, //
                      const auto line, const auto &val, const auto &want) {
        auto ok = val == want;

        // clang-format off
#define RED(...)    (tty?"\x1b[31m":"") << __VA_ARGS__ << (tty?"\x1b[m":"")
#define GREEN(...)  (tty?"\x1b[32m":"") << __VA_ARGS__ << (tty?"\x1b[m":"")
#define YELLOW(...) (tty?"\x1b[33m":"") << __VA_ARGS__ << (tty?"\x1b[m":"")
#define BLUE(...)   (tty?"\x1b[34m":"") << __VA_ARGS__ << (tty?"\x1b[m":"")
        // clang-format on

        auto q = [](const auto &x) {
            if constexpr (requires { std::quoted(x); })
                return std::quoted(x);
            else
                return x;
        };

        std::stringstream ss;
        ss << std::boolalpha;
        if (ok) {
            ss << "[" << GREEN(" OK ") << "]" //
               << line << ": " << BLUE(exprstr) << " == " << GREEN(q(val)) << '\n';
        }
        else {
            ss << "[" << GREEN(" OK ") << "]"                            //
               << line << ": " << BLUE(exprstr) << " == " << RED(q(val)) //
               << " [expected " << GREEN(wantstr) << " (" << YELLOW(q(want)) << ")]\n";
        }

        if (!ok) {
            std::cerr << ss.str();
            failcount++;
            if (errexit)
                exit(1);
        }
        else {
            if (verbose)
                std::cout << ss.str();
            passcount++;
        }
    }

#define assert_equal(expr, want) assert_equal(#expr, #want, __LINE__, expr, want)

    template <typename vectype>
    auto run() {
        if (verbose) {
            std::cout << "(start) " << type_name<vectype>() << '\n';
            std::cout << "Maxstrings = " << vectype::Maxstrings << '\n';
            std::cout << "size = " << sizeof(vectype) << '\n';
        }

        // basic
        vectype strvec;
        assert_equal(strvec.size(), 0u);
        assert_equal(strvec.fullsize(), 0u);

        strvec.push_back("hello");
        strvec.push_back("world");
        assert_equal(strvec[0], "hello");
        assert_equal(strvec[1], "world");

        // is empty after clearing
        strvec.clear();
        assert_equal(strvec.empty(), true);
        strvec.push_back("meow");
        strvec = {};
        assert_equal(strvec.empty(), true);

        // can resize to heap
        strvec.clear();
        size_t total = 0;
        for (size_t i = 0; i < 200; i++) {
            auto str = std::to_string(i);
            assert_equal(strvec.size(), i);
            strvec.push_back(str);
            total += str.size() + 1;
            assert_equal(strvec.size(), i + 1);
            assert_equal(strvec.fullsize(), total);
        }

        // copy assign
        vectype strvec2;
        strvec2.push_back("meow");
        strvec2.push_back(std::string(300, 'q'));
        strvec = strvec2;
        assert_equal(strvec.fullsize(), strvec2.fullsize());
        assert_equal(strvec.fullsize(), 306u);
        assert_equal(strvec.size(), strvec2.size());

        // copy constructor
        vectype strvec3(strvec2);
        assert_equal(strvec3.fullsize(), strvec2.fullsize());
        assert_equal(strvec3.fullsize(), 306u);
        assert_equal(strvec3.size(), strvec2.size());

        // move constructor after going to the heap
        strvec3.push_back(std::string(strvec3.bufsize(), 'q'));
        vectype strvec4(std::move(strvec3));
        assert_equal(strvec4.fullsize(), strvec2.fullsize() + strvec3.bufsize() + 1);
        assert_equal(strvec4.size(), strvec2.size() + 1);

        // move assign
        vectype strvec5 = std::move(strvec4);
        assert_equal(strvec5.size(), strvec2.size() + 1);

        // can immediately go to heap
        strvec.clear();
        strvec.push_back(std::string(200, 'a'));
        assert_equal(strvec.size(), 1u);
        assert_equal(strvec.fullsize(), 201u);

        // filling inplace buffer exactly stays in place
        strvec.clear();
        strvec.push_back(std::string(strvec.bufsize() - 1, 'a'));
        assert_equal(strvec.size(), 1u);
        assert_equal(strvec.fullsize(), strvec.bufsize());
        assert_equal(strvec.isinplace(), true);

        // one more than the size of the buffer
        strvec.clear();
        strvec.push_back(std::string(strvec.bufsize(), 'a'));
        assert_equal(strvec.size(), 1u);
        assert_equal(strvec.fullsize(), strvec.bufsize() + 1);
        assert_equal(strvec.isinplace(), false);

        // ssv stores arbitrary strings that can contain \0
        strvec.clear();
        std::string s(10, '\0');
        s += "meow";
        s += s;
        total = 0;
        for (auto i = 0u; i < strvec.maxstrings() * 2; i++) {
            strvec.push_back(s);
            total += s.size() + 1;
            assert_equal(strvec.size(), i + 1);
            assert_equal(strvec.fullsize(), total);
            assert_equal(strvec[rand() % (i + 1)], s);
            assert_equal(strvec.isinplace(),
                         total <= strvec.bufsize() && i + 1u <= strvec.maxstrings());
        }

        // multiple empty strings, including going to the heap
        strvec.clear();
        for (auto i = 0u; i < strvec.bufsize() * 2; i++) {
            strvec.push_back("");
            assert_equal(strvec.size(), i + 1);
            assert_equal(strvec.fullsize(), i + 1);
            assert_equal(strvec[rand() % (i + 1)], "");
        }

        // bunch of variable sized strings
        strvec = {};
        total = 0;
        std::vector<std::string> vec;
        for (char c = 'a'; c < 'z'; c++) {
            std::string s((rand() % 10) + 1, c);
            strvec.push_back(s);
            total += s.size() + 1;
            assert_equal(strvec.size(), c - 'a' + 1u);
            assert_equal(strvec.fullsize(), total);
            assert_equal(strvec.isinplace(),
                         total <= strvec.bufsize() && c - 'a' + 1u <= strvec.maxstrings());

            vec.push_back(s);
            auto r = rand() % vec.size();
            assert_equal(vec[r], strvec[r]);
        }

        // initializer list constructor
        strvec = {"foo", "bar", "baz"};
        assert_equal(strvec[1], "bar");
        assert_equal(ssv<16>{"a very long string that goes beyond 16 bytes"}.isonheap(), true);
        assert_equal(ssv<50>{"a very long string that goes beyond 16 bytes"}.isonheap(), false);

        // constructor with 2 iterators
        auto vecbybeginend = ssv(strvec.begin(), strvec.end());
        assert_equal(strvec.fullsize(), vecbybeginend.fullsize());

        // pop back
        strvec = {"meow", "moo", "woof"};
        strvec.pop_back();
        assert_equal(strvec.size(), 2u);
        while (strvec.isinplace())
            strvec.push_back("baaa");
        {
            auto size = strvec.size();
            strvec.pop_back();
            assert_equal(strvec.size(), size - 1);
        }

        // at
        auto at_throws = [&](auto idx) {
            try {
                strvec.at(idx);
                return false;
            } catch (std::out_of_range &) {
                return true;
            }
        };
        strvec.clear();
        assert_equal(at_throws(3), true);
        strvec = {"a", "b", "c", "d"};
        assert_equal(at_throws(3), false);
        strvec.push_back(std::string(1000, 'z'));
        assert_equal(at_throws(3), false);
        assert_equal(at_throws(4), false);
        assert_equal(at_throws(5), true);

        // front/back
        strvec = {"a", "b", "c", "d"};
        assert_equal(strvec.front(), "a");
        assert_equal(strvec.back(), "d");
        strvec.push_back(std::string(1000, 'z'));
        assert_equal(strvec.front(), "a");
        assert_equal(strvec.back().size(), 1000u);

        // resize down
        strvec = {"a", "b", "c", "d"};
        assert_equal(strvec.size(), 4u);
        strvec.resize(2);
        assert_equal(strvec.size(), 2u);
        while (strvec.isinplace())
            strvec.push_back("baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        strvec.push_back("meow");
        strvec.resize(strvec.size() - 1);
        assert_equal(strvec.isonheap(), true);
        strvec.resize(1);
        assert_equal(strvec.size(), 1u);
        assert_equal(strvec.isonheap(), false);

        // different parameters
        ssv<44, uint32_t> smol1;
        ssv<44, uint64_t> smol2;
        static_assert(smol1.maxstrings() < smol2.maxstrings());
        for (auto i = 0u; i < smol1.maxstrings(); i++)
            smol1.push_back("");
        assert_equal(smol1.isinplace(), true);
        smol1.push_back("");
        assert_equal(smol1.isinplace(), false);
        for (auto s : smol1)
            smol2.push_back(s);
        assert_equal(smol2.isinplace(), true);

        show();
        return *this;
    }

    void show() const {
        if (tty)
            std::cout << (failcount == 0 ? "\x1b[32m" : "\x1b[31m");

        if (current++ < total)
            std::cout << '[' << current << '/' << total << "] ";
        else
            std::cout << "[total] ";

        std::cout << passcount << " test" << (passcount == 1 ? "" : "s") << " passed, " //
                  << failcount << " test" << (failcount == 1 ? "" : "s") << " failed\n";
        if (tty)
            std::cout << "\x1b[m";
    }

    template <typename... t>
    static void run_tests() {
        total = sizeof...(t);
        (unit{}.template run<t>() + ...).show();
    };

    template <typename tuple>
    struct tuple_to_pack;

    template <typename... types>
    struct tuple_to_pack<std::tuple<types...>> {
        static void run() { run_tests<types...>(); }
    };
};

int main(int argc, char **argv) {
    options opt({
        {{"--verbose"sv, "-v"sv}, [] { verbose = true; }},
        {{"--quiet"sv, "-q"sv}, [] { verbose = false; }},
        {{"--errexit"sv, "-e"sv}, [] { errexit = true; }},
    });
    for (int i = 1; i < argc; i++)
        opt(argv[1]);

    tty = isatty(1);

    std::cout << "==== unit tests ====\n";

    using tuple = generate_ssvs<std::make_index_sequence<20>>::type;
    unit::tuple_to_pack<tuple>::run();
}
