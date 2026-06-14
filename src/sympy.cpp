#include "facet_internal.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace facet {
namespace {

using internal::attr_value;
using internal::escape;
using internal::join;

struct Srepr {
  std::string head;
  std::string text;
  bool string = false;
  bool number = false;
  bool call = false;
  std::vector<Srepr> args;
};

class SreprParser {
public:
  explicit SreprParser(std::string input) : input_(std::move(input)) {}

  Srepr parse() {
    Srepr value = expr();
    ws();
    if (pos_ != input_.size()) {
      throw Error("trailing input after SymPy srepr at byte " +
                  std::to_string(pos_ + 1));
    }
    return value;
  }

private:
  std::string input_;
  std::size_t pos_ = 0;

  void ws() {
    while (pos_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[pos_]))) {
      ++pos_;
    }
  }

  bool take(char c) {
    ws();
    if (pos_ < input_.size() && input_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  void expect(char c) {
    if (!take(c)) {
      throw Error(std::string("expected '") + c +
                  "' in SymPy srepr at byte " + std::to_string(pos_ + 1));
    }
  }

  std::string ident() {
    ws();
    if (pos_ >= input_.size() ||
        !(std::isalpha(static_cast<unsigned char>(input_[pos_])) ||
          input_[pos_] == '_')) {
      throw Error("expected identifier in SymPy srepr at byte " +
                  std::to_string(pos_ + 1));
    }
    std::size_t start = pos_++;
    while (pos_ < input_.size() &&
           (std::isalnum(static_cast<unsigned char>(input_[pos_])) ||
            input_[pos_] == '_')) {
      ++pos_;
    }
    return input_.substr(start, pos_ - start);
  }

  std::string number() {
    ws();
    std::size_t start = pos_;
    if (pos_ < input_.size() && input_[pos_] == '-') {
      ++pos_;
    }
    while (pos_ < input_.size() &&
           std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
      ++pos_;
    }
    if (pos_ < input_.size() && input_[pos_] == '.') {
      ++pos_;
      while (pos_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
      }
    }
    if (start == pos_ || (input_[start] == '-' && start + 1 == pos_)) {
      throw Error("expected number in SymPy srepr at byte " +
                  std::to_string(start + 1));
    }
    return input_.substr(start, pos_ - start);
  }

  std::string quoted() {
    ws();
    if (pos_ >= input_.size() ||
        (input_[pos_] != '\'' && input_[pos_] != '"')) {
      throw Error("expected string in SymPy srepr at byte " +
                  std::to_string(pos_ + 1));
    }
    char quote = input_[pos_++];
    std::string out;
    while (pos_ < input_.size() && input_[pos_] != quote) {
      if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
        ++pos_;
      }
      out.push_back(input_[pos_++]);
    }
    if (pos_ >= input_.size()) {
      throw Error("unterminated string in SymPy srepr");
    }
    ++pos_;
    return out;
  }

  void skip_keyword_value() {
    (void)ident();
    ws();
    if (take('=')) {
      (void)expr();
    }
  }

  bool looks_like_keyword() {
    ws();
    std::size_t p = pos_;
    if (p >= input_.size() ||
        !(std::isalpha(static_cast<unsigned char>(input_[p])) ||
          input_[p] == '_')) {
      return false;
    }
    ++p;
    while (p < input_.size() &&
           (std::isalnum(static_cast<unsigned char>(input_[p])) ||
            input_[p] == '_')) {
      ++p;
    }
    while (p < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[p]))) {
      ++p;
    }
    return p < input_.size() && input_[p] == '=';
  }

  Srepr expr() {
    ws();
    if (pos_ >= input_.size()) {
      throw Error("unexpected end of SymPy srepr");
    }
    if (input_[pos_] == '\'' || input_[pos_] == '"') {
      Srepr s;
      s.string = true;
      s.text = quoted();
      return s;
    }
    if (std::isdigit(static_cast<unsigned char>(input_[pos_])) ||
        input_[pos_] == '-') {
      Srepr n;
      n.number = true;
      n.text = number();
      return n;
    }

    std::string name = ident();
    if (!take('(')) {
      Srepr bare;
      bare.head = name;
      bare.text = name;
      return bare;
    }

    Srepr call;
    call.head = name;
    call.call = true;
    if (!take(')')) {
      do {
        if (looks_like_keyword()) {
          skip_keyword_value();
        } else {
          call.args.push_back(expr());
        }
      } while (take(','));
      expect(')');
    }
    return call;
  }
};

std::string print_sympy_prec(Ref ref, int parent_prec);

std::string sympy_atom(Ref ref) {
  if (ref->tag == Tag::Str) {
    return "\"" + escape(ref->text) + "\"";
  }
  return ref->text;
}

std::string sympy_wrap(std::string out, int prec, int parent_prec) {
  if (prec < parent_prec) {
    return "(" + out + ")";
  }
  return out;
}

std::string sympy_function_name(const std::string& head) {
  static const std::unordered_map<std::string, std::string> names = {
      {"sin", "sin"},   {"cos", "cos"},       {"tan", "tan"},
      {"log", "log"},   {"sqrt", "sqrt"},      {"simplify", "simplify"},
      {"expand", "expand"}};
  auto it = names.find(head);
  return it != names.end() ? it->second : "";
}

std::string shell_quote(const std::string& text) {
  std::string out = "'";
  for (char c : text) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out.push_back(c);
    }
  }
  out += "'";
  return out;
}

std::string python_string(const std::string& text) {
  std::string out = "'";
  for (char c : text) {
    if (c == '\\' || c == '\'') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out += "'";
  return out;
}

std::string run_command(const std::string& command) {
  std::array<char, 256> buffer{};
  std::string output;
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    throw Error("failed to start SymPy subprocess");
  }
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
    output += buffer.data();
  }
  int code = pclose(pipe);
  if (code != 0) {
    throw Error("SymPy subprocess failed: " + output);
  }
  while (!output.empty() &&
         (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }
  return output;
}

std::string sympy_python() {
  const char* configured = std::getenv("FACET_SYMPY_PYTHON");
  if (configured && configured[0] != '\0') {
    return configured;
  }
  return "python3";
}

std::string sympy_call(const std::string& fn, Ref ref) {
  std::vector<std::string> parts;
  for (Ref arg : ref->args) {
    parts.push_back(print_sympy_prec(arg, 0));
  }
  return fn + "(" + join(parts, ", ") + ")";
}

std::string print_sympy_prec(Ref ref, int parent_prec) {
  if (ref->tag != Tag::Compound) {
    return sympy_atom(ref);
  }
  if (!ref->attrs.empty()) {
    throw Error("SymPy emit does not support attributed expression: " +
                ref->text);
  }
  if (ref->text == "meta") {
    throw Error("SymPy emit does not support meta variables");
  }
  if (ref->text == "rule" || ref->text == "goal" ||
      ref->text == "forall" || ref->text == "exists") {
    throw Error("SymPy emit does not support " + ref->text);
  }
  if (ref->text == "neg" && ref->args.size() == 1) {
    return sympy_wrap("-" + print_sympy_prec(ref->args[0], 80), 80,
                      parent_prec);
  }
  if (ref->text == "^" && ref->args.size() == 2) {
    int prec = 70;
    std::string out = print_sympy_prec(ref->args[0], prec + 1) + "**" +
                      print_sympy_prec(ref->args[1], prec);
    return sympy_wrap(out, prec, parent_prec);
  }
  if (ref->text == "/" && ref->args.size() == 2) {
    int prec = 60;
    std::string out = print_sympy_prec(ref->args[0], prec) + "/" +
                      print_sympy_prec(ref->args[1], prec + 1);
    return sympy_wrap(out, prec, parent_prec);
  }
  if ((ref->text == "+" || ref->text == "-" || ref->text == "*") &&
      ref->args.size() == 2) {
    int prec = ref->text == "+" || ref->text == "-" ? 50 : 60;
    std::string op = ref->text == "*" ? "*" : ref->text;
    std::string out = print_sympy_prec(ref->args[0], prec) + op +
                      print_sympy_prec(ref->args[1], prec + 1);
    return sympy_wrap(out, prec, parent_prec);
  }
  if ((ref->text == "=" || ref->text == "!=" || ref->text == ">" ||
       ref->text == ">=" || ref->text == "<" || ref->text == "<=") &&
      ref->args.size() == 2) {
    static const std::vector<std::pair<std::string, std::string>> rels = {
        {"=", "Eq"},  {"!=", "Ne"}, {">", "Gt"},
        {">=", "Ge"}, {"<", "Lt"},  {"<=", "Le"}};
    for (const auto& rel : rels) {
      if (ref->text == rel.first) {
        return rel.second + "(" + print_sympy_prec(ref->args[0], 0) + ", " +
               print_sympy_prec(ref->args[1], 0) + ")";
      }
    }
  }
  if (ref->text == "int" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    Ref binder = ref->args[0];
    Ref domain = binder->args[1];
    if (domain->tag == Tag::Compound && domain->text == "range" &&
        domain->args.size() == 2) {
      return "Integral(" + print_sympy_prec(ref->args[1], 0) + ", (" +
             print_sympy_prec(binder->args[0], 0) + ", " +
             print_sympy_prec(domain->args[0], 0) + ", " +
             print_sympy_prec(domain->args[1], 0) + "))";
    }
  }
  if (ref->text == "sum" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    Ref binder = ref->args[0];
    Ref domain = binder->args[1];
    if (domain->tag == Tag::Compound && domain->text == "range" &&
        domain->args.size() == 2) {
      return "Sum(" + print_sympy_prec(ref->args[1], 0) + ", (" +
             print_sympy_prec(binder->args[0], 0) + ", " +
             print_sympy_prec(domain->args[0], 0) + ", " +
             print_sympy_prec(domain->args[1], 0) + "))";
    }
  }
  if (ref->text == "prod" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    Ref binder = ref->args[0];
    Ref domain = binder->args[1];
    if (domain->tag == Tag::Compound && domain->text == "range" &&
        domain->args.size() == 2) {
      return "Product(" + print_sympy_prec(ref->args[1], 0) + ", (" +
             print_sympy_prec(binder->args[0], 0) + ", " +
             print_sympy_prec(domain->args[0], 0) + ", " +
             print_sympy_prec(domain->args[1], 0) + "))";
    }
  }
  if (ref->text == "lim" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    Ref binder = ref->args[0];
    Ref domain = binder->args[1];
    if (domain->tag == Tag::Compound && domain->text == "approach" &&
        domain->args.size() == 1) {
      return "Limit(" + print_sympy_prec(ref->args[1], 0) + ", " +
             print_sympy_prec(binder->args[0], 0) + ", " +
             print_sympy_prec(domain->args[0], 0) + ")";
    }
  }
  if (ref->text == "diff" && ref->args.size() >= 2) {
    std::vector<std::string> parts{print_sympy_prec(ref->args[0], 0)};
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      parts.push_back(print_sympy_prec(ref->args[i], 0));
    }
    return "diff(" + join(parts, ", ") + ")";
  }
  if (ref->text == "lam" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    return "Lambda(" + print_sympy_prec(ref->args[0]->args[0], 0) + ", " +
           print_sympy_prec(ref->args[1], 0) + ")";
  }
  if (std::string fn = sympy_function_name(ref->text); !fn.empty()) {
    return sympy_call(fn, ref);
  }
  throw Error("SymPy emit does not support head: " + ref->text);
}

Ref from_srepr(Arena& arena, const Srepr& value);

bool is_integer_literal(const Srepr& value, const std::string& text) {
  return value.call && value.head == "Integer" && value.args.size() == 1 &&
         value.args[0].number && value.args[0].text == text;
}

Ref fold_variadic(Arena& arena, const std::string& head,
                  const std::vector<Srepr>& args) {
  if (args.empty()) {
    throw Error("SymPy srepr " + head + " needs at least one argument");
  }
  Ref out = from_srepr(arena, args.front());
  for (std::size_t i = 1; i < args.size(); ++i) {
    out = arena.compound(head, {out, from_srepr(arena, args[i])});
  }
  return out;
}

Ref tuple_at(Arena& arena, const Srepr& value, std::size_t index) {
  if (!value.call || value.head != "Tuple" || index >= value.args.size()) {
    throw Error("expected SymPy Tuple argument");
  }
  return from_srepr(arena, value.args[index]);
}

Ref from_srepr(Arena& arena, const Srepr& value) {
  if (value.string) {
    return arena.string(value.text);
  }
  if (value.number) {
    return value.text.find('.') == std::string::npos ? arena.integer(value.text)
                                                     : arena.real(value.text);
  }
  if (!value.call) {
    return arena.sym(value.text);
  }

  if (value.head == "Symbol" && value.args.size() == 1 && value.args[0].string) {
    return arena.sym(value.args[0].text);
  }
  if (value.head == "Integer" && value.args.size() == 1) {
    return arena.integer(value.args[0].text);
  }
  if (value.head == "Rational" && value.args.size() == 2) {
    return arena.rational(value.args[0].text, value.args[1].text);
  }
  if (value.head == "Float" && !value.args.empty()) {
    return arena.real(value.args[0].text);
  }
  if (value.head == "Add") {
    return fold_variadic(arena, "+", value.args);
  }
  if (value.head == "Mul") {
    bool first_is_neg_one =
        is_integer_literal(value.args[0], "-1") ||
        (value.args[0].number && value.args[0].text == "-1");
    if (value.args.size() == 2 && first_is_neg_one) {
      return arena.compound("neg", {from_srepr(arena, value.args[1])});
    }
    if (value.args.size() == 2 && value.args[1].call &&
        value.args[1].head == "Pow" && value.args[1].args.size() == 2 &&
        is_integer_literal(value.args[1].args[1], "-1")) {
      return arena.compound(
          "/", {from_srepr(arena, value.args[0]),
                from_srepr(arena, value.args[1].args[0])});
    }
    if (value.args.size() == 2 && value.args[0].call &&
        value.args[0].head == "Pow" && value.args[0].args.size() == 2 &&
        is_integer_literal(value.args[0].args[1], "-1")) {
      return arena.compound(
          "/", {from_srepr(arena, value.args[1]),
                from_srepr(arena, value.args[0].args[0])});
    }
    return fold_variadic(arena, "*", value.args);
  }
  if (value.head == "Pow" && value.args.size() == 2) {
    return arena.compound("^",
                          {from_srepr(arena, value.args[0]),
                           from_srepr(arena, value.args[1])});
  }
  if ((value.head == "sin" || value.head == "cos" || value.head == "tan" ||
       value.head == "log" || value.head == "sqrt") &&
      value.args.size() == 1) {
    return arena.compound(value.head, {from_srepr(arena, value.args[0])});
  }
  if ((value.head == "Integral" || value.head == "Sum" ||
       value.head == "Product") &&
      value.args.size() == 2 && value.args[1].call &&
      value.args[1].head == "Tuple" && value.args[1].args.size() == 3) {
    Ref binder =
        arena.compound("binder",
                       {tuple_at(arena, value.args[1], 0),
                        arena.compound("range",
                                       {tuple_at(arena, value.args[1], 1),
                                        tuple_at(arena, value.args[1], 2)})});
    std::string head = value.head == "Integral" ? "int" :
                       value.head == "Sum" ? "sum" : "prod";
    return arena.compound(head, {binder, from_srepr(arena, value.args[0])});
  }
  if (value.head == "Limit" && value.args.size() >= 3) {
    Ref binder = arena.compound(
        "binder",
        {from_srepr(arena, value.args[1]),
         arena.compound("approach", {from_srepr(arena, value.args[2])})});
    return arena.compound("lim", {binder, from_srepr(arena, value.args[0])});
  }
  if (value.head == "Derivative" && value.args.size() >= 2) {
    std::vector<Ref> args{from_srepr(arena, value.args[0])};
    for (std::size_t i = 1; i < value.args.size(); ++i) {
      if (value.args[i].call && value.args[i].head == "Tuple" &&
          value.args[i].args.size() == 2 &&
          is_integer_literal(value.args[i].args[1], "2")) {
        args.push_back(tuple_at(arena, value.args[i], 0));
        args.push_back(tuple_at(arena, value.args[i], 0));
      } else if (value.args[i].call && value.args[i].head == "Tuple" &&
                 value.args[i].args.size() == 2 &&
                 is_integer_literal(value.args[i].args[1], "1")) {
        args.push_back(tuple_at(arena, value.args[i], 0));
      } else {
        args.push_back(from_srepr(arena, value.args[i]));
      }
    }
    return arena.compound("diff", args);
  }
  if ((value.head == "Equality" || value.head == "Unequality" ||
       value.head == "GreaterThan" || value.head == "StrictGreaterThan" ||
       value.head == "LessThan" || value.head == "StrictLessThan") &&
      value.args.size() == 2) {
    static const std::vector<std::pair<std::string, std::string>> rels = {
        {"Equality", "="},          {"Unequality", "!="},
        {"GreaterThan", ">="},      {"StrictGreaterThan", ">"},
        {"LessThan", "<="},         {"StrictLessThan", "<"}};
    for (const auto& rel : rels) {
      if (value.head == rel.first) {
        return arena.compound(rel.second,
                              {from_srepr(arena, value.args[0]),
                               from_srepr(arena, value.args[1])});
      }
    }
  }
  if (value.head == "Lambda" && value.args.size() == 2) {
    Ref bound = nullptr;
    if (value.args[0].call && value.args[0].head == "Tuple" &&
        value.args[0].args.size() == 1) {
      bound = tuple_at(arena, value.args[0], 0);
    } else {
      bound = from_srepr(arena, value.args[0]);
    }
    return arena.compound(
        "lam", {arena.compound("binder", {bound, arena.sym("_")}),
                from_srepr(arena, value.args[1])});
  }

  throw Error("SymPy srepr reader does not support head: " + value.head);
}

// Map symbol name → SymPy assumption property names (e.g. "nonnegative").
using AssumeMap = std::map<std::string, std::vector<std::string>>;

// Walk a Facet assumption expression (e.g. (>= x 0)) and populate out.
void collect_assumptions(Ref cond, AssumeMap& out) {
  auto one = [&](Ref c) {
    if (c->tag != Tag::Compound || c->args.size() != 2) { return; }
    const std::string& op = c->text;
    Ref lhs = c->args[0];
    Ref rhs = c->args[1];
    bool lsym = lhs->tag == Tag::Sym;
    bool rsym = rhs->tag == Tag::Sym;
    bool rzero = rhs->tag == Tag::Int && rhs->text == "0";
    bool lzero = lhs->tag == Tag::Int && lhs->text == "0";
    auto add = [&](const std::string& sym, const char* prop) {
      out[sym].push_back(prop);
    };
    if (lsym && rzero) {
      if (op == ">=")      add(lhs->text, "nonnegative");
      else if (op == ">")  add(lhs->text, "positive");
      else if (op == "<=") add(lhs->text, "nonpositive");
      else if (op == "<")  add(lhs->text, "negative");
    } else if (rsym && lzero) {
      if (op == "<=")      add(rhs->text, "nonnegative");
      else if (op == "<")  add(rhs->text, "positive");
      else if (op == ">=") add(rhs->text, "nonpositive");
      else if (op == ">")  add(rhs->text, "negative");
    }
  };
  if (cond->tag == Tag::Compound && cond->text == "and") {
    for (Ref arg : cond->args) { one(arg); }
  } else {
    one(cond);
  }
}

// Build Python code that evaluates `source` as a SymPy expression and
// prints its srepr. `assumptions` maps symbol names to SymPy property names.
std::string build_eval_code(const std::string& source, const AssumeMap& assumptions) {
  // Build assume_map Python dict literal
  std::string assume_lit = "{";
  bool first_sym = true;
  for (const auto& [name, props] : assumptions) {
    if (!first_sym) { assume_lit += ", "; }
    first_sym = false;
    assume_lit += python_string(name) + ": {";
    for (std::size_t i = 0; i < props.size(); ++i) {
      if (i) { assume_lit += ", "; }
      assume_lit += python_string(props[i]) + ": True";
    }
    assume_lit += "}";
  }
  assume_lit += "}";

  return
    "import re\n"
    "import sympy as s\n"
    "src = " + python_string(source) + "\n"
    "assume_map = " + assume_lit + "\n"
    "names = set(re.findall(r'\\b[A-Za-z_]\\w*\\b', src))\n"
    "known = {\n"
    "  'Integral': s.Integral, 'Sum': s.Sum, 'Product': s.Product,\n"
    "  'Limit': s.Limit, 'Lambda': s.Lambda, 'diff': s.diff,\n"
    "  'sin': s.sin, 'cos': s.cos, 'tan': s.tan, 'log': s.log,\n"
    "  'sqrt': s.sqrt, 'simplify': s.simplify, 'expand': s.expand,\n"
    "  'Eq': s.Eq, 'Ne': s.Ne, 'Gt': s.Gt, 'Ge': s.Ge,\n"
    "  'Lt': s.Lt, 'Le': s.Le, 'pi': s.pi\n"
    "}\n"
    "env = dict(known)\n"
    "for name in names:\n"
    "  if name not in env:\n"
    "    kwargs = {k: v for d in [assume_map.get(name, {})] for k, v in d.items()}\n"
    "    env[name] = s.Symbol(name, **kwargs)\n"
    "expr = eval(src, {'__builtins__': {}}, env)\n"
    "print(s.srepr(expr))\n";
}

} // namespace

Ref read_sympy_srepr(Arena& arena, const std::string& input) {
  return from_srepr(arena, SreprParser(input).parse());
}

std::string print_sympy_srepr(Ref ref) {
  return run_command(shell_quote(sympy_python()) + " -c " +
                     shell_quote(build_eval_code(print_sympy(ref), {})) + " 2>&1");
}

Ref evaluate_sympy(Arena& arena, Ref ref) {
  // @via(sympy) signals that the expression should be evaluated, not just
  // represented. Strip context attrs, carry @assume into Symbol() kwargs.
  Ref via = attr_value(ref, "via");
  if (via && via->tag == Tag::Sym && via->text == "sympy") {
    Ref inner = arena.compound(ref->text, ref->args);
    AssumeMap assumptions;
    Ref assume = attr_value(ref, "assume");
    if (assume) { collect_assumptions(assume, assumptions); }
    std::string code = build_eval_code(print_sympy(inner), assumptions);
    return read_sympy_srepr(arena, run_command(
        shell_quote(sympy_python()) + " -c " + shell_quote(code) + " 2>&1"));
  }
  return read_sympy_srepr(arena, print_sympy_srepr(ref));
}

std::string print_sympy(Ref ref) {
  return print_sympy_prec(ref, 0);
}

} // namespace facet
