#include "ssv.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

int main(int argc, char **argv) {
    const bool verbose = argc < 2 ? true : std::string{argv[1]} == "quiet" ? false : true;

    size_t passcount{}, failcount{};
    auto assert_equal = [&](const auto &str, const auto line, const auto &val, const auto &want) {
        auto ok = val == want;
        auto msg = ok ? "[\x1b[32m OK \x1b[m]:" : "[\x1b[31mFAIL\x1b[m]:";
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

    // basic
    ssv strvec;
    assert_equal(strvec.nstrings(), 0u);
    assert_equal(strvec.size(), 0u);

    strvec.append("hello");
    strvec.append("world");
    assert_equal(strvec[0], "hello");
    assert_equal(strvec[1], "world");

    // is empty after clearing
    strvec.clear();
    assert_equal(strvec.empty(), true);
    strvec.append("meow");
    strvec = {};
    assert_equal(strvec.empty(), true);

    // can resize to heap
    strvec.clear();
    size_t total = 0;
    for (size_t i = 0; i < 200; i++) {
        auto str = std::to_string(i);
        assert_equal(strvec.nstrings(), i);
        strvec.append(str);
        total += str.size() + 1;
        assert_equal(strvec.nstrings(), i + 1);
        assert_equal(strvec.size(), total);
    }

    // copy assign
    ssv strvec2;
    strvec2.append("meow");
    strvec2.append(std::string(300, 'q'));
    strvec = strvec2;
    assert_equal(strvec.size(), strvec2.size());
    assert_equal(strvec.size(), 306u);
    assert_equal(strvec.nstrings(), strvec2.nstrings());

    // copy constructor
    ssv strvec3(strvec2);
    assert_equal(strvec3.size(), strvec2.size());
    assert_equal(strvec3.size(), 306u);
    assert_equal(strvec3.nstrings(), strvec2.nstrings());

    // move constructor after going to the heap
    strvec3.append(std::string(strvec3.bufsize(), 'q'));
    ssv strvec4(std::move(strvec3));
    assert_equal(strvec4.size(), strvec2.size() + strvec3.bufsize() + 1);
    assert_equal(strvec4.nstrings(), strvec2.nstrings() + 1);

    // move assign
    ssv strvec5 = std::move(strvec4);
    assert_equal(strvec5.nstrings(), strvec2.nstrings() + 1);

    // can immediately go to heap
    strvec.clear();
    strvec.append(std::string(200, 'a'));
    assert_equal(strvec.nstrings(), 1u);
    assert_equal(strvec.size(), 201u);

    // filling inplace buffer exactly stays in place
    strvec.clear();
    strvec.append(std::string(strvec.bufsize() - 1, 'a'));
    assert_equal(strvec.nstrings(), 1u);
    assert_equal(strvec.size(), strvec.bufsize());
    assert_equal(strvec.isinplace(), true);

    // one more than the size of the buffer
    strvec.clear();
    strvec.append(std::string(strvec.bufsize(), 'a'));
    assert_equal(strvec.nstrings(), 1u);
    assert_equal(strvec.size(), strvec.bufsize() + 1);
    assert_equal(strvec.isinplace(), false);

    // ssv stores arbitrary strings that can contain \0
    strvec.clear();
    std::string s(10, '\0');
    s += "meow";
    s += s;
    total = 0;
    for (auto i = 0u; i < strvec.maxstrings() * 2; i++) {
        strvec.append(s);
        total += s.size() + 1;
        assert_equal(strvec.nstrings(), i + 1);
        assert_equal(strvec.size(), total);
        assert_equal(strvec[rand() % (i + 1)], s);
        assert_equal(strvec.isinplace(),
                     total <= strvec.bufsize() && i + 1u <= strvec.maxstrings());
    }

    // multiple empty strings, including going to the heap
    strvec.clear();
    for (auto i = 0u; i < strvec.bufsize() * 2; i++) {
        strvec.append("");
        assert_equal(strvec.nstrings(), i + 1);
        assert_equal(strvec.size(), i + 1);
        assert_equal(strvec[rand() % (i + 1)], "");
    }

    // bunch of variable sized strings
    strvec = {};
    total = 0;
    std::vector<std::string> vec;
    for (char c = 'a'; c < 'z'; c++) {
        std::string s((rand() % 10) + 1, c);
        strvec.append(s);
        total += s.size() + 1;
        assert_equal(strvec.nstrings(), c - 'a' + 1u);
        assert_equal(strvec.size(), total);
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
        smol1.append("");
    assert_equal(smol1.isinplace(), true);
    smol1.append("");
    assert_equal(smol1.isinplace(), false);
    for (auto s : smol1)
        smol2.append(s);
    assert_equal(smol2.isinplace(), true);

    // initializer list constructor
    strvec = {"foo", "bar", "baz"};
    assert_equal(strvec[1], "bar");
    assert_equal(ssv<16>{"a very long string that goes beyond 16 bytes"}.isonheap(), true);
    assert_equal(ssv<50>{"a very long string that goes beyond 16 bytes"}.isonheap(), false);

    // constructor with 2 iterators
    auto vecbybeginend = ssv(strvec.begin(), strvec.end());
    assert_equal(strvec.size(), vecbybeginend.size());

    // perf test
    auto time = [](auto lambda) {
        const auto start{std::chrono::steady_clock::now()};
        lambda();
        const auto end{std::chrono::steady_clock::now()};
        return std::chrono::duration<double, std::milli>{end - start};
    };

    auto myvectime = time([] {
        srand(1234);
        for (auto q = 0; q < 1000000; q++) {
            ssv myvec;
            for (auto i = 0u; i < ssv<>::Maxstrings; i++)
                myvec.append(std::string(rand() % 10, 'q'));
        }
    });
    std::cout << "ssv took " << myvectime << '\n';

    auto stdvectime = time([] {
        srand(1234);
        for (auto q = 0; q < 1000000; q++) {
            std::vector<std::string> myvec;
            for (auto i = 0u; i < ssv<>::Maxstrings; i++)
                myvec.push_back(std::string(rand() % 10, 'q'));
        }
    });
    std::cout << "std::vector<std::string> took " << stdvectime << '\n';

    std::cout << "==== test result ====\n";
    std::cout << passcount << " tests pass, " << failcount << " tests fail\n";
}
