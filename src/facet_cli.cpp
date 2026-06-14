#include "facet/facet.hpp"

#include <iostream>
#include <iterator>
#include <string>

namespace {

std::string option_value(const std::string& arg, const std::string& key) {
  std::string prefix = key + "=";
  if (arg.rfind(prefix, 0) == 0) {
    return arg.substr(prefix.size());
  }
  return "";
}

void usage() {
  std::cerr << "usage: facet read=<surface|strict|core|object|sympy-srepr> "
               "emit=<surface|strict|core|object|latex|sympy> < input\n";
}

} // namespace

int main(int argc, char** argv) {
  std::string read = "surface";
  std::string emit = "core";
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (auto value = option_value(arg, "read"); !value.empty()) {
      read = value;
    } else if (auto value = option_value(arg, "emit"); !value.empty()) {
      emit = value;
    } else {
      usage();
      return 2;
    }
  }

  std::string input((std::istreambuf_iterator<char>(std::cin)),
                    std::istreambuf_iterator<char>());

  try {
    facet::Arena arena;
    facet::Ref expr = nullptr;
    if (read == "surface") {
      expr = facet::read_surface(arena, input);
    } else if (read == "strict") {
      expr = facet::read_strict(arena, input);
    } else if (read == "core") {
      expr = facet::read_core(arena, input);
    } else if (read == "object") {
      expr = facet::read_object(arena, input);
    } else if (read == "sympy-srepr") {
      expr = facet::read_sympy_srepr(arena, input);
    } else {
      usage();
      return 2;
    }

    if (emit == "surface") {
      std::cout << facet::print_surface(expr) << "\n";
    } else if (emit == "strict") {
      std::cout << facet::print_strict(expr) << "\n";
    } else if (emit == "core") {
      std::cout << facet::print_core(expr) << "\n";
    } else if (emit == "object") {
      std::cout << facet::print_object(expr) << "\n";
    } else if (emit == "latex") {
      std::cout << facet::print_latex(expr) << "\n";
    } else if (emit == "sympy") {
      std::cout << facet::print_sympy(expr) << "\n";
    } else {
      usage();
      return 2;
    }
  } catch (const facet::Error& error) {
    std::cerr << "facet: " << error.what() << "\n";
    return 1;
  }
}
