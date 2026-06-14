#include "facet/facet.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace facet;

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << name << "\n";
  }
}

void check_eq(const std::string& got, const std::string& want,
              const std::string& name) {
  if (got != want) {
    ++failures;
    std::cerr << "FAIL: " << name << "\n  got : " << got
              << "\n  want: " << want << "\n";
  }
}

void check_throws(void (*fn)(), const std::string& name) {
  try {
    fn();
    ++failures;
    std::cerr << "FAIL: " << name << "\n  expected exception\n";
  } catch (const Error&) {
  }
}

void core_round_trips() {
  Arena arena;
  std::vector<std::string> corpus = {
      "(+ (^ x 2) y)",
      "(int (binder x (range 0 1)) (sin (* pi x)))",
      "(simplify (sqrt (^ x 2)) :assume (>= x 0))",
      "(setbuild (^ i 2) (binder i (range 1 n)) :when (> i 0))"};
  for (const auto& input : corpus) {
    Ref expr = read_core(arena, input);
    check(same_tree(expr, read_core(arena, print_core(expr))),
          "core round-trip " + input);
  }
}

void strict_round_trips() {
  Arena arena;
  std::vector<std::string> corpus = {
      "+(^(x, 2), y)",
      "int(binder(x, range(0, 1)), sin(*(pi, x)))",
      "simplify(sqrt(^(x, 2)), assume=>=(x, 0))"};
  (void)corpus;

  Ref expr = read_strict(arena, "int(binder(x, range(0, 1)), sin(*(pi, x)))");
  check_eq(print_core(expr), "(int (binder x (range 0 1)) (sin (* pi x)))",
           "strict parses prefix calls");
  check(same_tree(expr, read_strict(arena, print_strict(expr))),
        "strict print/read fixpoint");
}

void surface_examples() {
  Arena arena;
  Ref integral = read_surface(arena, "int[x : 0..1](sin(pi*x))");
  check_eq(print_core(integral),
           "(int (binder x (range 0 1)) (sin (* pi x)))",
           "surface binder integral");
  check(same_tree(integral, read_surface(arena, print_surface(integral))),
        "surface integral fixpoint");
  check_eq(print_latex(integral), "\\int_{0}^{1} \\sin(\\pi x)\\,dx",
           "latex integral");

  Ref limit = read_surface(arena, "lim[x -> 0](sin(x)/x)");
  check_eq(print_core(limit),
           "(lim (binder x (approach 0)) (/ (sin x) x))",
           "surface limit approach binder");

  Ref setbuild = read_surface(arena, "{ i^2 | i : 1..n, i > 0 }");
  check_eq(print_core(setbuild),
           "(setbuild (^ i 2) (binder i (range 1 n)) :when (> i 0))",
           "surface set-builder");

  Ref subst = read_surface(arena, "(x^2 + y) @ subst{x = 2}");
  check_eq(print_core(subst), "(subst (+ (^ x 2) y) (= x 2))",
           "surface subst context");

  Ref assume = read_surface(arena, "simplify(sqrt(x^2)) @ assume(x >= 0)");
  check_eq(print_core(assume),
           "(simplify (sqrt (^ x 2)) :assume (>= x 0))",
           "surface assume context attribute");
}

void object_round_trips() {
  Arena arena;
  Ref expr = read_core(arena, "(int (binder x (range 0 1)) (sin (* pi x)))");
  std::string object = print_object(expr);
  check(same_tree(expr, read_object(arena, object)), "object JSON round-trip");
  check_eq(object,
           "{\"head\":\"int\",\"args\":[{\"head\":\"binder\",\"args\":[\"x\",{"
           "\"head\":\"range\",\"args\":[0,1]}]},{\"head\":\"sin\",\"args\":[{"
           "\"head\":\"*\",\"args\":[\"pi\",\"x\"]}]}]}",
           "object canonical compact JSON");
}

void cross_mode_agreement() {
  Arena arena;
  Ref surface = read_surface(arena, "int[x : 0..1](sin(pi*x))");
  Ref strict = read_strict(arena, "int(binder(x, range(0, 1)), sin(*(pi, x)))");
  Ref core = read_core(arena, "(int (binder x (range 0 1)) (sin (* pi x)))");
  check(same_tree(surface, strict), "surface equals strict");
  check(same_tree(strict, core), "strict equals core");
}

void adversarial_grammar() {
  Arena arena;
  check_eq(print_core(read_surface(arena, "1..n")),
           "(range 1 n)", "range token beats dots");
  check_eq(print_core(read_surface(arena, "?xs...")),
           "?xs...", "variadic token survives as meta symbol for now");
  check_throws(
      []() {
        Arena a;
        (void)read_surface(a, "sum[x=2](x)");
      },
      "binder rejects equals");
  check_eq(print_core(read_surface(arena, "{ x | x : 1..n }")),
           "(setbuild x (binder x (range 1 n)))",
           "set-builder bar is not pipeline");
  check_eq(print_core(read_surface(arena, "a |> b")),
           "(|> a b)", "pipeline token parsed separately");
}

} // namespace

int main() {
  core_round_trips();
  strict_round_trips();
  surface_examples();
  object_round_trips();
  cross_mode_agreement();
  adversarial_grammar();

  if (failures) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "All Facet Gen 1 tests passed\n";
  return 0;
}
