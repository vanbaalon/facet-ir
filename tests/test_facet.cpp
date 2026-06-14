#include "facet/facet.hpp"
#include "../src/facet_internal.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace facet;

namespace {

int failures = 0;

struct CorpusCase {
  std::string name;
  std::map<std::string, std::string> fields;
};

std::string layout_text(const std::string& input) {
  std::vector<std::string> parts;
  for (const auto& tok : facet::internal::lex_layout_for_test(input)) {
    parts.push_back(tok.text);
  }
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) {
      out += " ";
    }
    out += parts[i];
  }
  return out;
}

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

void check_throws_contains(void (*fn)(), const std::string& needle,
                           const std::string& name) {
  try {
    fn();
    ++failures;
    std::cerr << "FAIL: " << name << "\n  expected exception\n";
  } catch (const Error& error) {
    std::string message = error.what();
    if (message.find(needle) == std::string::npos) {
      ++failures;
      std::cerr << "FAIL: " << name << "\n  got : " << message
                << "\n  want substring: " << needle << "\n";
    }
  }
}

std::string trim(const std::string& input) {
  std::size_t first = input.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  std::size_t last = input.find_last_not_of(" \t\r\n");
  return input.substr(first, last - first + 1);
}

std::vector<CorpusCase> read_corpus(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw Error("could not open corpus: " + path);
  }
  std::vector<CorpusCase> cases;
  CorpusCase current;
  std::string line;
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty() || line.rfind("#", 0) == 0) {
      continue;
    }
    if (line == "---") {
      if (!current.name.empty()) {
        cases.push_back(current);
      }
      current = CorpusCase{};
      continue;
    }
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
      throw Error("bad corpus line: " + line);
    }
    std::string key = trim(line.substr(0, colon));
    std::string value = trim(line.substr(colon + 1));
    if (key == "case") {
      current.name = value;
    } else {
      current.fields[key] = value;
    }
  }
  if (!current.name.empty()) {
    cases.push_back(current);
  }
  return cases;
}

void corpus_round_trips_for(const std::string& path) {
  std::vector<CorpusCase> cases = read_corpus(path);
  check(!cases.empty(), "corpus has cases");
  for (const auto& item : cases) {
    Arena arena;
    Ref expected = nullptr;
    if (auto found = item.fields.find("core"); found != item.fields.end()) {
      expected = read_core(arena, found->second);
      check(same_tree(expected, read_core(arena, print_core(expected))),
            "corpus core fixpoint " + item.name);
    }

    if (auto found = item.fields.find("surface"); found != item.fields.end()) {
      Ref surface = read_surface(arena, found->second);
      if (expected) {
        check(same_tree(surface, expected),
              "corpus surface agrees " + item.name);
      }
      check(same_tree(surface, read_surface(arena, print_surface(surface))),
            "corpus surface fixpoint " + item.name);
    }

    if (auto found = item.fields.find("strict"); found != item.fields.end()) {
      Ref strict = read_strict(arena, found->second);
      if (expected) {
        check(same_tree(strict, expected),
              "corpus strict agrees " + item.name);
      }
      check(same_tree(strict, read_strict(arena, print_strict(strict))),
            "corpus strict fixpoint " + item.name);
    }

    if (auto found = item.fields.find("latex"); found != item.fields.end()) {
      check(expected != nullptr, "corpus latex has core " + item.name);
      if (expected) {
        check_eq(print_latex(expected), found->second,
                 "corpus latex " + item.name);
      }
    }

    if (auto found = item.fields.find("sympy"); found != item.fields.end()) {
      check(expected != nullptr, "corpus sympy has core " + item.name);
      if (expected) {
        check_eq(print_sympy(expected), found->second,
                 "corpus sympy " + item.name);
      }
    }

    if (auto found = item.fields.find("sympy_srepr");
        found != item.fields.end()) {
      check(expected != nullptr, "corpus sympy_srepr has core " + item.name);
      if (expected) {
        check(same_tree(read_sympy_srepr(arena, found->second), expected),
              "corpus sympy_srepr agrees " + item.name);
      }
    }

    if (auto found = item.fields.find("object"); found != item.fields.end()) {
      check(expected != nullptr, "corpus object has core " + item.name);
      if (expected) {
        check_eq(print_object(expected), found->second,
                 "corpus object " + item.name);
        check(same_tree(read_object(arena, found->second), expected),
              "corpus object agrees " + item.name);
      }
    }
  }
}

void corpus_round_trips() {
  corpus_round_trips_for("tests/corpus/gen1.txt");
  corpus_round_trips_for("tests/corpus/v2-m1.txt");
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
  for (const auto& input : corpus) {
    Ref expr = read_strict(arena, input);
    check(same_tree(expr, read_strict(arena, print_strict(expr))),
          "strict round-trip " + input);
  }

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
  check_eq(print_latex(integral),
           "\\int_{0}^{1} \\sin\\left(\\pi x\\right)\\,dx",
           "latex integral");

  Ref limit = read_surface(arena, "lim[x -> 0](sin(x)/x)");
  check_eq(print_core(limit),
           "(lim (binder x (approach 0)) (/ (sin x) x))",
           "surface limit approach binder");

  Ref setbuild = read_surface(arena, "{ i^2 | i : 1..n, i > 0 }");
  check_eq(print_core(setbuild),
           "(setbuild (^ i 2) (binder i (range 1 n)) :when (> i 0))",
           "surface set-builder");
  check_eq(print_surface(setbuild), "set{ i ^ 2 | i : 1..n, i > 0 }",
           "surface set-builder prints named set head");

  Ref named_setbuild = read_surface(arena, "set{ i^2 | i : 1..n, i > 0 }");
  check(same_tree(named_setbuild, setbuild),
        "surface named set-builder agrees with bare compatibility form");

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
  check_eq(print_latex(subscript), "a_{k} + T_{\\mu}",
           "latex prints subscripts");

  Ref meta = read_surface(arena, "?xs...? + ?x");
  check_eq(print_core(meta),
           "(+ (meta xs :kind seq?) (meta x :kind one))",
           "surface meta variables carry explicit kind");
  check_eq(print_surface(meta), "?xs...? + ?x", "surface prints meta vars");

  Ref unary = read_surface(arena, "-x + -1");
  check_eq(print_core(unary), "(+ (neg x) -1)", "surface unary minus");

  Ref chained = read_surface(
      arena, "simplify(sqrt(x^2)) @ assume(x >= 0) @ via(sympy)");
  check_eq(print_core(chained),
           "(simplify (sqrt (^ x 2)) :assume (>= x 0) :via sympy)",
           "surface chained contexts");

  Ref quantified = read_surface(arena, "forall[x : R](x^2 >= 0)");
  check_eq(print_core(quantified),
           "(forall (binder x R) (>= (^ x 2) 0))",
           "surface forall binder");

  Ref vector_access = read_surface(arena, "v[i]");
  check_eq(print_core(vector_access), "(at v i)",
           "surface vector access lowers to at");
  check_eq(print_surface(vector_access), "v[i]",
           "surface vector access prints with brackets");
  check_eq(print_latex(vector_access), "v_{i}",
           "latex vector access prints as subscript");
  check(same_tree(vector_access,
                  read_surface(arena, print_surface(vector_access))),
        "surface vector access round-trip");

  Ref matrix_access = read_surface(arena, "M[i, j]");
  check_eq(print_core(matrix_access), "(at M i j)",
           "surface matrix access lowers to variadic at");
  check_eq(print_surface(matrix_access), "M[i, j]",
           "surface matrix access prints with comma-separated brackets");
  check_eq(print_latex(matrix_access), "M_{ij}",
           "latex matrix access prints compact subscripts");

  Ref computed_access = read_surface(arena, "(f(x))[i]");
  check_eq(print_core(computed_access), "(at (f x) i)",
           "surface postfix access applies after calls");

  Ref function_muscle_memory = read_surface(arena, "sin[x]");
  check_eq(print_core(function_muscle_memory), "(at sin x)",
           "surface known function bracket parses as access");

  Ref vector_slice = read_surface(arena, "v[a..b]");
  check_eq(print_core(vector_slice), "(slice v (range a b))",
           "surface range access lowers to slice");
  check_eq(print_surface(vector_slice), "v[a..b]",
           "surface vector slice prints with brackets");
  check_eq(print_latex(vector_slice), "v_{a..b}",
           "latex vector slice prints range subscript");
  check(same_tree(vector_slice,
                  read_surface(arena, print_surface(vector_slice))),
        "surface vector slice round-trip");

  Ref stepped_slice = read_surface(arena, "v[a..b, step=k]");
  check_eq(print_core(stepped_slice), "(slice v (range a b :step k))",
           "surface stepped slice stores step on range");
  check_eq(print_surface(stepped_slice), "v[a..b, step=k]",
           "surface stepped slice prints step in brackets");
  check_eq(print_latex(stepped_slice), "v_{a:k:b}",
           "latex stepped slice prints compact stride");

  Ref end_slice = read_surface(arena, "v[2..end]");
  check_eq(print_core(end_slice), "(slice v (range 2 (end v 1)))",
           "surface end in slice is axis-aware");
  check_eq(print_surface(end_slice), "v[2..end]",
           "surface end in slice prints bracket-local keyword");

  Ref matrix_slice = read_surface(arena, "M[1..end, all]");
  check_eq(print_core(matrix_slice),
           "(slice M (range 1 (end M 1)) all)",
           "surface matrix slice rewrites end with axis");
  check_eq(print_surface(matrix_slice), "M[1..end, all]",
           "surface matrix slice prints bracket-local end");

  Ref dict = read_surface(arena, "dict{ \"mass\" : m }");
  check_eq(print_core(dict), "(dict (pair \"mass\" m))",
           "surface dict literal lowers to dict pairs");
  check_eq(print_surface(dict), "dict{ \"mass\" : m }",
           "surface dict literal prints with named head");
  check(same_tree(dict, read_surface(arena, print_surface(dict))),
        "surface dict literal round-trip");

  Ref dict_access = read_surface(arena, "d[\"mass\"]");
  check_eq(print_core(dict_access), "(at d \"mass\")",
           "surface dict access reuses at");

  Ref structural_keys = read_surface(arena, "dict{ x+y : 1, y+x : 2 }");
  check_eq(print_core(structural_keys),
           "(dict (pair (+ x y) 1) (pair (+ y x) 2))",
           "surface dict keys remain structural");
  check_eq(print_latex(read_surface(arena, "dict{ mass : m }")),
           "\\left\\{ mass \\mapsto m \\right\\}",
           "latex dict prints maplets");

  Ref goal = read_surface(
      arena, "goal g: exists[?F : Smooth](int[x : 0..1](?F) = pi / 4)");
  check_eq(print_surface(goal),
           "goal g: exists[?F : Smooth](int[x : 0..1](?F) = pi / 4)",
           "surface goal wrapper");

  Ref rule = read_surface(
      arena, "rule pyth: sin(?a)^2 + cos(?a)^2 ~> 1 when ?a > 0");
  check_eq(print_surface(rule),
           "rule pyth: sin(?a) ^ 2 + cos(?a) ^ 2 ~> 1 when ?a > 0",
           "surface rule wrapper with guard");
}

void latex_examples() {
  Arena arena;
  check_eq(print_latex(read_surface(arena, "alpha + beta_mu")),
           "\\alpha + \\beta_{\\mu}", "latex greek symbols and subscript");
  check_eq(print_latex(read_core(arena, "(* (+ a b) c)")),
           "\\left(a + b\\right) c", "latex precedence parentheses");
  check_eq(print_latex(read_surface(arena, "{ i^2 | i : 1..n, i > 0 }")),
           "\\left\\{ i^{2} \\mid i = 1,\\ldots,n,\\; i > 0 \\right\\}",
           "latex set-builder");
  check_eq(print_latex(read_surface(arena, "?xs...?")),
           "?xs\\ldots?", "latex optional sequence meta");
  check_eq(print_latex(read_surface(arena, "-(a + b)")),
           "-\\left(a + b\\right)", "latex negation parentheses");
  check_eq(print_surface(read_core(arena, "(idx T (up mu) (down nu))")),
           "T^mu_nu", "surface prints tensor index variance");
  check_eq(print_latex(read_core(arena, "(idx T (up mu) (down nu))")),
           "T^{\\mu}_{\\nu}", "latex prints tensor index variance");
}

void sympy_examples() {
  Arena arena;
  check_eq(print_sympy(read_surface(arena, "int[x : 0..1](sin(pi*x))")),
           "Integral(sin(pi*x), (x, 0, 1))", "sympy integral");
  check_eq(print_sympy(read_surface(arena, "sum[x : 0..1](x * 2 + 1)")),
           "Sum(x*2+1, (x, 0, 1))", "sympy finite sum");
  check_eq(print_sympy(read_surface(arena, "sqrt(x^2)")),
           "sqrt(x**2)", "sympy sqrt power");
  check_eq(print_sympy(read_surface(arena, "x |-> x^2")),
           "Lambda(x, x**2)", "sympy lambda");
  check_eq(print_sympy(read_surface(arena, "prod[x : 1..n](x)")),
           "Product(x, (x, 1, n))", "sympy product");
  check_eq(print_sympy(read_surface(arena, "lim[x -> 0](sin(x) / x)")),
           "Limit(sin(x)/x, x, 0)", "sympy limit");
  check_eq(print_sympy(read_surface(arena, "diff[x, x](sin(x))")),
           "diff(sin(x), x, x)", "sympy derivative");
  check_eq(print_sympy(read_surface(arena, "x >= 0")),
           "Ge(x, 0)", "sympy relation");
  check_throws_contains(
      []() {
        Arena a;
        (void)print_sympy(read_surface(
            a, "rule pyth: sin(?a)^2 + cos(?a)^2 ~> 1 when ?a > 0"));
      },
      "SymPy emit does not support rule", "sympy rejects rules");
  check_throws_contains(
      []() {
        Arena a;
        (void)print_sympy(
            read_surface(a, "simplify(sqrt(x^2)) @ assume(x >= 0)"));
      },
      "attributed expression", "sympy rejects attributed expressions");

  check_eq(print_core(read_sympy_srepr(
               arena, "Integral(sin(Mul(pi, Symbol('x'))), "
                      "Tuple(Symbol('x'), Integer(0), Integer(1)))")),
           "(int (binder x (range 0 1)) (sin (* pi x)))",
           "sympy srepr reads integral");
  check_eq(print_core(read_sympy_srepr(
               arena, "Sum(Add(Mul(Symbol('x'), Integer(2)), Integer(1)), "
                      "Tuple(Symbol('x'), Integer(0), Integer(1)))")),
           "(sum (binder x (range 0 1)) (+ (* x 2) 1))",
           "sympy srepr reads sum");
  check_eq(print_core(read_sympy_srepr(
               arena, "Lambda(Tuple(Symbol('x')), Pow(Symbol('x'), Integer(2)))")),
           "(lam (binder x _) (^ x 2))",
           "sympy srepr reads lambda tuple");
  check_eq(print_core(read_sympy_srepr(
               arena, "Add(Mul(Integer(-1), Symbol('x')), Integer(-1))")),
           "(+ (neg x) -1)", "sympy srepr normalizes unary minus");
  check_eq(print_core(read_sympy_srepr(
               arena, "Derivative(sin(Symbol('x')), Tuple(Symbol('x'), Integer(2)))")),
           "(diff (sin x) x x)", "sympy srepr reads derivative");
  check_eq(print_core(read_sympy_srepr(
               arena, "Limit(sin(Symbol('x')), Symbol('x'), Integer(0))")),
           "(lim (binder x (approach 0)) (sin x))", "sympy srepr reads limit");
  check_eq(print_core(read_sympy_srepr(
               arena, "Mul(Pow(Symbol('x'), Integer(-1)), sin(Symbol('x')))")),
           "(/ (sin x) x)", "sympy srepr normalizes reciprocal first");
  check_eq(print_core(read_sympy_srepr(
               arena, "Product(Symbol('x'), Tuple(Symbol('x'), Integer(1), Symbol('n')))")),
           "(prod (binder x (range 1 n)) x)", "sympy srepr reads product");
  check_eq(print_core(read_sympy_srepr(
               arena, "GreaterThan(Symbol('x'), Integer(0))")),
           "(>= x 0)", "sympy srepr reads relation");
}

void audit_regressions() {
  Arena arena;
  Ref left_power = read_core(arena, "(^ (^ a b) c)");
  check_eq(print_surface(left_power), "(a ^ b) ^ c",
           "right-assoc printer preserves left-nested power");
  check(same_tree(left_power, read_surface(arena, print_surface(left_power))),
        "left-nested power surface round-trip");

  Ref left_arrow = read_core(arena, "(-> (-> A B) C)");
  check_eq(print_surface(left_arrow), "(A -> B) -> C",
           "right-assoc printer preserves left-nested arrow");
  check(same_tree(left_arrow, read_surface(arena, print_surface(left_arrow))),
        "left-nested arrow surface round-trip");

  Ref atom_context = read_surface(arena, "x @ assume(y > 0)");
  check_eq(print_core(atom_context), "(@ x (assume (> y 0)))",
           "context on atom stays explicit");
  check(same_tree(atom_context,
                  read_surface(arena, print_surface(atom_context))),
        "context on atom surface round-trip");

  Ref subst_sum = read_core(arena, "(+ (subst a (= x 1)) b)");
  check_eq(print_surface(subst_sum), "(a @ subst{x = 1}) + b",
           "subst printer respects parent precedence");
  check(same_tree(subst_sum, read_surface(arena, print_surface(subst_sum))),
        "subst in addition surface round-trip");

  Ref custom_access = read_surface(arena, "custom[i]");
  check_eq(print_core(custom_access), "(at custom i)",
           "custom head bracket syntax is access in v2");
  check_eq(print_surface(custom_access), "custom[i]",
           "custom head access prints with brackets");
  check_throws_contains(
      []() {
        Arena a;
        (void)read_surface(a, "custom[x : R](body)");
      },
      "expected ']'", "custom binder heads are rejected in v2");

  check_throws(
      []() {
        Arena a;
        (void)a.compound("f", {}, {{"assume", a.sym("x")},
                                   {"assume", a.sym("y")}});
      },
      "duplicate attributes rejected");
  check_throws(
      []() {
        Arena a;
        (void)a.rational("1", "00");
      },
      "zero rational denominator rejected");
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

  Ref escaped = arena.string("line\nnext\tend");
  std::string escaped_object = print_object(escaped);
  check(escaped_object.find("\\n") != std::string::npos,
        "object printer escapes newline");
  check(escaped_object.find("\\t") != std::string::npos,
        "object printer escapes tab");
  check(same_tree(escaped, read_object(arena, escaped_object)),
        "object escaped string round-trip");

  Ref with_unknown = read_object(
      arena,
      "{\"head\":\"sin\",\"source\":{\"line\":1,\"notes\":[true,null]},"
      "\"args\":[{\"atom\":\"sym\",\"value\":\"x\"}]}");
  check_eq(print_core(with_unknown), "(sin x)",
           "object reader skips unknown metadata keys");
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

void diagnostics_include_locations() {
  check_throws_contains(
      []() {
        Arena a;
        (void)read_surface(a, "int[x : 0..1](\nsin(pi*x)");
      },
      "line 2, column 10", "surface parser reports line/column");
  check_throws_contains(
      []() {
        Arena a;
        (void)read_core(a, "(+ x\n y");
      },
      "line 2, column 3", "core parser reports line/column at eof");
  check_throws_contains(
      []() {
        Arena a;
        (void)read_surface(a, "sum[x=2](x)");
      },
      "line 1, column 7", "binder diagnostic includes location");
  check_throws_contains(
      []() {
        Arena a;
        (void)read_strict(a, "sin(\nx");
      },
      "line 2, column 2", "strict parser reports line/column");
  check_throws_contains(
      []() {
        Arena a;
        (void)read_surface(a, "dict{ \"f\" : f : R -> R }");
      },
      "nested dict ascription must be parenthesized",
      "dict nested ascription diagnostic is targeted");
}

void validator_warnings() {
  Arena arena;

  Ref function_access = read_surface(arena, "sin[x]");
  std::vector<Diagnostic> function_warnings = validate(function_access);
  check_eq(std::to_string(function_warnings.size()), "1",
           "validator warns on known function access count");
  if (!function_warnings.empty()) {
    check_eq(function_warnings[0].code, "IndexingKnownFunction",
             "validator warns on known function access code");
    check(function_warnings[0].message.find("sin(...)") != std::string::npos,
          "validator known function warning suggests parentheses");
  }

  Ref real_access = read_surface(arena, "v[i]");
  check(validate(real_access).empty(),
        "validator is silent for ordinary index access");

  Ref bracket_end = read_surface(arena, "v[1..end]");
  check(validate(bracket_end).empty(),
        "validator is silent for bracket-local end");

  Ref free_end = read_surface(arena, "n := end");
  std::vector<Diagnostic> end_warnings = validate(free_end);
  check_eq(std::to_string(end_warnings.size()), "1",
           "validator warns on free end count");
  if (!end_warnings.empty()) {
    check_eq(end_warnings[0].code, "EndOutsideIndex",
             "validator warns on free end code");
  }
}

void layout_lexer_tests() {
  check_eq(layout_text("do:\n    return x\n"),
           "do : NEWLINE INDENT return x NEWLINE DEDENT",
           "layout lexer emits indent/dedent for do block");

  check_eq(layout_text("do:\n    while c:\n        return x\n    return y\n"),
           "do : NEWLINE INDENT while c : NEWLINE INDENT return x NEWLINE "
           "DEDENT return y NEWLINE DEDENT",
           "layout lexer emits nested block structure");

  check_eq(layout_text("do:\n    return f(\n        x,\n        y\n    )\n"),
           "do : NEWLINE INDENT return f ( x , y ) NEWLINE DEDENT",
           "layout lexer ignores indentation inside continuation lines");

  check_throws_contains(
      []() {
        (void)layout_text("x\n    y\n");
      },
      "unexpected indent", "layout lexer rejects indent without opener");

  check_throws_contains(
      []() {
        (void)layout_text("do:\nreturn x\n");
      },
      "expected indented block", "layout lexer requires block after colon");

  check_throws_contains(
      []() {
        (void)layout_text("do:\n    return x\n  return y\n");
      },
      "dedent does not match", "layout lexer rejects mismatched dedent");

  check_throws_contains(
      []() {
        (void)layout_text("do:\n\treturn x\n");
      },
      "tabs are not allowed", "layout lexer rejects tabs in indentation");
}

void new_feature_regressions() {
  Arena arena;

  // set literal: bare { } remains readable, named set{ } is canonical v2
  Ref set3 = read_surface(arena, "{ 1, 2, 3 }");
  check_eq(print_core(set3), "(set 1 2 3)", "set literal lowers to set compound");
  check_eq(print_surface(set3), "set{ 1, 2, 3 }",
           "set literal prints with named set head");
  check(same_tree(set3, read_surface(arena, print_surface(set3))),
        "set literal surface round-trip");

  Ref named_set = read_surface(arena, "set{ 1, 2, 3 }");
  check(same_tree(named_set, set3), "named set literal parses to set compound");

  Ref seq = read_surface(arena, "seq{ a, b, c }");
  check_eq(print_core(seq), "(seq a b c)", "seq literal lowers to seq compound");
  check_eq(print_surface(seq), "seq{ a, b, c }",
           "seq literal prints with named seq head");
  check(same_tree(seq, read_surface(arena, print_surface(seq))),
        "seq literal surface round-trip");

  // broadcast: f.(args) notation survives surface round-trip
  Ref bc = read_surface(arena, "f.(a, b)");
  check_eq(print_core(bc), "(broadcast f (args a b))",
           "broadcast lowers to broadcast/args compound");
  check_eq(print_surface(bc), "f.(a, b)", "broadcast prints with dot-paren");
  check(same_tree(bc, read_surface(arena, print_surface(bc))),
        "broadcast surface round-trip");

  // lim: LaTeX renders as \lim with arrow subscript
  check_eq(print_latex(read_surface(arena, "lim[x -> 0](sin(x)/x)")),
           "\\lim_{x \\to 0} \\frac{\\sin\\left(x\\right)}{x}",
           "latex lim with approach");

  // sum: LaTeX renders as \sum with range bounds
  check_eq(print_latex(read_surface(arena, "sum[k : 1..n](k)")),
           "\\sum_{k = 1}^{n} k", "latex sum with range");

  // prod: LaTeX renders as \prod with range bounds
  check_eq(print_latex(read_surface(arena, "prod[k : 1..n](a_k)")),
           "\\prod_{k = 1}^{n} a_{k}", "latex prod with range");

  // rational: -0 denominator rejected
  check_throws(
      []() {
        Arena a;
        (void)a.rational("1", "-0");
      },
      "negative zero denominator rejected");

  // rational: invalid numerator gives distinct error
  check_throws_contains(
      []() {
        Arena a;
        (void)a.rational("x", "2");
      },
      "numerator", "rational rejects non-numeric numerator");

  // sympy srepr: bare -1 in Mul normalised to neg
  check_eq(print_core(read_sympy_srepr(
               arena, "Add(Mul(-1, Symbol('x')), Integer(-1))")),
           "(+ (neg x) -1)", "sympy srepr bare Mul(-1, x) normalizes to neg");
}

} // namespace

int main() {
  corpus_round_trips();
  core_round_trips();
  arena_interns_and_compares_exact_trees();
  strict_round_trips();
  surface_examples();
  latex_examples();
  sympy_examples();
  audit_regressions();
  object_round_trips();
  cross_mode_agreement();
  adversarial_grammar();
  diagnostics_include_locations();
  validator_warnings();
  layout_lexer_tests();
  new_feature_regressions();

  if (failures) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "All Facet Gen 1 tests passed\n";
  return 0;
}
