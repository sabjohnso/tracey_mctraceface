#include <tracey_mctraceface/shorten_symbol.hpp>

#include <string>
#include <string_view>

namespace tracey_mctraceface {

  namespace {

    auto
    preceded_by_operator(std::string_view s, std::size_t pos) -> bool {
      if (pos < 8) return false;
      return s.substr(pos - 8, 8) == "operator";
    }

    // Pass 1: Collapse template parameters.
    // vector<string> → vector<...>
    // Preserves operator< and operator<<.
    auto
    collapse_templates(std::string_view sym) -> std::string {
      std::string result;
      result.reserve(sym.size());
      int angle_depth = 0;

      for (std::size_t i = 0; i < sym.size(); ++i) {
        char c = sym[i];

        if (c == '<' && angle_depth == 0) {
          if (preceded_by_operator(result, result.size())) {
            // operator< or operator<<
            result += '<';
            if (i + 1 < sym.size() && sym[i + 1] == '<') {
              result += '<';
              ++i;
            }
          } else {
            // Template parameter — collapse to <...>
            result += "<...>";
            angle_depth = 1;
          }
        } else if (c == '<' && angle_depth > 0) {
          ++angle_depth;
        } else if (c == '>' && angle_depth > 0) {
          --angle_depth;
        } else if (angle_depth == 0) {
          result += c;
        }
      }

      return result;
    }

    // Pass 2: Strip the function parameter list.
    // foo(int, string) → foo
    // Preserves operator().
    auto
    strip_parameters(std::string_view sym) -> std::string {
      int depth = 0;
      std::size_t last_open = std::string_view::npos;

      for (std::size_t i = 0; i < sym.size(); ++i) {
        char c = sym[i];

        if (c == '{' || c == '[') {
          ++depth;
        } else if (c == '}' || c == ']') {
          --depth;
        } else if (c == '(' && depth == 0) {
          if (preceded_by_operator(sym, i)) {
            // operator() — skip the () pair, don't record
            if (i + 1 < sym.size() && sym[i + 1] == ')') ++i;
          } else {
            last_open = i;
          }
        }
      }

      if (last_open != std::string_view::npos) {
        return std::string(sym.substr(0, last_open));
      }
      return std::string(sym);
    }

  } // namespace

  auto
  shorten_symbol(std::string_view symbol) -> std::string {
    auto pass1 = collapse_templates(symbol);
    return strip_parameters(pass1);
  }

} // namespace tracey_mctraceface
