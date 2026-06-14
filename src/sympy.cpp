#include "facet_internal.hpp"

#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace facet {
namespace {

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
  static const std::vector<std::pair<std::string, std::string>> names = {
      {"sin", "sin"},   {"cos", "cos"},   {"tan", "tan"},
      {"log", "log"},   {"sqrt", "sqrt"}, {"simplify", "simplify"},
      {"expand", "expand"}};
  for (const auto& name : names) {
    if (head == name.first) {
      return name.second;
    }
  }
  return "";
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
    return value.args[0].number ? arena.integer(value.args[0].text)
                                : arena.integer(value.args[0].text);
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
    if (value.args.size() == 2 && is_integer_literal(value.args[0], "-1")) {
      return arena.compound("neg", {from_srepr(arena, value.args[1])});
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
  if ((value.head == "Integral" || value.head == "Sum") &&
      value.args.size() == 2 && value.args[1].call &&
      value.args[1].head == "Tuple" && value.args[1].args.size() == 3) {
    Ref binder =
        arena.compound("binder",
                       {tuple_at(arena, value.args[1], 0),
                        arena.compound("range",
                                       {tuple_at(arena, value.args[1], 1),
                                        tuple_at(arena, value.args[1], 2)})});
    return arena.compound(value.head == "Integral" ? "int" : "sum",
                          {binder, from_srepr(arena, value.args[0])});
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

} // namespace

Ref read_sympy_srepr(Arena& arena, const std::string& input) {
  return from_srepr(arena, SreprParser(input).parse());
}

std::string print_sympy(Ref ref) {
  return print_sympy_prec(ref, 0);
}

} // namespace facet
