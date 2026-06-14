#include "facet/facet.hpp"

#include <iostream>
#include <iterator>
#include <sstream>
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
               "emit=<surface|strict|core|object|latex|render:svg|coverage:K|source:sympy|source:sympy-srepr|source:sympy-core|sympy|sympy-srepr|sympy-core> "
               "[compare=EXPR] [by=<structural|simplify|numeric|numeric(samples=N,tol=E)>] < input\n";
}

std::string format_coverage(const facet::Coverage& coverage) {
  std::ostringstream out;
  out << "coverage: " << coverage.supported << "/" << coverage.total
      << " supported[kernel=" << coverage.kernel << "]";
  for (const auto& missing : coverage.missing) {
    out << "\nunmapped: " << missing.path << " " << missing.head
        << " kernel=" << missing.kernel;
  }
  return out.str();
}

std::string format_compare(const facet::CompareResult& result) {
  std::ostringstream out;
  out << "agreement: " << result.status << "[by=" << result.by
      << ", strength=" << result.strength << ", detail=" << result.detail;
  if (result.by == "numeric") {
    out << ", tol=" << result.tol << ", samples=" << result.samples;
  }
  if (!result.witness.empty()) {
    out << ", witness=" << result.witness;
  }
  out << "]";
  return out.str();
}

} // namespace

int main(int argc, char** argv) {
  std::string read = "surface";
  std::string emit = "core";
  std::string compare_expr;
  std::string compare_by = "structural";
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (auto value = option_value(arg, "read"); !value.empty()) {
      read = value;
    } else if (auto value = option_value(arg, "emit"); !value.empty()) {
      emit = value;
    } else if (auto value = option_value(arg, "compare"); !value.empty()) {
      compare_expr = value;
    } else if (auto value = option_value(arg, "by"); !value.empty()) {
      compare_by = value;
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
    } else if (read == "sympy-srepr" || read == "source:sympy-srepr") {
      expr = facet::read_sympy_srepr(arena, input);
    } else {
      usage();
      return 2;
    }

    if (read == "surface") {
      for (const auto& diagnostic : facet::validate(expr)) {
        std::cerr << "facet warning [" << diagnostic.code
                  << "]: " << diagnostic.message << "\n";
      }
    }

    if (!compare_expr.empty()) {
      facet::Ref rhs = nullptr;
      if (read == "surface") {
        rhs = facet::read_surface(arena, compare_expr);
      } else if (read == "strict") {
        rhs = facet::read_strict(arena, compare_expr);
      } else if (read == "core") {
        rhs = facet::read_core(arena, compare_expr);
      } else if (read == "object") {
        rhs = facet::read_object(arena, compare_expr);
      } else {
        throw facet::Error("compare is not available for read mode: " + read);
      }
      std::cout << format_compare(facet::compare(arena, expr, rhs, compare_by))
                << "\n";
      return 0;
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
    } else if (emit == "render:svg") {
      std::cout << facet::render_svg(expr) << "\n";
    } else if (emit.rfind("coverage:", 0) == 0) {
      std::cout << format_coverage(facet::coverage(expr, emit.substr(9)))
                << "\n";
    } else if (emit == "sympy" || emit == "source:sympy") {
      std::cout << facet::print_sympy(expr) << "\n";
    } else if (emit == "sympy-srepr" || emit == "source:sympy-srepr") {
      std::cout << facet::print_sympy_srepr(expr) << "\n";
    } else if (emit == "sympy-core" || emit == "source:sympy-core") {
      std::cout << facet::print_core(facet::evaluate_sympy(arena, expr)) << "\n";
    } else {
      usage();
      return 2;
    }
  } catch (const facet::Error& error) {
    std::cerr << "facet: " << error.what() << "\n";
    return 1;
  }
}
