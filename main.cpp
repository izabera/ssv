#include "ssv.hpp"
#include <iostream>

int main() {
    ssv strvec;
    strvec.append("hello");
    strvec.append("world");
    std::cout << strvec << '\n';

    strvec = {};
    for (int i = 0; i < 200; i++) {
        auto str = std::to_string(i);
        strvec.append(str);
        std::cout << i << ' ' << strvec << '\n';
    }

    for (int i = 0; i < 200; i++) {
        auto str = std::to_string(i);
        strvec.append(str);
        std::cout << i << ' ' << strvec[i] << '\n';
    }

    strvec = {};
    strvec.append(std::string(200, 'a'));
    std::cout << strvec << '\n';
    std::cout << strvec[0] << '\n';

    strvec = {};
    strvec.append(std::string(100, 'b'));
    std::cout << strvec << '\n';
    strvec.append(std::string(100, 'b'));
    strvec = {};
    std::cout << strvec << '\n';

    strvec = {};
    for (int i = 0; i < 100; i++) {
        strvec.append("");
        std::cout << strvec << '\n';
    }

    strvec = {};
    for (char c = 'a'; c < 'z'; c++) {
        strvec.append({&c, 1});
        for (auto str : strvec) {
            std::cout << str << ",";
        }
        std::cout << '\n';
    }
}
