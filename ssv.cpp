#include <iostream>
#include <vector>
#include <iomanip>
#include "ssv.hpp"

std::ostream &operator<<(std::ostream &stream, const ssv &s) {
    std::vector<std::string_view> vec(s.begin(), s.end());

    auto sep = "";
    stream << "[ ";
    for (auto str : vec) {
        stream << sep << std::quoted(str);
        sep = ", ";
    }
    stream << " ] size=" << s.size();
    stream << " inplace=" << s.inplace << " offsets=[ ";
    sep = "";
    for (auto i = 0; i < 9; i++) {
        stream << sep << (0x7f & (s.offsets >> (7 * i)));
        sep = ", ";
    }
    stream << " ] nelem_inplace=";

    auto desc = s.inplace_desc();
    return stream << int(desc.nfields);
}
