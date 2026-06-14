#include "facet_internal.hpp"

#include <string>
#include <utility>
#include <vector>

namespace facet {
namespace {

using internal::escape;
using internal::join;

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

} // namespace

std::string print_sympy(Ref ref) {
  return print_sympy_prec(ref, 0);
}

} // namespace facet
