ssv
===

small string vector

a vector that stores strings compactly and in place, with dynamic heap
allocation when needed.  mostly suited for vectors of few short strings

the strings are immutable, and always null-terminated.  so it's something
between `std::vector<std::string>` and `std::vector<std::string_view>`

the api is similar to std::vector:

- element access
  - at
  - operator[]
  - front
  - back
- iterators
  - begin (const)
  - end (const)
- capacity
  - empty
  - size
- modifiers
  - clear
  - push_back
  - pop_back
  - resize (down only)

in addition we also have:

- fullsize (total space used by the strings)
