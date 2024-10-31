#include "ssv.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unistd.h>
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

int main(int argc, char **argv) {
    const bool verbose = argc < 2 ? true : std::string{argv[1]} == "quiet" ? false : true;
    const bool tty = isatty(1);

    size_t passcount{}, failcount{};

    auto assert_equal = [&](const auto &str, const auto line, const auto &val, const auto &want) {
        auto ok = val == want;
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
        ss << msg << line << ": " << str << " == " << q(val);
        if (!ok)
            ss << " (want " << q(want) << ")";
        ss << '\n';

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
    };
#define assert_equal(expr, want) assert_equal(#expr, __LINE__, expr, want)

    //
    // unit tests
    //

    std::cout << "==== unit tests ====\n";

    // basic
    ssv strvec;
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
    ssv strvec2;
    strvec2.push_back("meow");
    strvec2.push_back(std::string(300, 'q'));
    strvec = strvec2;
    assert_equal(strvec.fullsize(), strvec2.fullsize());
    assert_equal(strvec.fullsize(), 306u);
    assert_equal(strvec.size(), strvec2.size());

    // copy constructor
    ssv strvec3(strvec2);
    assert_equal(strvec3.fullsize(), strvec2.fullsize());
    assert_equal(strvec3.fullsize(), 306u);
    assert_equal(strvec3.size(), strvec2.size());

    // move constructor after going to the heap
    strvec3.push_back(std::string(strvec3.bufsize(), 'q'));
    ssv strvec4(std::move(strvec3));
    assert_equal(strvec4.fullsize(), strvec2.fullsize() + strvec3.bufsize() + 1);
    assert_equal(strvec4.size(), strvec2.size() + 1);

    // move assign
    ssv strvec5 = std::move(strvec4);
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
    assert_equal(strvec.size(), strvec.maxstrings() + 1);
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

    if (tty)
        std::cout << (failcount == 0 ? "\x1b[32m" : "\x1b[31m");
    std::cout << passcount << " test" << (passcount == 1 ? "" : "s") << " passed, " //
              << failcount << " test" << (failcount == 1 ? "" : "s") << " failed\n";
    if (tty)
        std::cout << "\x1b[m";

    //
    // perf tests
    //

    std::cout << "==== perf tests ====\n";

    auto time = [&](auto lambda) {
        my_srand(1234);
        const auto start{std::chrono::steady_clock::now()};
        lambda();
        const auto end{std::chrono::steady_clock::now()};
        return std::chrono::duration<double, std::milli>{end - start};
    };

    constexpr static char qqqq[] = "qqqqqqqqq";
    constexpr static auto maxiter = 1'000'000;
    {
        auto myvectime = time([] {
            for (auto q = 0; q < maxiter; q++) {
                ssv myvec;
                auto max = (my_rand() % ssv<>::Maxstrings) * 2;
                myvec.reserve(max);
                for (auto i = 0u; i < max; i++) {
                    auto r = my_rand() % sizeof qqqq;
                    myvec.push_back({qqqq + r, sizeof qqqq - r});
                }
            }
        });
        std::cout << "ssv took " << myvectime << '\n';

        auto stdvectime = time([] {
            for (auto q = 0; q < maxiter; q++) {
                std::vector<std::string> myvec;
                auto max = (my_rand() % ssv<>::Maxstrings) * 2;
                myvec.reserve(max);
                for (auto i = 0u; i < max; i++) {
                    auto r = my_rand() % sizeof qqqq;
                    myvec.push_back({qqqq + r, sizeof qqqq - r});
                }
            }
        });
        std::cout << "std::vector<std::string> took " << stdvectime << '\n';
    }

    {
        auto myvectime = time([] {
            for (auto q = 0; q < maxiter; q++) {
                ssv myvec;
                myvec.reserve(ssv<>::Maxstrings);
                for (auto i = 0u; i < ssv<>::Maxstrings; i++) {
                    auto r = my_rand() % sizeof qqqq;
                    myvec.push_back({qqqq + r, sizeof qqqq - r});
                }
            }
        });
        std::cout << "ssv took " << myvectime << '\n';

        auto stdvectime = time([] {
            for (auto q = 0; q < maxiter; q++) {
                std::vector<std::string> myvec;
                myvec.reserve(ssv<>::Maxstrings);
                for (auto i = 0u; i < ssv<>::Maxstrings; i++) {
                    auto r = my_rand() % sizeof qqqq;
                    myvec.push_back({qqqq + r, sizeof qqqq - r});
                }
            }
        });
        std::cout << "std::vector<std::string> took " << stdvectime << '\n';
    }
}
