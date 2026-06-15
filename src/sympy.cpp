#include "facet_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
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
  if (ref->text == "inf" || ref->text == "infinity") return "oo";
  if (ref->text == "pi") return "pi";
  return ref->text;
}

std::string sympy_wrap(std::string out, int prec, int parent_prec) {
  if (prec < parent_prec) {
    return "(" + out + ")";
  }
  return out;
}

enum class MapKind { Infix, Function, Relation };

struct KernelMapEntry {
  const char* head;
  MapKind kind;
  const char* target;
  int prec = 0;
};

struct KernelManifest {
  const char* name;
  const char* transport;
  const char* default_role;
  std::vector<KernelMapEntry> map;
};

const KernelManifest& sympy_manifest() {
  static const KernelManifest manifest = {
      "sympy",
      "subprocess:python3",
      "transformer",
      {
          {"+", MapKind::Infix, "+", 50},
          {"-", MapKind::Infix, "-", 50},
          {"*", MapKind::Infix, "*", 60},
          {"/", MapKind::Infix, "/", 60},
          {"^", MapKind::Infix, "**", 70},
          {"sin", MapKind::Function, "sin"},
          {"cos", MapKind::Function, "cos"},
          {"tan", MapKind::Function, "tan"},
          {"log", MapKind::Function, "log"},
          {"sqrt", MapKind::Function, "sqrt"},
          {"simplify", MapKind::Function, "simplify"},
          {"expand", MapKind::Function, "expand"},
          {"=", MapKind::Relation, "Eq"},
          {"!=", MapKind::Relation, "Ne"},
          {">", MapKind::Relation, "Gt"},
          {">=", MapKind::Relation, "Ge"},
          {"<", MapKind::Relation, "Lt"},
          {"<=", MapKind::Relation, "Le"},
          {"exp", MapKind::Function, "exp"},
          {"abs", MapKind::Function, "Abs"},
          {"factor", MapKind::Function, "factor"},
          {"int", MapKind::Function, "integrate"},
          {"sum", MapKind::Function, "summation"},
          {"prod", MapKind::Function, "Product"},
          {"lim", MapKind::Function, "limit"},
          {"diff", MapKind::Function, "diff"},
          {"lam", MapKind::Function, "Lambda"},
      }};
  return manifest;
}

const KernelManifest& stub_manifest() {
  static const KernelManifest manifest = {
      "stub", "none", "transformer", {}};
  return manifest;
}

const KernelManifest& python_manifest() {
  static const KernelManifest manifest = {
      "python",
      "source-only",
      "transformer",
      {
          {"+", MapKind::Infix, "+", 50},
          {"-", MapKind::Infix, "-", 50},
          {"*", MapKind::Infix, "*", 60},
          {"/", MapKind::Infix, "/", 60},
          {"^", MapKind::Infix, "**", 70},
          {"sin", MapKind::Function, "math.sin"},
          {"cos", MapKind::Function, "math.cos"},
          {"tan", MapKind::Function, "math.tan"},
          {"log", MapKind::Function, "math.log"},
          {"sqrt", MapKind::Function, "math.sqrt"},
          {"exp", MapKind::Function, "math.exp"},
          {"=", MapKind::Relation, "=="},
          {"!=", MapKind::Relation, "!="},
          {">", MapKind::Relation, ">"},
          {">=", MapKind::Relation, ">="},
          {"<", MapKind::Relation, "<"},
          {"<=", MapKind::Relation, "<="},
      }};
  return manifest;
}

const KernelManifest& kernel_manifest(const std::string& kernel) {
  if (kernel == "sympy") {
    return sympy_manifest();
  }
  if (kernel == "python") {
    return python_manifest();
  }
  if (kernel == "stub") {
    return stub_manifest();
  }
  throw Error("unknown kernel: " + kernel);
}

std::unordered_map<std::string_view, const KernelMapEntry*>
build_kernel_index(const KernelManifest& manifest) {
  std::unordered_map<std::string_view, const KernelMapEntry*> index;
  index.reserve(manifest.map.size() * 2);
  for (const auto& entry : manifest.map) {
    index.emplace(entry.head, &entry);
  }
  return index;
}

const std::unordered_map<std::string_view, const KernelMapEntry*>&
kernel_index(const KernelManifest& manifest) {
  if (std::string_view(manifest.name) == "sympy") {
    static const auto index = build_kernel_index(sympy_manifest());
    return index;
  }
  if (std::string_view(manifest.name) == "stub") {
    static const auto index = build_kernel_index(stub_manifest());
    return index;
  }
  if (std::string_view(manifest.name) == "python") {
    static const auto index = build_kernel_index(python_manifest());
    return index;
  }
  throw Error("unknown kernel manifest: " + std::string(manifest.name));
}

const KernelMapEntry* lookup_kernel_entry(const KernelManifest& manifest,
                                          const std::string& head) {
  const auto& index = kernel_index(manifest);
  auto it = index.find(head);
  return it != index.end() ? it->second : nullptr;
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

std::string print_source_prec(Ref ref, const KernelManifest& manifest,
                              int parent_prec);

std::string source_call(const std::string& fn, Ref ref,
                        const KernelManifest& manifest) {
  std::vector<std::string> parts;
  for (Ref arg : ref->args) {
    parts.push_back(print_source_prec(arg, manifest, 0));
  }
  return fn + "(" + join(parts, ", ") + ")";
}

std::string print_source_prec(Ref ref, const KernelManifest& manifest,
                              int parent_prec) {
  if (std::string_view(manifest.name) == "sympy") {
    return print_sympy_prec(ref, parent_prec);
  }
  if (ref->tag != Tag::Compound) {
    if (std::string_view(manifest.name) == "python" && ref->tag == Tag::Sym &&
        ref->text == "pi") {
      return "math.pi";
    }
    return sympy_atom(ref);
  }
  if (!ref->attrs.empty()) {
    throw Error(std::string(manifest.name) +
                " source emit does not support attributed expression: " +
                ref->text);
  }
  if (ref->text == "neg" && ref->args.size() == 1) {
    return sympy_wrap("-" + print_source_prec(ref->args[0], manifest, 80),
                      80, parent_prec);
  }
  const KernelMapEntry* table_entry = lookup_kernel_entry(manifest, ref->text);
  if (table_entry && table_entry->kind == MapKind::Infix &&
      ref->args.size() == 2) {
    int prec = table_entry->prec;
    int rhs_prec = ref->text == "^" ? prec : prec + 1;
    int lhs_prec = ref->text == "^" ? prec + 1 : prec;
    std::string out = print_source_prec(ref->args[0], manifest, lhs_prec) +
                      table_entry->target +
                      print_source_prec(ref->args[1], manifest, rhs_prec);
    return sympy_wrap(out, prec, parent_prec);
  }
  if (table_entry && table_entry->kind == MapKind::Relation &&
      ref->args.size() == 2) {
    if (std::string_view(manifest.name) == "python") {
      int prec = 30;
      std::string out = print_source_prec(ref->args[0], manifest, prec) +
                        table_entry->target +
                        print_source_prec(ref->args[1], manifest, prec + 1);
      return sympy_wrap(out, prec, parent_prec);
    }
    return std::string(table_entry->target) + "(" +
           print_source_prec(ref->args[0], manifest, 0) + ", " +
           print_source_prec(ref->args[1], manifest, 0) + ")";
  }
  if (table_entry && table_entry->kind == MapKind::Function) {
    return source_call(table_entry->target, ref, manifest);
  }
  throw Error(std::string(manifest.name) +
              " source emit does not support head: " + ref->text);
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
  const KernelMapEntry* table_entry =
      lookup_kernel_entry(sympy_manifest(), ref->text);
  if (table_entry && table_entry->kind == MapKind::Infix &&
      ref->args.size() == 2) {
    int prec = table_entry->prec;
    int rhs_prec = ref->text == "^" ? prec : prec + 1;
    int lhs_prec = ref->text == "^" ? prec + 1 : prec;
    std::string lhs = print_sympy_prec(ref->args[0], lhs_prec);
    // Python: ** binds more tightly than unary -.
    // Without explicit parens, -a**n means -(a**n), not (-a)**n.
    if (ref->text == "^" && !lhs.empty() && lhs[0] == '-') {
      lhs = "(" + lhs + ")";
    }
    std::string out = lhs + table_entry->target +
                      print_sympy_prec(ref->args[1], rhs_prec);
    return sympy_wrap(out, prec, parent_prec);
  }
  if (table_entry && table_entry->kind == MapKind::Relation &&
      ref->args.size() == 2) {
    return std::string(table_entry->target) + "(" +
           print_sympy_prec(ref->args[0], 0) + ", " +
           print_sympy_prec(ref->args[1], 0) + ")";
  }
  if (ref->text == "int" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    Ref binder = ref->args[0];
    Ref domain = binder->args[1];
    if (domain->tag == Tag::Compound && domain->text == "range" &&
        domain->args.size() == 2) {
      return "integrate(" + print_sympy_prec(ref->args[1], 0) + ", (" +
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
      return "summation(" + print_sympy_prec(ref->args[1], 0) + ", (" +
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
             print_sympy_prec(domain->args[1], 0) + ")).doit()";
    }
  }
  if (ref->text == "lim" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    Ref binder = ref->args[0];
    Ref domain = binder->args[1];
    if (domain->tag == Tag::Compound && domain->text == "approach" &&
        domain->args.size() == 1) {
      return "limit(" + print_sympy_prec(ref->args[1], 0) + ", " +
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
  if (table_entry && table_entry->kind == MapKind::Function &&
      ref->text != "int" && ref->text != "sum" && ref->text != "prod" &&
      ref->text != "lim" && ref->text != "diff" && ref->text != "lam") {
    return sympy_call(table_entry->target, ref);
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
    const std::string& sym_name = value.args[0].text;
    if (sym_name == "oo") return arena.sym("inf");
    return arena.sym(sym_name);
  }
  if (!value.call && value.head == "oo") {
    return arena.sym("inf");
  }
  if (value.head == "Integer" && value.args.size() == 1) {
    return arena.integer(value.args[0].text);
  }
  if (value.head == "Rational" && value.args.size() == 2 &&
      value.args[0].number && value.args[1].number) {
    return arena.compound("/", {arena.integer(value.args[0].text),
                                arena.integer(value.args[1].text)});
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
       value.head == "log" || value.head == "sqrt" || value.head == "exp" ||
       value.head == "erf" || value.head == "erfc" || value.head == "Abs" ||
       value.head == "factorial" || value.head == "gamma") &&
      value.args.size() == 1) {
    std::string head = value.head == "Abs" ? "abs" : value.head;
    return arena.compound(head, {from_srepr(arena, value.args[0])});
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
    "  'integrate': s.integrate, 'summation': s.summation, 'limit': s.limit,\n"
    "  'Integral': s.Integral, 'Sum': s.Sum, 'Product': s.Product,\n"
    "  'Limit': s.Limit, 'Lambda': s.Lambda, 'diff': s.diff,\n"
    "  'sin': s.sin, 'cos': s.cos, 'tan': s.tan, 'log': s.log,\n"
    "  'exp': s.exp, 'sqrt': s.sqrt, 'Abs': s.Abs, 'factor': s.factor,\n"
    "  'simplify': s.simplify, 'expand': s.expand,\n"
    "  'Eq': s.Eq, 'Ne': s.Ne, 'Gt': s.Gt, 'Ge': s.Ge,\n"
    "  'Lt': s.Lt, 'Le': s.Le, 'pi': s.pi, 'oo': s.oo\n"
    "}\n"
    "env = dict(known)\n"
    "import json as _json, os as _os\n"
    "_srepr_env = {k: getattr(s, k) for k in dir(s) if not k.startswith('_')}\n"
    "for _k, _v in _json.loads(_os.environ.get('FACET_SESSION_SREPR', '{}')).items():\n"
    "  env[_k] = eval(_v, {'__builtins__': {}}, _srepr_env)\n"
    "for name in names:\n"
    "  if name not in env:\n"
    "    kwargs = {k: v for d in [assume_map.get(name, {})] for k, v in d.items()}\n"
    "    env[name] = s.Symbol(name, **kwargs)\n"
    "expr = eval(src, {'__builtins__': {}}, env)\n"
    "print(s.srepr(expr))\n";
}

bool coverage_supports_head(const KernelManifest& manifest,
                            const std::string& head) {
  if (lookup_kernel_entry(manifest, head)) {
    return true;
  }
  if (head == "neg" &&
      (std::string_view(manifest.name) == "sympy" ||
       std::string_view(manifest.name) == "python")) {
    return true;
  }
  if (std::string(manifest.name) == "sympy") {
    return head == "binder" || head == "range" || head == "approach";
  }
  return false;
}

void coverage_walk(Ref ref, const KernelManifest& manifest,
                   const std::string& path, Coverage& out) {
  if (!ref || ref->tag != Tag::Compound) {
    return;
  }
  ++out.total;
  if (!ref->attrs.empty()) {
    out.missing.push_back({ref->text + ":attrs", manifest.name, path});
  } else if (coverage_supports_head(manifest, ref->text)) {
    ++out.supported;
  } else {
    out.missing.push_back({ref->text, manifest.name, path});
  }
  for (std::size_t i = 0; i < ref->args.size(); ++i) {
    coverage_walk(ref->args[i], manifest,
                  path + ".args[" + std::to_string(i) + "]", out);
  }
  for (const auto& attr : ref->attrs) {
    coverage_walk(attr.value, manifest, path + ".attrs[" + attr.key + "]",
                  out);
  }
}

void require_sympy_coverage(Ref ref) {
  Coverage cov;
  cov.kernel = sympy_manifest().name;
  coverage_walk(ref, sympy_manifest(), "root", cov);
  if (!cov.missing.empty()) {
    const Unmapped& first = cov.missing.front();
    throw Error("SymPy emit unmapped head at " + first.path + ": " +
                first.head);
  }
}

void require_source_coverage(Ref ref, const KernelManifest& manifest) {
  Coverage cov;
  cov.kernel = manifest.name;
  coverage_walk(ref, manifest, "root", cov);
  if (!cov.missing.empty()) {
    const Unmapped& first = cov.missing.front();
    throw Error(std::string(manifest.name) + " source emit unmapped head at " +
                first.path + ": " + first.head);
  }
}

struct NumericOptions {
  int samples = 100;
  double tol = 1e-9;
};

bool starts_with(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

NumericOptions parse_numeric_options(const std::string& by) {
  NumericOptions options;
  if (by == "numeric") {
    return options;
  }
  std::string_view view(by);
  if (!starts_with(view, "numeric(") || view.back() != ')') {
    throw Error("unknown compare mode: " + by);
  }
  std::string inner(view.substr(8, view.size() - 9));
  std::size_t start = 0;
  while (start <= inner.size()) {
    std::size_t comma = inner.find(',', start);
    std::string item = inner.substr(start, comma == std::string::npos
                                               ? std::string::npos
                                               : comma - start);
    item.erase(std::remove_if(item.begin(), item.end(),
                              [](unsigned char c) { return std::isspace(c); }),
               item.end());
    if (!item.empty()) {
      std::size_t eq = item.find('=');
      if (eq == std::string::npos) {
        throw Error("numeric compare option must use key=value: " + item);
      }
      std::string key = item.substr(0, eq);
      std::string value = item.substr(eq + 1);
      if (key == "samples") {
        options.samples = std::stoi(value);
      } else if (key == "tol") {
        options.tol = std::stod(value);
      } else {
        throw Error("unknown numeric compare option: " + key);
      }
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  if (options.samples <= 0) {
    throw Error("numeric compare samples must be positive");
  }
  if (!(options.tol >= 0.0)) {
    throw Error("numeric compare tol must be non-negative");
  }
  return options;
}

double numeric_atom(Ref ref) {
  if (ref->tag == Tag::Int || ref->tag == Tag::Real) {
    return std::stod(ref->text);
  }
  if (ref->tag == Tag::Rat) {
    std::size_t slash = ref->text.find('/');
    return std::stod(ref->text.substr(0, slash)) /
           std::stod(ref->text.substr(slash + 1));
  }
  throw Error("numeric compare expected numeric atom");
}

void collect_numeric_symbols(Ref ref, std::set<std::string>& out) {
  if (!ref) {
    return;
  }
  if (ref->tag == Tag::Sym) {
    if (ref->text != "pi" && ref->text != "e") {
      out.insert(ref->text);
    }
    return;
  }
  for (Ref arg : ref->args) {
    collect_numeric_symbols(arg, out);
  }
  for (const auto& attr : ref->attrs) {
    collect_numeric_symbols(attr.value, out);
  }
}

double sample_value(int sample, int var_index) {
  int raw = ((sample + 1) * (var_index + 3) * 37) % 2001;
  double value = static_cast<double>(raw) / 1000.0 - 1.0;
  if (std::abs(value) < 1e-12) {
    value = 0.125 * (var_index + 1);
  }
  return value;
}

std::string format_double(double value) {
  std::ostringstream out;
  out.precision(12);
  out << value;
  return out.str();
}

double eval_numeric_compare(Ref ref,
                            const std::unordered_map<std::string, double>& env) {
  if (ref->tag == Tag::Int || ref->tag == Tag::Real || ref->tag == Tag::Rat) {
    return numeric_atom(ref);
  }
  if (ref->tag == Tag::Sym) {
    if (ref->text == "pi") {
      return 3.14159265358979323846;
    }
    if (ref->text == "e") {
      return 2.71828182845904523536;
    }
    auto it = env.find(ref->text);
    if (it == env.end()) {
      throw Error("numeric compare unbound symbol: " + ref->text);
    }
    return it->second;
  }
  if (ref->tag != Tag::Compound || !ref->attrs.empty()) {
    throw Error("numeric compare cannot evaluate: " + print_sympy_prec(ref, 0));
  }
  if (ref->text == "neg" && ref->args.size() == 1) {
    return -eval_numeric_compare(ref->args[0], env);
  }
  if ((ref->text == "+" || ref->text == "-" || ref->text == "*" ||
       ref->text == "/" || ref->text == "^") &&
      ref->args.size() == 2) {
    double lhs = eval_numeric_compare(ref->args[0], env);
    double rhs = eval_numeric_compare(ref->args[1], env);
    if (ref->text == "+") {
      return lhs + rhs;
    }
    if (ref->text == "-") {
      return lhs - rhs;
    }
    if (ref->text == "*") {
      return lhs * rhs;
    }
    if (ref->text == "/") {
      return lhs / rhs;
    }
    return std::pow(lhs, rhs);
  }
  if (ref->args.size() == 1) {
    double arg = eval_numeric_compare(ref->args[0], env);
    if (ref->text == "sin") {
      return std::sin(arg);
    }
    if (ref->text == "cos") {
      return std::cos(arg);
    }
    if (ref->text == "tan") {
      return std::tan(arg);
    }
    if (ref->text == "log") {
      return std::log(arg);
    }
    if (ref->text == "sqrt") {
      return std::sqrt(arg);
    }
    if (ref->text == "exp") {
      return std::exp(arg);
    }
  }
  throw Error("numeric compare cannot evaluate head: " + ref->text);
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
  require_sympy_coverage(ref);
  return print_sympy_prec(ref, 0);
}

std::string print_source(Ref ref, const std::string& kernel) {
  const KernelManifest& manifest = kernel_manifest(kernel);
  require_source_coverage(ref, manifest);
  return print_source_prec(ref, manifest, 0);
}

Coverage coverage(Ref ref, const std::string& kernel) {
  const KernelManifest& manifest = kernel_manifest(kernel);
  Coverage out;
  out.kernel = manifest.name;
  coverage_walk(ref, manifest, "root", out);
  return out;
}

CompareResult compare(Arena& arena, Ref lhs, Ref rhs, const std::string& by) {
  CompareResult result;
  result.by = by;
  if (by == "structural") {
    result.agreement = same_tree(lhs, rhs);
    result.status = result.agreement ? "Ok" : "Fail";
    result.strength = "intrinsic";
    result.detail = result.agreement ? "same_tree" : "different_tree";
    return result;
  }
  if (by == "simplify") {
    if (same_tree(lhs, rhs)) {
      result.agreement = true;
      result.status = "Ok";
      result.strength = "transformer";
      result.detail = "same_tree_precheck";
      return result;
    }
    try {
      Ref lhs_eval = evaluate_sympy(arena, lhs);
      Ref rhs_eval = evaluate_sympy(arena, rhs);
      result.agreement = same_tree(lhs_eval, rhs_eval);
      result.status = result.agreement ? "Ok" : "Fail";
      result.strength = "transformer";
      result.detail = result.agreement ? "sympy_same_tree" : "sympy_diff_tree";
    } catch (const Error& error) {
      std::string message = error.what();
      if (message.rfind("SymPy subprocess failed:", 0) != 0) {
        throw;
      }
      result.agreement = false;
      result.status = "Unknown";
      result.strength = "transformer";
      result.detail = "sympy_unavailable";
    }
    return result;
  }
  if (by == "numeric" || starts_with(by, "numeric(")) {
    NumericOptions options = parse_numeric_options(by);
    result.by = "numeric";
    result.strength = "evidence";
    result.samples = options.samples;
    result.tol = options.tol;
    std::set<std::string> symbols;
    collect_numeric_symbols(lhs, symbols);
    collect_numeric_symbols(rhs, symbols);
    std::vector<std::string> vars(symbols.begin(), symbols.end());
    int finite_samples = 0;
    try {
      for (int sample = 0; sample < options.samples; ++sample) {
        std::unordered_map<std::string, double> env;
        for (std::size_t i = 0; i < vars.size(); ++i) {
          env.emplace(vars[i], sample_value(sample, static_cast<int>(i)));
        }
        double lhs_value = eval_numeric_compare(lhs, env);
        double rhs_value = eval_numeric_compare(rhs, env);
        if (!std::isfinite(lhs_value) || !std::isfinite(rhs_value)) {
          continue;
        }
        ++finite_samples;
        if (std::abs(lhs_value - rhs_value) > options.tol) {
          std::ostringstream witness;
          witness << "sample=" << sample;
          for (const auto& var : vars) {
            witness << "," << var << "=" << format_double(env[var]);
          }
          witness << ",lhs=" << format_double(lhs_value)
                  << ",rhs=" << format_double(rhs_value);
          result.agreement = false;
          result.status = "Fail";
          result.detail = "numeric_witness";
          result.witness = witness.str();
          return result;
        }
      }
    } catch (const Error& error) {
      result.agreement = false;
      result.status = "Unknown";
      result.detail = error.what();
      return result;
    }
    if (finite_samples == 0) {
      result.agreement = false;
      result.status = "Unknown";
      result.detail = "numeric_no_finite_samples";
      return result;
    }
    result.agreement = true;
    result.status = "Ok";
    result.detail = "numeric_samples";
    return result;
  }
  throw Error("unknown compare mode: " + by);
}

} // namespace facet
