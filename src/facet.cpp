#include "facet/facet.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace facet {
namespace {

void hash_mix(std::uint64_t& h, std::uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
}

std::uint64_t hash_string(const std::string& s) {
  std::uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  return h;
}

std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

struct Tok {
  std::string text;
  bool eof = false;
};

class Lexer {
public:
  explicit Lexer(std::string input) : input_(std::move(input)) { next(); }

  const Tok& peek() const { return tok_; }

  bool at(const std::string& text) const { return tok_.text == text; }

  bool take(const std::string& text) {
    if (!at(text)) {
      return false;
    }
    next();
    return true;
  }

  std::string expect() {
    if (tok_.eof) {
      throw Error("unexpected end of input");
    }
    std::string text = tok_.text;
    next();
    return text;
  }

  void expect(const std::string& text) {
    if (!take(text)) {
      throw Error("expected '" + text + "', got '" + tok_.text + "'");
    }
  }

private:
  std::string input_;
  std::size_t pos_ = 0;
  Tok tok_;

  void next() {
    while (pos_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[pos_]))) {
      ++pos_;
    }
    if (pos_ >= input_.size()) {
      tok_ = {"", true};
      return;
    }

    char c = input_[pos_];
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '?' ||
        c == '\\') {
      std::size_t start = pos_++;
      while (pos_ < input_.size()) {
        char d = input_[pos_];
        if (!std::isalnum(static_cast<unsigned char>(d)) && d != '_' &&
            d != '?' && d != '\\') {
          break;
        }
        ++pos_;
      }
      if (input_.compare(pos_, 3, "...") == 0) {
        pos_ += 3;
      }
      tok_ = {input_.substr(start, pos_ - start), false};
      return;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::size_t start = pos_++;
      while (pos_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
      }
      tok_ = {input_.substr(start, pos_ - start), false};
      return;
    }
    if (c == '"') {
      ++pos_;
      std::string s;
      while (pos_ < input_.size() && input_[pos_] != '"') {
        if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
          ++pos_;
        }
        s.push_back(input_[pos_++]);
      }
      if (pos_ >= input_.size()) {
        throw Error("unterminated string");
      }
      ++pos_;
      tok_ = {'"' + s + '"', false};
      return;
    }

    static const std::vector<std::string> ops = {
        "===", "...", ":=", "~>", "~=", "=>", "|->", "|>", "->",
        "..",  ">=",  "<=", "!=", ".("};
    for (const auto& op : ops) {
      if (input_.compare(pos_, op.size(), op) == 0) {
        pos_ += op.size();
        tok_ = {op, false};
        return;
      }
    }
    tok_ = {std::string(1, input_[pos_++]), false};
  }
};

bool is_int_token(const std::string& s) {
  return !s.empty() &&
         std::all_of(s.begin(), s.end(),
                     [](unsigned char c) { return std::isdigit(c); });
}

bool is_string_token(const std::string& s) {
  return s.size() >= 2 && s.front() == '"' && s.back() == '"';
}

Ref attr_value(Ref ref, const std::string& key) {
  for (const auto& attr : ref->attrs) {
    if (attr.key == key) {
      return attr.value;
    }
  }
  return nullptr;
}

std::string key_of(const Node& node) {
  std::ostringstream out;
  out << static_cast<int>(node.tag) << ':' << node.text << '(';
  for (Ref arg : node.args) {
    out << arg->hash << ',';
  }
  out << ')';
  for (const auto& attr : node.attrs) {
    out << ':' << attr.key << '=' << attr.value->hash;
  }
  return out.str();
}

int prec_of(const std::string& op) {
  if (op == "@" || op == "|>") {
    return 10;
  }
  if (op == "=>" || op == "~>" || op == "~=") {
    return 20;
  }
  if (op == "=" || op == "===" || op == "!=" || op == ">" || op == "<" ||
      op == ">=" || op == "<=") {
    return 30;
  }
  if (op == "..") {
    return 35;
  }
  if (op == "|->") {
    return 40;
  }
  if (op == "+" || op == "-") {
    return 50;
  }
  if (op == "*" || op == "/") {
    return 60;
  }
  if (op == "^") {
    return 70;
  }
  return -1;
}

bool right_assoc(const std::string& op) { return op == "^" || op == "|->"; }

class CoreParser {
public:
  CoreParser(Arena& arena, std::string input)
      : arena_(arena), lex_(std::move(input)) {}

  Ref parse() {
    Ref ref = expr();
    if (!lex_.peek().eof) {
      throw Error("trailing input after core expression");
    }
    return ref;
  }

private:
  Arena& arena_;
  Lexer lex_;

  Ref expr() {
    if (lex_.take("(")) {
      std::string h = lex_.expect();
      std::vector<Ref> args;
      std::vector<Attr> attrs;
      while (!lex_.take(")")) {
        if (lex_.take(":")) {
          std::string key = lex_.expect();
          attrs.push_back({key, expr()});
        } else {
          args.push_back(expr());
        }
      }
      return arena_.compound(h, std::move(args), std::move(attrs));
    }
    std::string t = lex_.expect();
    if (is_int_token(t)) {
      return arena_.integer(t);
    }
    if (is_string_token(t)) {
      return arena_.string(t.substr(1, t.size() - 2));
    }
    return arena_.sym(t);
  }
};

class StrictParser {
public:
  StrictParser(Arena& arena, std::string input)
      : arena_(arena), input_(std::move(input)) {}

  Ref parse() {
    Ref ref = expr();
    ws();
    if (pos_ != input_.size()) {
      throw Error("trailing input after strict expression");
    }
    return ref;
  }

private:
  Arena& arena_;
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
      throw Error(std::string("expected '") + c + "'");
    }
  }

  std::string atom() {
    ws();
    if (pos_ >= input_.size()) {
      throw Error("unexpected end of strict input");
    }
    if (input_[pos_] == '"') {
      std::size_t start = pos_++;
      while (pos_ < input_.size() && input_[pos_] != '"') {
        if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
          ++pos_;
        }
        ++pos_;
      }
      if (pos_ >= input_.size()) {
        throw Error("unterminated string");
      }
      ++pos_;
      return input_.substr(start, pos_ - start);
    }
    static const std::vector<std::string> op_heads = {
        "===", "...", ":=", "~>", "~=", "=>", "|->", "|>", "->",
        "..",  ">=",  "<=", "!=", "+",  "-",  "*",  "/",  "^",
        "=",   ">",   "<",  "@",  ":"};
    for (const auto& op : op_heads) {
      if (input_.compare(pos_, op.size(), op) == 0) {
        pos_ += op.size();
        return op;
      }
    }
    std::size_t start = pos_++;
    while (pos_ < input_.size()) {
      char c = input_[pos_];
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' &&
          c != '?' && c != '\\') {
        break;
      }
      ++pos_;
    }
    return input_.substr(start, pos_ - start);
  }

  bool looks_like_attr() {
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

  Ref expr() {
    std::string t = atom();
    if (is_int_token(t)) {
      return arena_.integer(t);
    }
    if (is_string_token(t)) {
      return arena_.string(t.substr(1, t.size() - 2));
    }
    if (take('(')) {
      std::vector<Ref> args;
      std::vector<Attr> attrs;
      if (!take(')')) {
        do {
          if (looks_like_attr()) {
            std::string key = atom();
            expect('=');
            attrs.push_back({key, expr()});
          } else {
            args.push_back(expr());
          }
        } while (take(','));
        expect(')');
      }
      return arena_.compound(t, std::move(args), std::move(attrs));
    }
    return arena_.sym(t);
  }
};

class SurfaceParser {
public:
  SurfaceParser(Arena& arena, std::string input)
      : arena_(arena), lex_(std::move(input)) {}

  Ref parse() {
    Ref ref = expr(0);
    if (!lex_.peek().eof) {
      throw Error("trailing input after surface expression near '" +
                  lex_.peek().text + "'");
    }
    return ref;
  }

private:
  Arena& arena_;
  Lexer lex_;

  Ref expr(int min_prec) {
    Ref lhs = primary();
    while (!lex_.peek().eof) {
      std::string op = lex_.peek().text;
      int prec = prec_of(op);
      if (prec < min_prec) {
        break;
      }
      lex_.expect();
      Ref rhs = expr(prec + (right_assoc(op) ? 0 : 1));
      if (op == "@") {
        lhs = attach_context(lhs, rhs);
      } else if (op == "..") {
        lhs = arena_.compound("range", {lhs, rhs});
      } else {
        lhs = arena_.compound(op, {lhs, rhs});
      }
    }
    return lhs;
  }

  Ref primary() {
    if (lex_.take("(")) {
      Ref inner = expr(0);
      lex_.expect(")");
      return inner;
    }
    if (lex_.take("{")) {
      Ref value = expr(0);
      if (lex_.take("|")) {
        Ref b = binder_tail();
        std::vector<Attr> attrs;
        if (lex_.take(",")) {
          attrs.push_back({"when", expr(0)});
        }
        lex_.expect("}");
        return arena_.compound("setbuild", {value, b}, attrs);
      }
      std::vector<Ref> items{value};
      while (lex_.take(",")) {
        items.push_back(expr(0));
      }
      lex_.expect("}");
      return arena_.compound("set", items);
    }
    std::string t = lex_.expect();
    if (is_int_token(t)) {
      return arena_.integer(t);
    }
    if (is_string_token(t)) {
      return arena_.string(t.substr(1, t.size() - 2));
    }
    if (lex_.take(".(")) {
      std::vector<Ref> args = args_until(")");
      return arena_.compound("broadcast", {arena_.sym(t), arena_.compound("args", args)});
    }
    if (lex_.take("[")) {
      if (lex_.at("]")) {
        throw Error("empty binder");
      }
      std::string name = lex_.expect();
      std::string sep = lex_.expect();
      if (sep != ":" && sep != "->") {
        throw Error("binder must use ':' or '->'");
      }
      Ref domain = expr(0);
      lex_.expect("]");
      lex_.expect("(");
      Ref body = expr(0);
      lex_.expect(")");
      Ref binder =
          arena_.compound("binder", {arena_.sym(name), sep == "->" ? arena_.compound("approach", {domain}) : domain});
      return arena_.compound(t, {binder, body});
    }
    if (lex_.take("(")) {
      std::vector<Ref> args;
      std::vector<Attr> attrs;
      if (!lex_.take(")")) {
        do {
          args.push_back(expr(0));
        } while (lex_.take(","));
        lex_.expect(")");
      }
      return arena_.compound(t, args, attrs);
    }
    if (lex_.take("{")) {
      std::vector<Ref> args;
      if (!lex_.take("}")) {
        do {
          args.push_back(expr(0));
        } while (lex_.take(","));
        lex_.expect("}");
      }
      return arena_.compound(t, args);
    }
    return arena_.sym(t);
  }

  Ref binder_tail() {
    std::string name = lex_.expect();
    lex_.expect(":");
    Ref domain = expr(0);
    return arena_.compound("binder", {arena_.sym(name), domain});
  }

  std::vector<Ref> args_until(const std::string& close) {
    std::vector<Ref> args;
    if (!lex_.take(close)) {
      do {
        args.push_back(expr(0));
      } while (lex_.take(","));
      lex_.expect(close);
    }
    return args;
  }

  Ref attach_context(Ref target, Ref context) {
    if (context->tag == Tag::Compound && context->text == "subst") {
      std::vector<Ref> args{target};
      args.insert(args.end(), context->args.begin(), context->args.end());
      return arena_.compound("subst", args);
    }
    if (context->tag == Tag::Compound &&
        (context->text == "assume" || context->text == "via" ||
         context->text == "need") &&
        context->args.size() == 1 && context->attrs.empty()) {
      std::vector<Attr> attrs = target->attrs;
      attrs.push_back({context->text, context->args[0]});
      return arena_.compound(target->text, target->args, attrs);
    }
    return arena_.compound("@", {target, context});
  }
};

std::string print_surface_prec(Ref ref, int parent_prec);

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) {
      out += sep;
    }
    out += parts[i];
  }
  return out;
}

std::vector<std::string> printed_args(Ref ref,
                                      std::string (*printer)(Ref)) {
  std::vector<std::string> parts;
  for (Ref arg : ref->args) {
    parts.push_back(printer(arg));
  }
  return parts;
}

std::string print_atom(Ref ref) {
  if (ref->tag == Tag::Str) {
    return "\"" + escape(ref->text) + "\"";
  }
  return ref->text;
}

std::string print_core_inner(Ref ref) {
  if (ref->tag != Tag::Compound) {
    return print_atom(ref);
  }
  std::string out = "(" + ref->text;
  for (Ref arg : ref->args) {
    out += " " + print_core_inner(arg);
  }
  for (const auto& attr : ref->attrs) {
    out += " :" + attr.key + " " + print_core_inner(attr.value);
  }
  out += ")";
  return out;
}

std::string print_strict_inner(Ref ref) {
  if (ref->tag != Tag::Compound) {
    return print_atom(ref);
  }
  std::string out = ref->text + "(";
  bool first = true;
  for (Ref arg : ref->args) {
    if (!first) {
      out += ", ";
    }
    first = false;
    out += print_strict_inner(arg);
  }
  for (const auto& attr : ref->attrs) {
    if (!first) {
      out += ", ";
    }
    first = false;
    out += attr.key + "=" + print_strict_inner(attr.value);
  }
  out += ")";
  return out;
}

std::string binder_surface(Ref binder) {
  if (binder->tag != Tag::Compound || binder->text != "binder" ||
      binder->args.size() != 2) {
    throw Error("expected binder node");
  }
  Ref domain = binder->args[1];
  if (domain->tag == Tag::Compound && domain->text == "approach" &&
      domain->args.size() == 1) {
    return print_surface_prec(binder->args[0], 0) + " -> " +
           print_surface_prec(domain->args[0], 0);
  }
  return print_surface_prec(binder->args[0], 0) + " : " +
         print_surface_prec(domain, 0);
}

std::string print_surface_prec(Ref ref, int parent_prec) {
  if (ref->tag != Tag::Compound) {
    return print_atom(ref);
  }
  if ((ref->text == "sum" || ref->text == "int" || ref->text == "prod" ||
       ref->text == "lim" || ref->text == "forall" || ref->text == "exists" ||
       ref->text == "solve") &&
      ref->args.size() == 2 && ref->args[0]->tag == Tag::Compound &&
      ref->args[0]->text == "binder") {
    return ref->text + "[" + binder_surface(ref->args[0]) + "](" +
           print_surface_prec(ref->args[1], 0) + ")";
  }
  if (ref->text == "setbuild" && ref->args.size() == 2) {
    std::string out = "{ " + print_surface_prec(ref->args[0], 0) + " | " +
                      binder_surface(ref->args[1]);
    if (Ref when = attr_value(ref, "when")) {
      out += ", " + print_surface_prec(when, 0);
    }
    return out + " }";
  }
  if (ref->text == "range" && ref->args.size() == 2) {
    std::string out = print_surface_prec(ref->args[0], 35) + ".." +
                      print_surface_prec(ref->args[1], 36);
    if (35 < parent_prec) {
      return "(" + out + ")";
    }
    return out;
  }
  int prec = prec_of(ref->text);
  if (prec >= 0 && ref->args.size() == 2) {
    std::string out = print_surface_prec(ref->args[0], prec) + " " +
                      ref->text + " " +
                      print_surface_prec(ref->args[1], prec + 1);
    if (prec < parent_prec) {
      return "(" + out + ")";
    }
    return out;
  }
  if (ref->text == "subst" && !ref->args.empty()) {
    std::vector<std::string> subs;
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      subs.push_back(print_surface_prec(ref->args[i], 0));
    }
    return print_surface_prec(ref->args[0], 0) + " @ subst{" +
           join(subs, ", ") + "}";
  }
  std::vector<std::string> parts;
  for (Ref arg : ref->args) {
    parts.push_back(print_surface_prec(arg, 0));
  }
  std::string out = ref->text + "(" + join(parts, ", ") + ")";
  for (const auto& attr : ref->attrs) {
    out += " @ " + attr.key + "(" + print_surface_prec(attr.value, 0) + ")";
  }
  return out;
}

std::string print_latex_inner(Ref ref) {
  if (ref->tag != Tag::Compound) {
    if (ref->text == "pi") {
      return "\\pi";
    }
    return ref->text;
  }
  if (ref->text == "^" && ref->args.size() == 2) {
    return print_latex_inner(ref->args[0]) + "^{" +
           print_latex_inner(ref->args[1]) + "}";
  }
  if (ref->text == "*" && ref->args.size() == 2) {
    return print_latex_inner(ref->args[0]) + " " + print_latex_inner(ref->args[1]);
  }
  if (ref->text == "/" && ref->args.size() == 2) {
    return "\\frac{" + print_latex_inner(ref->args[0]) + "}{" +
           print_latex_inner(ref->args[1]) + "}";
  }
  if ((ref->text == "+" || ref->text == "=" || ref->text == ">=" ||
       ref->text == "<=") &&
      ref->args.size() == 2) {
    return print_latex_inner(ref->args[0]) + " " + ref->text + " " +
           print_latex_inner(ref->args[1]);
  }
  if (ref->text == "sin" && ref->args.size() == 1) {
    return "\\sin(" + print_latex_inner(ref->args[0]) + ")";
  }
  if (ref->text == "int" && ref->args.size() == 2 &&
      ref->args[0]->args.size() == 2) {
    Ref b = ref->args[0];
    Ref dom = b->args[1];
    if (dom->tag == Tag::Compound && dom->text == "range" &&
        dom->args.size() == 2) {
      return "\\int_{" + print_latex_inner(dom->args[0]) + "}^{" +
             print_latex_inner(dom->args[1]) + "} " +
             print_latex_inner(ref->args[1]) + "\\,d" +
             print_latex_inner(b->args[0]);
    }
  }
  return "\\" + ref->text + "(" +
         join(printed_args(ref, print_latex_inner), ", ") + ")";
}

class JsonParser {
public:
  JsonParser(Arena& arena, std::string input)
      : arena_(arena), input_(std::move(input)) {}

  Ref parse() {
    Ref ref = value();
    ws();
    if (pos_ != input_.size()) {
      throw Error("trailing input after object expression");
    }
    return ref;
  }

private:
  Arena& arena_;
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
      throw Error(std::string("expected JSON '") + c + "'");
    }
  }

  std::string string() {
    ws();
    expect('"');
    std::string out;
    while (pos_ < input_.size() && input_[pos_] != '"') {
      if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
        ++pos_;
      }
      out.push_back(input_[pos_++]);
    }
    expect('"');
    return out;
  }

  Ref value() {
    ws();
    if (pos_ >= input_.size()) {
      throw Error("unexpected end of JSON");
    }
    if (input_[pos_] == '"') {
      return arena_.sym(string());
    }
    if (std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
      std::size_t start = pos_++;
      while (pos_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
      }
      return arena_.integer(input_.substr(start, pos_ - start));
    }
    if (input_[pos_] == '{') {
      return object();
    }
    throw Error("unsupported JSON value");
  }

  Ref object() {
    expect('{');
    std::string h;
    std::vector<Ref> args;
    std::vector<Attr> attrs;
    while (!take('}')) {
      std::string key = string();
      expect(':');
      if (key == "head") {
        h = string();
      } else if (key == "args") {
        expect('[');
        if (!take(']')) {
          do {
            args.push_back(value());
          } while (take(','));
          expect(']');
        }
      } else if (key == "attrs") {
        expect('{');
        while (!take('}')) {
          std::string attr_key = string();
          expect(':');
          attrs.push_back({attr_key, value()});
          take(',');
        }
      } else {
        throw Error("unknown object key: " + key);
      }
      take(',');
    }
    if (h.empty()) {
      throw Error("object node missing head");
    }
    return arena_.compound(h, std::move(args), std::move(attrs));
  }
};

std::string object_inner(Ref ref) {
  if (ref->tag == Tag::Int) {
    return ref->text;
  }
  if (ref->tag == Tag::Sym) {
    return "\"" + escape(ref->text) + "\"";
  }
  if (ref->tag == Tag::Str) {
    return "{\"head\":\"str\",\"args\":[\"" + escape(ref->text) + "\"]}";
  }
  std::string out = "{\"head\":\"" + escape(ref->text) + "\",\"args\":[";
  for (std::size_t i = 0; i < ref->args.size(); ++i) {
    if (i) {
      out += ",";
    }
    out += object_inner(ref->args[i]);
  }
  out += "]";
  if (!ref->attrs.empty()) {
    out += ",\"attrs\":{";
    for (std::size_t i = 0; i < ref->attrs.size(); ++i) {
      if (i) {
        out += ",";
      }
      out += "\"" + escape(ref->attrs[i].key) + "\":" +
             object_inner(ref->attrs[i].value);
    }
    out += "}";
  }
  out += "}";
  return out;
}

} // namespace

Error::Error(const std::string& message) : std::runtime_error(message) {}

Ref Arena::sym(std::string name) {
  Node node;
  node.tag = Tag::Sym;
  node.text = std::move(name);
  return intern(std::move(node));
}

Ref Arena::integer(std::string value) {
  Node node;
  node.tag = Tag::Int;
  node.text = std::move(value);
  return intern(std::move(node));
}

Ref Arena::string(std::string value) {
  Node node;
  node.tag = Tag::Str;
  node.text = std::move(value);
  return intern(std::move(node));
}

Ref Arena::compound(std::string h, std::vector<Ref> args,
                    std::vector<Attr> attrs) {
  std::sort(attrs.begin(), attrs.end(),
            [](const Attr& a, const Attr& b) { return a.key < b.key; });
  Node node;
  node.tag = Tag::Compound;
  node.text = std::move(h);
  node.args = std::move(args);
  node.attrs = std::move(attrs);
  return intern(std::move(node));
}

Ref Arena::intern(Node node) {
  std::uint64_t h = hash_string(node.text);
  hash_mix(h, static_cast<std::uint64_t>(node.tag));
  for (Ref arg : node.args) {
    hash_mix(h, arg->hash);
  }
  for (const auto& attr : node.attrs) {
    hash_mix(h, hash_string(attr.key));
    hash_mix(h, attr.value->hash);
  }
  node.hash = h;
  std::string key = key_of(node);
  for (const auto& entry : index_) {
    if (entry.first == key) {
      return entry.second;
    }
  }
  nodes_.push_back(std::make_unique<Node>(std::move(node)));
  Ref ref = nodes_.back().get();
  index_.push_back({key, ref});
  return ref;
}

Ref read_core(Arena& arena, const std::string& input) {
  return CoreParser(arena, input).parse();
}

std::string print_core(Ref ref) { return print_core_inner(ref); }

Ref read_strict(Arena& arena, const std::string& input) {
  return StrictParser(arena, input).parse();
}

std::string print_strict(Ref ref) { return print_strict_inner(ref); }

Ref read_surface(Arena& arena, const std::string& input) {
  return SurfaceParser(arena, input).parse();
}

std::string print_surface(Ref ref) { return print_surface_prec(ref, 0); }

Ref read_object(Arena& arena, const std::string& input) {
  return JsonParser(arena, input).parse();
}

std::string print_object(Ref ref) { return object_inner(ref); }

std::string print_latex(Ref ref) { return print_latex_inner(ref); }

bool same_tree(Ref lhs, Ref rhs) { return lhs == rhs || lhs->hash == rhs->hash; }

} // namespace facet
