#include "opts.hpp"
#include "ssv.hpp"
#include <chrono>
#include <iostream>
#include <ranges>
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

template <typename T>
constexpr auto type_name() {
    auto name = std::string_view{__PRETTY_FUNCTION__};
    auto start = name.find("T = ") + 4;
    auto end = name.rfind("]");
    return name.substr(start, end - start);
}

int main(int argc, char **argv) {
    options opt({});
    for (int i = 1; i < argc; i++)
        opt(argv[1]);

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
        ssv<>, ssv<40>, ssv<44, uint32_t>, std::vector<std::string> //
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
