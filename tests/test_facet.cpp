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
      "(+ 1.5 2)",
      "\"literal\"",
      "(int (binder x (range 0 1)) (sin (* pi x)))",
      "(simplify (sqrt (^ x 2)) :assume (>= x 0))",
      "(setbuild (^ i 2) (binder i (range 1 n)) :when (> i 0))"};
  for (const auto& input : corpus) {
    Ref expr = read_core(arena, input);
    check(same_tree(expr, read_core(arena, print_core(expr))),
          "core round-trip " + input);
  }
}

void arena_interns_and_compares_exact_trees() {
  Arena arena;
  Ref one = read_core(arena, "(+ x 1)");
  Ref again = read_core(arena, "(+ x 1)");
  Ref different = read_core(arena, "(+ x 2)");
  check(one == again, "arena hash-conses identical trees");
  check(same_tree(one, again), "same_tree accepts identical structure");
  check(!same_tree(one, different), "same_tree rejects different structure");
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

  Ref real = read_strict(arena, "+(1.25, 2)");
  check_eq(print_core(real), "(+ 1.25 2)", "strict parses real atoms");
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

  Ref lambda = read_surface(arena, "x |-> x^2");
  check_eq(print_core(lambda), "(lam (binder x _) (^ x 2))",
           "surface lambda lowers to lam binder");
  check_eq(print_surface(lambda), "x |-> x ^ 2", "surface prints lambda");
  check_eq(print_latex(lambda), "x \\mapsto x^{2}", "latex prints lambda");

  Ref definition = read_surface(arena, "f := x |-> x^2");
  check_eq(print_core(definition), "(:= f (lam (binder x _) (^ x 2)))",
           "surface definition with lambda");

  Ref arrow = read_surface(arena, "R -> R");
  check_eq(print_core(arrow), "(-> R R)", "surface type arrow");

  Ref subscript = read_surface(arena, "a_k + T_mu");
  check_eq(print_core(subscript),
           "(+ (idx a (down k)) (idx T (down mu)))",
           "surface subscript lowers to idx/down");
  check_eq(print_latex(subscript), "a_{k} + T_{mu}",
           "latex prints subscripts");

  Ref meta = read_surface(arena, "?xs...? + ?x");
  check_eq(print_core(meta),
           "(+ (meta xs :kind seq?) (meta x :kind one))",
           "surface meta variables carry explicit kind");
  check_eq(print_surface(meta), "?xs...? + ?x", "surface prints meta vars");
}

void object_round_trips() {
  Arena arena;
  Ref expr = read_core(arena, "(int (binder x (range 0 1)) (sin (* pi x)))");
  std::string object = print_object(expr);
  check(same_tree(expr, read_object(arena, object)), "object JSON round-trip");
  check_eq(object,
           "{\"head\":\"int\",\"args\":[{\"head\":\"binder\",\"args\":[{"
           "\"atom\":\"sym\",\"value\":\"x\"},{\"head\":\"range\",\"args\":[{"
           "\"atom\":\"int\",\"value\":\"0\"},{\"atom\":\"int\",\"value\":\"1\"}"
           "]}]},{\"head\":\"sin\",\"args\":[{\"head\":\"*\",\"args\":[{"
           "\"atom\":\"sym\",\"value\":\"pi\"},{\"atom\":\"sym\",\"value\":\"x\"}"
           "]}]}]}",
           "object canonical typed JSON");

  Ref str = arena.string("x");
  Ref sym = arena.sym("x");
  check(!same_tree(str, sym), "string atom is distinct from symbol atom");
  check(same_tree(str, read_object(arena, print_object(str))),
        "object string atom round-trip");
  Ref rat = arena.rational("1", "2");
  check(same_tree(rat, read_object(arena, print_object(rat))),
        "object rational atom round-trip");
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
           "(meta xs :kind seq)", "variadic token becomes explicit meta");
  check_eq(print_core(read_surface(arena, "?xs...?")),
           "(meta xs :kind seq?)", "optional variadic token is longest-match");
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
  arena_interns_and_compares_exact_trees();
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
