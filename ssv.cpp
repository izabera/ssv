#include "ssv.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <ranges>
#include <sstream>
#include <unistd.h>
#include <utility>
#include <vector>

// glibc's rand() was actually showing up in the profile...
// we don't need fancy randomness for the perf tests here
uint64_t seed;
inline void my_srand(unsigned s) {
    seed = s - 1;
}
inline int my_rand(void) {
    seed = 6364136223846793005ULL * seed + 1;
    return seed >> 33;
}

template <typename T>
constexpr auto type_name() {
    auto name = std::string_view{__PRETTY_FUNCTION__};
    auto start = name.find("T = ") + 4;
    auto end = name.rfind("]");
    return name.substr(start, end - start);
}

bool tty;
bool verbose;
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
        auto msg = tty ? //
                       ok ? "[\x1b[32m OK \x1b[m]:" : "[\x1b[31mFAIL\x1b[m]:"
                       : //
                       ok ? "[ OK ]:" : "[FAIL]:";
        auto q = [](const auto &x) {
            if constexpr (requires { std::quoted(x); })
                return std::quoted(x);
            else
                return x;
        };

        std::stringstream ss;
        ss << std::boolalpha;
        if (ok) {
            ss << msg << line << ": " << BLUE(exprstr) << " == " << GREEN(q(val)) << '\n';
        }
        else {
            ss << msg << line << ": " << BLUE(exprstr) << " == " << RED(q(val));
            ss << " [expected " << GREEN(wantstr) << " (" << YELLOW(q(want)) << ")]\n";
        }

        if (!ok) {
            std::cerr << ss.str();
            failcount++;
            // exit(1);
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
        strvec.resize(2);
        assert_equal(strvec.size(), 2u);
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

        std::cout << passcount << " test" << (passcount == 1 ? "" : "s") << " passed, " //
                  << failcount << " test" << (failcount == 1 ? "" : "s") << " failed\n";
        if (tty)
            std::cout << "\x1b[m";
    }

    template <typename... t>
    static void run_tests() {
        total = sizeof...(t);
        std::cout << "==== unit tests ====\n";
        (unit{}.template run<t>() + ...).show();
    };
};

int main(int argc, char **argv) {
    verbose = argc < 2 ? true : std::string{argv[1]} == "quiet" ? false : true;
    tty = isatty(1);

    unit::run_tests<ssv<40>, ssv<40, uint16_t>, ssv<40, uint32_t>, //
                    ssv<44>, ssv<44, uint16_t>, ssv<44, uint32_t>, //
                    ssv<48>, ssv<48, uint16_t>, ssv<48, uint32_t>, //
                    ssv<52>, ssv<52, uint16_t>, ssv<52, uint32_t>, //
                    ssv<56>, ssv<56, uint16_t>, ssv<56, uint32_t>, //
                    ssv<60>, ssv<60, uint16_t>, ssv<60, uint32_t>>();

    std::cout << "==== perf tests ====\n";

    constexpr static auto maxiter = 1'000'000;
    auto time = [&]<typename vectype>(auto lambda, auto... args) {
        my_srand(1234);
        const auto start{std::chrono::steady_clock::now()};
        for (auto q = 0; q < maxiter; q++) {
            lambda.template operator()<vectype>(args...);
        }
        const auto end{std::chrono::steady_clock::now()};
        return std::chrono::duration<double, std::milli>{end - start};
    };

    struct test_description {
        std::string_view name;
        std::vector<std::string_view> args = {};
    };

    // cartesian product of both types and values
    auto cartesian = [&]<typename... t>(test_description description, auto lambda, auto... args) {
        std::cout << "--- " << description.name << '\n';
        std::cout << "type";
        for (const auto &name : description.args)
            std::cout << '\t' << name;
        std::cout << "\ttime\n";

        auto tostringall = [&](auto &&...arg) {
            auto tostring = [](auto arg) -> std::string {
                if constexpr (requires { std::to_string(arg); })
                    return std::to_string(arg);
                std::stringstream ss;
                if constexpr (requires { ss << arg; }) {
                    ss << arg;
                    return ss.str();
                }
                return "<unprintable>";
            };
            std::stringstream ss;
            [&](auto &&...) {}((ss << tostring(arg) << '\t')...);
            return ss.str();
        };

        for (auto tuple : std::ranges::cartesian_product_view(args...)) {
            [](auto &&...) {}((std::cout << type_name<t>() << '\t' << std::apply(tostringall, tuple)
                                         << time.template operator()<t>(lambda, tuple) << '\n')...);
        }
    };

    constexpr static char qqqq[] = "qqqqqqqqq";
    cartesian.template operator()<
        // compare perf with these types
        ssv<>, ssv<40>, std::vector<std::string> //
        >(
        {"push back test", {"reserve", "limit"}},

        // the actual perf test
        []<typename vectype>(auto tuple) {
            vectype myvec;
            auto [reserve, limit] = tuple;
            if (reserve > 0)
                myvec.reserve(reserve);
            for (auto i = 0; i < limit; i++) {
                auto r = my_rand() % sizeof qqqq;
                myvec.push_back({qqqq + r, sizeof qqqq - r});
            }
        },

        // test parameters
        std::vector{0, 3, 5, 9}, std::vector{4, 5, 6, 9, 12, 15, 18});
}
