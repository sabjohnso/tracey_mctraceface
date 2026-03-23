#include <tracey_mctraceface/shorten_symbol.hpp>

#include <catch2/catch_test_macros.hpp>

using tracey_mctraceface::shorten_symbol;

// ===========================================================================
// Template collapsing
// ===========================================================================

TEST_CASE("collapse simple template", "[shorten_symbol]") {
  CHECK(shorten_symbol("std::vector<int>") == "std::vector<...>");
}

TEST_CASE("collapse nested templates", "[shorten_symbol]") {
  CHECK(
    shorten_symbol("std::map<std::string, std::vector<int>>") ==
    "std::map<...>");
}

TEST_CASE("collapse template with method", "[shorten_symbol]") {
  CHECK(
    shorten_symbol("std::vector<int>::iterator") ==
    "std::vector<...>::iterator");
}

TEST_CASE("no template — unchanged", "[shorten_symbol]") {
  CHECK(shorten_symbol("main") == "main");
}

// ===========================================================================
// Parameter stripping
// ===========================================================================

TEST_CASE("strip simple parameters", "[shorten_symbol]") {
  CHECK(shorten_symbol("foo(int, string)") == "foo");
}

TEST_CASE("strip empty parameters", "[shorten_symbol]") {
  CHECK(shorten_symbol("foo()") == "foo");
}

TEST_CASE("strip const parameters", "[shorten_symbol]") {
  CHECK(shorten_symbol("foo() const") == "foo");
}

TEST_CASE("strip with template and params", "[shorten_symbol]") {
  CHECK(
    shorten_symbol("std::vector<int>::push_back(const int&)") ==
    "std::vector<...>::push_back");
}

// ===========================================================================
// Operator preservation
// ===========================================================================

TEST_CASE("preserve operator<", "[shorten_symbol]") {
  CHECK(shorten_symbol("operator<") == "operator<");
}

TEST_CASE("preserve operator<<", "[shorten_symbol]") {
  CHECK(shorten_symbol("operator<<") == "operator<<");
}

TEST_CASE("preserve operator()", "[shorten_symbol]") {
  CHECK(shorten_symbol("operator()") == "operator()");
}

TEST_CASE("preserve operator() with params", "[shorten_symbol]") {
  CHECK(shorten_symbol("operator()(int, int)") == "operator()");
}

TEST_CASE("preserve operator() in templated class", "[shorten_symbol]") {
  CHECK(
    shorten_symbol("std::less<int>::operator()(const int&, const int&)") ==
    "std::less<...>::operator()");
}

// ===========================================================================
// Combined edge cases
// ===========================================================================

TEST_CASE("complex symbol shortening", "[shorten_symbol]") {
  CHECK(
    shorten_symbol(
      "std::__1::basic_string<char, std::__1::char_traits<char>, "
      "std::__1::allocator<char>>::append(char const*, unsigned long)") ==
    "std::__1::basic_string<...>::append");
}
