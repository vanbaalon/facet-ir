#include "facet_internal.hpp"

#include <cctype>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace facet {
namespace {

using internal::Lexer;
using internal::attr_value;
using internal::atom_from_token;
using internal::escape;
using internal::is_binder_head;
using internal::is_known_nonindexed_function;
using internal::join;
using internal::lookup_op;
using internal::prec_of;
using internal::print_atom;
using internal::right_assoc;
using internal::surface_atom_from_token;

class CoreParser {
public:
  CoreParser(Arena& arena, std::string input)
      : arena_(arena), lex_(std::move(input)) {}

  Ref parse() {
    Ref ref = expr();
    if (!lex_.peek().eof) {
      throw Error("trailing input after core expression near line " +
                  std::to_string(lex_.peek().line) + ", column " +
                  std::to_string(lex_.peek().column));
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
    return atom_from_token(arena_, lex_.expect());
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
      throw Error("trailing input after strict expression at " + location(pos_));
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
      throw Error(std::string("expected '") + c +
                  "' in strict expression at " + location(pos_));
    }
  }

  std::string location(std::size_t pos) const {
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < pos && i < input_.size(); ++i) {
      if (input_[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
    return "line " + std::to_string(line) + ", column " +
           std::to_string(column);
  }

  std::string atom() {
    ws();
    if (pos_ >= input_.size()) {
      throw Error("unexpected end of strict input at " + location(pos_));
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
        throw Error("unterminated string in strict expression at " +
                    location(start));
      }
      ++pos_;
      return input_.substr(start, pos_ - start);
    }
    if (std::isdigit(static_cast<unsigned char>(input_[pos_])) ||
        (input_[pos_] == '-' && pos_ + 1 < input_.size() &&
         std::isdigit(static_cast<unsigned char>(input_[pos_ + 1])))) {
      std::size_t start = pos_++;
      while (pos_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
      }
      if (pos_ < input_.size() && input_[pos_] == '.' &&
          pos_ + 1 < input_.size() &&
          std::isdigit(static_cast<unsigned char>(input_[pos_ + 1]))) {
        ++pos_;
        while (pos_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
          ++pos_;
        }
      }
      return input_.substr(start, pos_ - start);
    }
    static const std::vector<std::string> op_heads = {
        "===", "...?", "...", ":=", "~>", "~=", "=>", "|->", "|>", "->",
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
    return atom_from_token(arena_, t);
  }
};

class SurfaceParser {
public:
  SurfaceParser(Arena& arena, std::string input)
      : arena_(arena), lex_(std::move(input)) {}

  Ref parse() {
    if (lex_.at("rule")) {
      return rule_decl();
    }
    if (lex_.at("goal")) {
      return labelled_decl("goal");
    }
    Ref ref = expr(0);
    if (!lex_.peek().eof) {
      throw Error("trailing input after surface expression near line " +
                  std::to_string(lex_.peek().line) + ", column " +
                  std::to_string(lex_.peek().column));
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
      } else if (op == "|->") {
        lhs = arena_.compound("lam",
                              {arena_.compound("binder", {lhs, arena_.sym("_")}),
                               rhs});
      } else {
        lhs = arena_.compound(op, {lhs, rhs});
      }
    }
    return lhs;
  }

  Ref primary() {
    if (lex_.take("-")) {
      Ref value = primary();
      if (value->tag == Tag::Int || value->tag == Tag::Real) {
        return value->tag == Tag::Int ? arena_.integer("-" + value->text)
                                      : arena_.real("-" + value->text);
      }
      return arena_.compound("neg", {value});
    }
    Ref value = atom_or_group();
    while (lex_.take("[")) {
      value = bracket_postfix(value);
    }
    return value;
  }

  Ref atom_or_group() {
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
    if (lex_.take(".(")) {
      std::vector<Ref> args = args_until(")");
      return arena_.compound("broadcast",
                             {arena_.sym(t), arena_.compound("args", args)});
    }
    if (lex_.take("(")) {
      std::vector<Ref> args;
      if (!lex_.take(")")) {
        do {
          args.push_back(expr(0));
        } while (lex_.take(","));
        lex_.expect(")");
      }
      return arena_.compound(t, args);
    }
    if (lex_.take("{")) {
      if (t == "dict") {
        return dict_literal();
      }
      if (t == "set" || t == "seq") {
        return collection_literal(t);
      }
      std::vector<Ref> args;
      if (!lex_.take("}")) {
        do {
          args.push_back(expr(0));
        } while (lex_.take(","));
        lex_.expect("}");
      }
      return arena_.compound(t, args);
    }
    return surface_atom_from_token(arena_, t);
  }

  Ref dict_literal() {
    std::vector<Ref> pairs;
    if (!lex_.take("}")) {
      do {
        Ref key = expr(0);
        lex_.expect(":");
        Ref value = expr(0);
        if (lex_.at(":")) {
          throw Error("nested dict ascription must be parenthesized at line " +
                      std::to_string(lex_.peek().line) + ", column " +
                      std::to_string(lex_.peek().column));
        }
        pairs.push_back(arena_.compound("pair", {key, value}));
      } while (lex_.take(","));
      lex_.expect("}");
    }
    return arena_.compound("dict", pairs);
  }

  Ref collection_literal(const std::string& head) {
    if (lex_.take("}")) {
      return arena_.compound(head);
    }
    Ref first = expr(0);
    if (head == "set" && lex_.take("|")) {
      Ref b = binder_tail();
      std::vector<Attr> attrs;
      if (lex_.take(",")) {
        attrs.push_back({"when", expr(0)});
      }
      lex_.expect("}");
      return arena_.compound("setbuild", {first, b}, attrs);
    }
    std::vector<Ref> args{first};
    while (lex_.take(",")) {
      args.push_back(expr(0));
    }
    lex_.expect("}");
    return arena_.compound(head, args);
  }

  Ref bracket_postfix(Ref target) {
    if (target->tag == Tag::Sym && is_binder_head(target->text)) {
      if (target->text == "diff") {
        std::vector<Ref> vars;
        if (!lex_.take("]")) {
          do {
            vars.push_back(surface_atom_from_token(arena_, lex_.expect()));
          } while (lex_.take(","));
          lex_.expect("]");
        }
        lex_.expect("(");
        Ref body = expr(0);
        lex_.expect(")");
        std::vector<Ref> args{body};
        args.insert(args.end(), vars.begin(), vars.end());
        return arena_.compound("diff", args);
      }
      if (lex_.at("]")) {
        throw Error("empty binder at line " + std::to_string(lex_.peek().line) +
                    ", column " + std::to_string(lex_.peek().column));
      }
      std::string name = lex_.expect();
      std::string sep = lex_.expect();
      if (sep != ":" && sep != "->") {
        throw Error("binder must use ':' or '->' at line " +
                    std::to_string(lex_.peek().line) + ", column " +
                    std::to_string(lex_.peek().column));
      }
      Ref domain = expr(0);
      lex_.expect("]");
      lex_.expect("(");
      Ref body = expr(0);
      lex_.expect(")");
      Ref binder = arena_.compound(
          "binder",
          {surface_atom_from_token(arena_, name),
           sep == "->" ? arena_.compound("approach", {domain}) : domain});
      return arena_.compound(target->text, {binder, body});
    }

    std::vector<Ref> args = bracket_args(target);
    lex_.expect("]");
    bool slicing = false;
    for (Ref arg : args) {
      slicing = slicing ||
                (arg->tag == Tag::Compound && arg->text == "range");
    }
    std::vector<Ref> out_args{target};
    out_args.insert(out_args.end(), args.begin(), args.end());
    return arena_.compound(slicing ? "slice" : "at", out_args);
  }

  std::vector<Ref> bracket_args(Ref target) {
    std::vector<Ref> args;
    if (lex_.at("]")) {
      return args;
    }
    std::size_t axis = 1;
    while (true) {
      Ref arg = rewrite_end(expr(0), target, axis);
      if (arg->tag == Tag::Compound && arg->text == "range" &&
          lex_.take(",")) {
        if (lex_.at("step")) {
          lex_.expect("step");
          lex_.expect("=");
          Ref step = expr(0);
          arg = arena_.compound("range", arg->args, {{"step", step}});
          args.push_back(arg);
          ++axis;
          if (!lex_.take(",")) {
            break;
          }
          continue;
        }
        args.push_back(arg);
        ++axis;
        continue;
      }
      args.push_back(arg);
      ++axis;
      if (!lex_.take(",")) {
        break;
      }
    }
    return args;
  }

  Ref rewrite_end(Ref ref, Ref target, std::size_t axis) {
    if (ref->tag == Tag::Sym && ref->text == "end") {
      return arena_.compound("end", {target, arena_.integer(std::to_string(axis))});
    }
    if (ref->tag != Tag::Compound) {
      return ref;
    }
    std::vector<Ref> args;
    args.reserve(ref->args.size());
    for (Ref arg : ref->args) {
      args.push_back(rewrite_end(arg, target, axis));
    }
    std::vector<Attr> attrs;
    attrs.reserve(ref->attrs.size());
    for (const auto& attr : ref->attrs) {
      attrs.push_back({attr.key, rewrite_end(attr.value, target, axis)});
    }
    return arena_.compound(ref->text, std::move(args), std::move(attrs));
  }

  Ref binder_tail() {
    std::string name = lex_.expect();
    lex_.expect(":");
    Ref domain = expr(0);
    return arena_.compound("binder",
                           {surface_atom_from_token(arena_, name), domain});
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
    if (target->tag != Tag::Compound) {
      return arena_.compound("@", {target, context});
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

  Ref rule_decl() {
    lex_.expect("rule");
    std::string name = lex_.expect();
    lex_.expect(":");
    Ref body = expr(0);
    std::vector<Attr> attrs;
    if (lex_.take("when")) {
      attrs.push_back({"when", expr(0)});
    }
    std::vector<Ref> metas;
    collect_meta(body, metas);
    for (const auto& attr : attrs) {
      collect_meta(attr.value, metas);
    }
    std::vector<Ref> forall_args = metas;
    forall_args.push_back(body);
    return arena_.compound("rule",
                           {arena_.sym(name),
                            arena_.compound("forall", forall_args, attrs)});
  }

  Ref labelled_decl(const std::string& head) {
    lex_.expect(head);
    std::string name = lex_.expect();
    lex_.expect(":");
    return arena_.compound(head, {arena_.sym(name), expr(0)});
  }

  void collect_meta(Ref ref, std::vector<Ref>& out) {
    if (!ref) {
      return;
    }
    if (ref->tag == Tag::Compound && ref->text == "meta") {
      for (Ref seen : out) {
        if (same_tree(seen, ref)) {
          return;
        }
      }
      out.push_back(ref);
    }
    for (Ref arg : ref->args) {
      collect_meta(arg, out);
    }
    for (const auto& attr : ref->attrs) {
      collect_meta(attr.value, out);
    }
  }
};

std::string print_surface_prec(Ref ref, int parent_prec);

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

std::string bracket_arg_surface(Ref ref, Ref target, std::size_t axis) {
  if (ref->tag == Tag::Compound && ref->text == "end" &&
      ref->args.size() == 2 && same_tree(ref->args[0], target) &&
      ref->args[1]->tag == Tag::Int &&
      ref->args[1]->text == std::to_string(axis)) {
    return "end";
  }
  if (ref->tag == Tag::Compound && ref->text == "range" &&
      ref->args.size() == 2) {
    std::string out =
        bracket_arg_surface(ref->args[0], target, axis) + ".." +
        bracket_arg_surface(ref->args[1], target, axis);
    if (Ref step = attr_value(ref, "step")) {
      out += ", step=" + print_surface_prec(step, 0);
    }
    return out;
  }
  return print_surface_prec(ref, 0);
}

std::string print_surface_prec(Ref ref, int parent_prec) {
  if (ref->tag != Tag::Compound) {
    return print_atom(ref);
  }
  if (ref->text == "rule" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Sym && ref->args[1]->tag == Tag::Compound &&
      ref->args[1]->text == "forall" && !ref->args[1]->args.empty()) {
    Ref forall = ref->args[1];
    Ref body = forall->args.back();
    std::string out = "rule " + ref->args[0]->text + ": " +
                      print_surface_prec(body, 0);
    if (Ref when = attr_value(forall, "when")) {
      out += " when " + print_surface_prec(when, 0);
    }
    return out;
  }
  if (ref->text == "goal" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Sym) {
    return "goal " + ref->args[0]->text + ": " +
           print_surface_prec(ref->args[1], 0);
  }
  if (ref->text == "neg" && ref->args.size() == 1) {
    std::string out = "-" + print_surface_prec(ref->args[0], 80);
    if (80 < parent_prec) {
      return "(" + out + ")";
    }
    return out;
  }
  if (ref->text == "lam" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder") {
    int prec = prec_of("|->");
    std::string out = print_surface_prec(ref->args[0]->args[0], prec) +
                      " |-> " + print_surface_prec(ref->args[1], prec);
    if (prec < parent_prec) {
      return "(" + out + ")";
    }
    return out;
  }
  if (ref->text == "at" && ref->args.size() >= 2) {
    std::vector<std::string> parts;
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      parts.push_back(bracket_arg_surface(ref->args[i], ref->args[0], i));
    }
    return print_surface_prec(ref->args[0], 80) + "[" + join(parts, ", ") +
           "]";
  }
  if (ref->text == "slice" && ref->args.size() >= 2) {
    std::vector<std::string> parts;
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      parts.push_back(bracket_arg_surface(ref->args[i], ref->args[0], i));
    }
    return print_surface_prec(ref->args[0], 80) + "[" + join(parts, ", ") +
           "]";
  }
  if (ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder") {
    return ref->text + "[" + binder_surface(ref->args[0]) + "](" +
           print_surface_prec(ref->args[1], 0) + ")";
  }
  if (ref->text == "idx" && ref->args.size() == 2 &&
      ref->args[1]->tag == Tag::Compound && ref->args[1]->text == "down" &&
      ref->args[1]->args.size() == 1) {
    return print_surface_prec(ref->args[0], 80) + "_" +
           print_surface_prec(ref->args[1]->args[0], 80);
  }
  if (ref->text == "idx" && ref->args.size() > 1) {
    std::string out = print_surface_prec(ref->args[0], 80);
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      Ref index = ref->args[i];
      if (index->tag == Tag::Compound && index->args.size() == 1 &&
          (index->text == "up" || index->text == "down")) {
        out += index->text == "up" ? "^" : "_";
        out += print_surface_prec(index->args[0], 80);
      } else {
        out += "_{" + print_surface_prec(index, 0) + "}";
      }
    }
    return out;
  }
  if (ref->text == "diff" && ref->args.size() >= 2) {
    std::vector<std::string> vars;
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      vars.push_back(print_surface_prec(ref->args[i], 0));
    }
    return "diff[" + join(vars, ", ") + "](" +
           print_surface_prec(ref->args[0], 0) + ")";
  }
  if (ref->text == "meta" && ref->args.size() == 1) {
    std::string out = "?" + print_surface_prec(ref->args[0], 0);
    Ref kind = attr_value(ref, "kind");
    if (kind && kind->text == "seq") {
      out += "...";
    } else if (kind && kind->text == "seq?") {
      out += "...?";
    }
    return out;
  }
  if (ref->text == "setbuild" && ref->args.size() == 2) {
    std::string out = "set{ " + print_surface_prec(ref->args[0], 0) + " | " +
                      binder_surface(ref->args[1]);
    if (Ref when = attr_value(ref, "when")) {
      out += ", " + print_surface_prec(when, 0);
    }
    return out + " }";
  }
  if (ref->text == "dict") {
    std::vector<std::string> parts;
    for (Ref arg : ref->args) {
      if (arg->tag == Tag::Compound && arg->text == "pair" &&
          arg->args.size() == 2) {
        parts.push_back(print_surface_prec(arg->args[0], 0) + " : " +
                        print_surface_prec(arg->args[1], 0));
      } else {
        parts.push_back(print_surface_prec(arg, 0));
      }
    }
    return "dict{ " + join(parts, ", ") + " }";
  }
  if (ref->text == "set" || ref->text == "seq") {
    std::vector<std::string> parts;
    for (Ref arg : ref->args) {
      parts.push_back(print_surface_prec(arg, 0));
    }
    return ref->text + "{ " + join(parts, ", ") + " }";
  }
  if (ref->text == "broadcast" && ref->args.size() == 2 &&
      ref->args[1]->tag == Tag::Compound && ref->args[1]->text == "args") {
    std::vector<std::string> parts;
    for (Ref arg : ref->args[1]->args) {
      parts.push_back(print_surface_prec(arg, 0));
    }
    return print_surface_prec(ref->args[0], 80) + ".(" + join(parts, ", ") + ")";
  }
  int prec = prec_of(ref->text);
  if (prec >= 0 && ref->args.size() == 2) {
    const internal::OpInfo* op = lookup_op(ref->text);
    std::string glyph = op ? op->surface : ref->text;
    bool ra = right_assoc(ref->text);
    std::string out = print_surface_prec(ref->args[0], prec + (ra ? 1 : 0)) +
                      " " + glyph + " " +
                      print_surface_prec(ref->args[1], prec + (ra ? 0 : 1));
    if (ref->text == "range") {
      out = print_surface_prec(ref->args[0], prec) + glyph +
            print_surface_prec(ref->args[1], prec + 1);
    }
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
    int at_prec = prec_of("@");
    std::string out = print_surface_prec(ref->args[0], at_prec) + " @ subst{" +
                      join(subs, ", ") + "}";
    if (at_prec < parent_prec) {
      return "(" + out + ")";
    }
    return out;
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

std::string print_latex_prec(Ref ref, int parent_prec);

std::string latex_atom(Ref ref) {
  static const std::unordered_map<std::string, std::string> symbols = {
      {"pi", "\\pi"},       {"alpha", "\\alpha"},   {"beta", "\\beta"},
      {"gamma", "\\gamma"}, {"delta", "\\delta"},   {"epsilon", "\\epsilon"},
      {"theta", "\\theta"}, {"lambda", "\\lambda"}, {"mu", "\\mu"},
      {"nu", "\\nu"},       {"sigma", "\\sigma"},   {"omega", "\\omega"}};
  auto it = symbols.find(ref->text);
  return it != symbols.end() ? it->second : ref->text;
}

std::string latex_wrap(std::string out, int prec, int parent_prec) {
  if (prec < parent_prec) {
    return "\\left(" + out + "\\right)";
  }
  return out;
}

std::string latex_attrs(Ref ref) {
  if (ref->attrs.empty()) {
    return "";
  }
  std::vector<std::string> parts;
  for (const auto& attr : ref->attrs) {
    parts.push_back("\\operatorname{" + attr.key + "}=" +
                    print_latex_prec(attr.value, 0));
  }
  return "\\;_{[" + join(parts, ", ") + "]}";
}

std::string latex_binder(Ref binder) {
  if (binder->tag != Tag::Compound || binder->text != "binder" ||
      binder->args.size() != 2) {
    throw Error("expected binder node");
  }
  Ref domain = binder->args[1];
  if (domain->tag == Tag::Compound && domain->text == "approach" &&
      domain->args.size() == 1) {
    return print_latex_prec(binder->args[0], 0) + " \\to " +
           print_latex_prec(domain->args[0], 0);
  }
  if (domain->tag == Tag::Compound && domain->text == "range" &&
      domain->args.size() == 2) {
    return print_latex_prec(binder->args[0], 0) + " = " +
           print_latex_prec(domain->args[0], 0) + ",\\ldots," +
           print_latex_prec(domain->args[1], 0);
  }
  return print_latex_prec(binder->args[0], 0) + " \\in " +
         print_latex_prec(domain, 0);
}

std::string latex_slice_arg(Ref ref) {
  if (ref->tag == Tag::Compound && ref->text == "range" &&
      ref->args.size() == 2) {
    if (Ref step = attr_value(ref, "step")) {
      return print_latex_prec(ref->args[0], 0) + ":" +
             print_latex_prec(step, 0) + ":" +
             print_latex_prec(ref->args[1], 0);
    }
    return print_latex_prec(ref->args[0], 0) + ".." +
           print_latex_prec(ref->args[1], 0);
  }
  return print_latex_prec(ref, 0);
}

std::string print_latex_prec(Ref ref, int parent_prec) {
  if (ref->tag != Tag::Compound) {
    return latex_atom(ref);
  }
  if (ref->text == "rule" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Sym && ref->args[1]->tag == Tag::Compound &&
      ref->args[1]->text == "forall" && !ref->args[1]->args.empty()) {
    Ref forall = ref->args[1];
    std::string out = "\\operatorname{rule}_{\\mathrm{" +
                      ref->args[0]->text + "}}: " +
                      print_latex_prec(forall->args.back(), 0);
    if (Ref when = attr_value(forall, "when")) {
      out += "\\;\\operatorname{when}\\;" + print_latex_prec(when, 0);
    }
    return out;
  }
  if (ref->text == "goal" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Sym) {
    return "\\operatorname{goal}_{\\mathrm{" + ref->args[0]->text +
           "}}\\left(" + print_latex_prec(ref->args[1], 0) + "\\right)";
  }
  if (ref->text == "neg" && ref->args.size() == 1) {
    return latex_wrap("-" + print_latex_prec(ref->args[0], 80), 80,
                      parent_prec);
  }
  if (ref->text == "meta" && ref->args.size() == 1) {
    std::string out = "?" + print_latex_prec(ref->args[0], 0);
    Ref kind = attr_value(ref, "kind");
    if (kind && kind->text == "seq") {
      out += "\\ldots";
    } else if (kind && kind->text == "seq?") {
      out += "\\ldots?";
    }
    return out;
  }
  if (ref->text == "at" && ref->args.size() >= 2) {
    std::string out = print_latex_prec(ref->args[0], 80) + "_{";
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      out += print_latex_prec(ref->args[i], 0);
    }
    return out + "}";
  }
  if (ref->text == "slice" && ref->args.size() >= 2) {
    std::string out = print_latex_prec(ref->args[0], 80) + "_{";
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      if (i > 1) {
        out += ",";
      }
      out += latex_slice_arg(ref->args[i]);
    }
    return out + "}";
  }
  if (ref->text == "idx" && ref->args.size() == 2 &&
      ref->args[1]->tag == Tag::Compound && ref->args[1]->text == "down" &&
      ref->args[1]->args.size() == 1) {
    return print_latex_prec(ref->args[0], 80) + "_{" +
           print_latex_prec(ref->args[1]->args[0], 0) + "}";
  }
  if (ref->text == "idx" && ref->args.size() > 1) {
    std::string out = print_latex_prec(ref->args[0], 80);
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      Ref index = ref->args[i];
      if (index->tag == Tag::Compound && index->args.size() == 1 &&
          index->text == "up") {
        out += "^{" + print_latex_prec(index->args[0], 0) + "}";
      } else if (index->tag == Tag::Compound && index->args.size() == 1 &&
                 index->text == "down") {
        out += "_{" + print_latex_prec(index->args[0], 0) + "}";
      }
    }
    return out;
  }
  if (ref->text == "diff" && ref->args.size() >= 2) {
    std::string out = "\\frac{d";
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      out += "^{}";
    }
    out += "}{";
    for (std::size_t i = 1; i < ref->args.size(); ++i) {
      out += "d" + print_latex_prec(ref->args[i], 0);
    }
    out += "} " + print_latex_prec(ref->args[0], 0);
    return out;
  }
  if (ref->text == "lam" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder") {
    int prec = prec_of("|->");
    std::string out = print_latex_prec(ref->args[0]->args[0], prec) +
                      " \\mapsto " + print_latex_prec(ref->args[1], prec);
    return latex_wrap(out, prec, parent_prec);
  }
  if ((ref->text == "forall" || ref->text == "exists") &&
      ref->args.size() == 2 && ref->args[0]->tag == Tag::Compound &&
      ref->args[0]->text == "binder") {
    std::string quant = ref->text == "forall" ? "\\forall" : "\\exists";
    return quant + " " + latex_binder(ref->args[0]) + ",\\; " +
           print_latex_prec(ref->args[1], 0);
  }
  if (ref->text == "setbuild" && ref->args.size() == 2) {
    std::string out = "\\left\\{ " + print_latex_prec(ref->args[0], 0) +
                      " \\mid " + latex_binder(ref->args[1]);
    if (Ref when = attr_value(ref, "when")) {
      out += ",\\; " + print_latex_prec(when, 0);
    }
    return out + " \\right\\}";
  }
  if (ref->text == "dict") {
    std::vector<std::string> parts;
    for (Ref arg : ref->args) {
      if (arg->tag == Tag::Compound && arg->text == "pair" &&
          arg->args.size() == 2) {
        parts.push_back(print_latex_prec(arg->args[0], 0) + " \\mapsto " +
                        print_latex_prec(arg->args[1], 0));
      } else {
        parts.push_back(print_latex_prec(arg, 0));
      }
    }
    return "\\left\\{ " + join(parts, ", ") + " \\right\\}";
  }
  if (ref->text == "set") {
    std::vector<std::string> parts;
    for (Ref arg : ref->args) {
      parts.push_back(print_latex_prec(arg, 0));
    }
    return "\\left\\{ " + join(parts, ", ") + " \\right\\}";
  }
  if (ref->text == "seq") {
    std::vector<std::string> parts;
    for (Ref arg : ref->args) {
      parts.push_back(print_latex_prec(arg, 0));
    }
    return "\\left\\langle " + join(parts, ", ") + " \\right\\rangle";
  }
  if (ref->text == "^" && ref->args.size() == 2) {
    return print_latex_prec(ref->args[0], prec_of("^") + 1) + "^{" +
           print_latex_prec(ref->args[1], 0) + "}";
  }
  if (ref->text == "/" && ref->args.size() == 2) {
    return "\\frac{" + print_latex_prec(ref->args[0], 0) + "}{" +
           print_latex_prec(ref->args[1], 0) + "}";
  }
  if (const internal::OpInfo* op = lookup_op(ref->text);
      op && ref->args.size() == 2 && ref->text != "range") {
    int prec = op->prec;
    bool ra = right_assoc(ref->text);
    std::string sep = ref->text == "*" ? " " : " " + std::string(op->latex) + " ";
    std::string out =
        print_latex_prec(ref->args[0], prec + (ra ? 1 : 0)) + sep +
        print_latex_prec(ref->args[1], prec + (ra ? 0 : 1));
    return latex_wrap(out, prec, parent_prec);
  }
  if ((ref->text == "sin" || ref->text == "cos" || ref->text == "tan" ||
       ref->text == "log") &&
      ref->args.size() == 1) {
    return "\\" + ref->text + "\\left(" + print_latex_prec(ref->args[0], 0) +
           "\\right)";
  }
  if (ref->text == "sqrt" && ref->args.size() == 1) {
    return "\\sqrt{" + print_latex_prec(ref->args[0], 0) + "}";
  }
  if (ref->text == "int" && ref->args.size() == 2 &&
      ref->args[0]->args.size() == 2) {
    Ref b = ref->args[0];
    Ref dom = b->args[1];
    if (dom->tag == Tag::Compound && dom->text == "range" &&
        dom->args.size() == 2) {
      return "\\int_{" + print_latex_prec(dom->args[0], 0) + "}^{" +
             print_latex_prec(dom->args[1], 0) + "} " +
             print_latex_prec(ref->args[1], 0) + "\\,d" +
             print_latex_prec(b->args[0], 0);
    }
  }
  if (ref->text == "lim" && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    return "\\lim_{" + latex_binder(ref->args[0]) + "} " +
           print_latex_prec(ref->args[1], 0);
  }
  if ((ref->text == "sum" || ref->text == "prod") && ref->args.size() == 2 &&
      ref->args[0]->tag == Tag::Compound && ref->args[0]->text == "binder" &&
      ref->args[0]->args.size() == 2) {
    Ref b = ref->args[0];
    Ref dom = b->args[1];
    std::string cmd = ref->text == "sum" ? "\\sum" : "\\prod";
    if (dom->tag == Tag::Compound && dom->text == "range" &&
        dom->args.size() == 2) {
      return cmd + "_{" + print_latex_prec(b->args[0], 0) + " = " +
             print_latex_prec(dom->args[0], 0) + "}^{" +
             print_latex_prec(dom->args[1], 0) + "} " +
             print_latex_prec(ref->args[1], 0);
    }
    return cmd + "_{" + latex_binder(b) + "} " +
           print_latex_prec(ref->args[1], 0);
  }
  std::vector<std::string> parts;
  for (Ref arg : ref->args) {
    parts.push_back(print_latex_prec(arg, 0));
  }
  return "\\operatorname{" + ref->text + "}\\left(" + join(parts, ", ") +
         "\\right)" + latex_attrs(ref);
}

std::string print_latex_inner(Ref ref) {
  return print_latex_prec(ref, 0);
}

void validate_walk(Ref ref, std::vector<Diagnostic>& out) {
  if (!ref) {
    return;
  }
  if (ref->tag == Tag::Sym && ref->text == "end") {
    out.push_back({"EndOutsideIndex",
                   "`end` is only bracket-local inside access or slice; use "
                   "end(v, axis) outside indexing"});
    return;
  }
  if (ref->tag != Tag::Compound) {
    return;
  }
  if (ref->text == "at" && ref->args.size() >= 2 &&
      ref->args[0]->tag == Tag::Sym &&
      is_known_nonindexed_function(ref->args[0]->text)) {
    out.push_back(
        {"IndexingKnownFunction",
         "parsed as concrete access at(" + ref->args[0]->text +
             ", ...); did you mean " + ref->args[0]->text + "(...)?"});
  }
  for (Ref arg : ref->args) {
    validate_walk(arg, out);
  }
  for (const auto& attr : ref->attrs) {
    validate_walk(attr.value, out);
  }
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
      if (input_[pos_] == '\\') {
        if (pos_ + 1 >= input_.size()) {
          throw Error("unterminated JSON escape");
        }
        ++pos_;
        char escaped = input_[pos_++];
        switch (escaped) {
        case '"':
        case '\\':
        case '/':
          out.push_back(escaped);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          if (pos_ + 4 > input_.size()) {
            throw Error("unterminated JSON unicode escape");
          }
          std::string hex = input_.substr(pos_, 4);
          pos_ += 4;
          char* end = nullptr;
          long code = std::strtol(hex.c_str(), &end, 16);
          if (!end || *end != '\0' || code < 0 || code > 0x7f) {
            throw Error("unsupported JSON unicode escape");
          }
          out.push_back(static_cast<char>(code));
          break;
        }
        default:
          throw Error("unsupported JSON escape");
        }
      } else {
        out.push_back(input_[pos_++]);
      }
    }
    expect('"');
    return out;
  }

  void skip_string() { (void)string(); }

  void skip_literal(const std::string& literal) {
    ws();
    if (input_.compare(pos_, literal.size(), literal) != 0) {
      throw Error("expected JSON literal: " + literal);
    }
    pos_ += literal.size();
  }

  void skip_array() {
    expect('[');
    if (!take(']')) {
      do {
        skip_value();
      } while (take(','));
      expect(']');
    }
  }

  void skip_object() {
    expect('{');
    if (!take('}')) {
      do {
        skip_string();
        expect(':');
        skip_value();
      } while (take(','));
      expect('}');
    }
  }

  void skip_value() {
    ws();
    if (pos_ >= input_.size()) {
      throw Error("unexpected end of JSON");
    }
    char c = input_[pos_];
    if (c == '"') {
      skip_string();
      return;
    }
    if (c == '{') {
      skip_object();
      return;
    }
    if (c == '[') {
      skip_array();
      return;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
      ++pos_;
      while (pos_ < input_.size() &&
             (std::isdigit(static_cast<unsigned char>(input_[pos_])) ||
              input_[pos_] == '.' || input_[pos_] == 'e' ||
              input_[pos_] == 'E' || input_[pos_] == '+' ||
              input_[pos_] == '-')) {
        ++pos_;
      }
      return;
    }
    if (input_.compare(pos_, 4, "true") == 0) {
      skip_literal("true");
      return;
    }
    if (input_.compare(pos_, 5, "false") == 0) {
      skip_literal("false");
      return;
    }
    if (input_.compare(pos_, 4, "null") == 0) {
      skip_literal("null");
      return;
    }
    throw Error("unsupported JSON value");
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
    std::string atom_tag;
    std::string atom_value;
    std::string h;
    std::vector<Ref> args;
    std::vector<Attr> attrs;
    while (!take('}')) {
      std::string key = string();
      expect(':');
      if (key == "atom") {
        atom_tag = string();
      } else if (key == "value") {
        atom_value = string();
      } else if (key == "head") {
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
        skip_value();
      }
      take(',');
    }
    if (!atom_tag.empty()) {
      if (atom_tag == "sym") {
        return arena_.sym(atom_value);
      }
      if (atom_tag == "int") {
        return arena_.integer(atom_value);
      }
      if (atom_tag == "rat") {
        std::size_t slash = atom_value.find('/');
        if (slash == std::string::npos) {
          throw Error("rational object atom must be numerator/denominator");
        }
        return arena_.rational(atom_value.substr(0, slash),
                               atom_value.substr(slash + 1));
      }
      if (atom_tag == "real") {
        return arena_.real(atom_value);
      }
      if (atom_tag == "str") {
        return arena_.string(atom_value);
      }
      throw Error("unknown object atom tag: " + atom_tag);
    }
    if (h.empty()) {
      throw Error("object node missing head");
    }
    return arena_.compound(h, std::move(args), std::move(attrs));
  }
};

std::string object_inner(Ref ref) {
  if (ref->tag == Tag::Sym) {
    return "{\"atom\":\"sym\",\"value\":\"" + escape(ref->text) + "\"}";
  }
  if (ref->tag == Tag::Int) {
    return "{\"atom\":\"int\",\"value\":\"" + escape(ref->text) + "\"}";
  }
  if (ref->tag == Tag::Rat) {
    return "{\"atom\":\"rat\",\"value\":\"" + escape(ref->text) + "\"}";
  }
  if (ref->tag == Tag::Real) {
    return "{\"atom\":\"real\",\"value\":\"" + escape(ref->text) + "\"}";
  }
  if (ref->tag == Tag::Str) {
    return "{\"atom\":\"str\",\"value\":\"" + escape(ref->text) + "\"}";
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

std::vector<Diagnostic> validate(Ref ref) {
  std::vector<Diagnostic> out;
  validate_walk(ref, out);
  return out;
}

} // namespace facet
