#include "facet_internal.hpp"

#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace facet::internal {

const std::vector<OpInfo>& registry() {
  static const std::vector<OpInfo> ops = {
      {"|>", "|>", 10, false, "\\triangleright"},
      {"=>", "=>", 20, false, "\\Rightarrow"},
      {"~>", "~>", 20, false, "\\to"},
      {"~=", "~=", 20, false, "\\sim"},
      {":=", ":=", 25, false, ":="},
      {"=", "=", 30, false, "="},
      {"===", "===", 30, false, "\\equiv"},
      {"!=", "!=", 30, false, "\\ne"},
      {">", ">", 30, false, ">"},
      {"<", "<", 30, false, "<"},
      {">=", ">=", 30, false, "\\ge"},
      {"<=", "<=", 30, false, "\\le"},
      {"range", "..", 35, false, ".."},
      {"|->", "|->", 40, true, "\\mapsto"},
      {"->", "->", 45, true, "\\to"},
      {"+", "+", 50, false, "+"},
      {"-", "-", 50, false, "-"},
      {"*", "*", 60, false, " "},
      {"/", "/", 60, false, "/"},
      {"^", "^", 70, true, "^"},
  };
  return ops;
}

const OpInfo* lookup_op(const std::string& head) {
  static const std::unordered_map<std::string, const OpInfo*> index = []() {
    std::unordered_map<std::string, const OpInfo*> m;
    m.reserve(registry().size() * 2);
    for (const auto& op : registry()) {
      m.emplace(op.head, &op);
      if (std::string_view(op.surface) != std::string_view(op.head)) {
        m.emplace(op.surface, &op);
      }
    }
    return m;
  }();
  auto it = index.find(head);
  return it != index.end() ? it->second : nullptr;
}

bool is_binder_head(const std::string& head) {
  static const std::unordered_set<std::string> heads = {
      "sum",        "prod",   "int",         "lim",       "forall",
      "exists",     "diff",   "plot",        "plot3d",    "parametric",
      "param",      "contour", "field",      "complexplot", "manipulate",
      "setbuild",   "seq",    "fold",        "scan",
  };
  return heads.find(head) != heads.end();
}

bool is_known_nonindexed_function(const std::string& head) {
  static const std::unordered_set<std::string> heads = {
      "sin", "cos", "tan", "log", "exp", "sqrt", "abs", "det", "tr",
      "erf", "erfc", "gamma", "factorial",
      "simplify", "expand", "factor",
  };
  return heads.find(head) != heads.end();
}

int prec_of(const std::string& op) {
  if (op == "@") {
    return 10;
  }
  if (const OpInfo* info = lookup_op(op)) {
    return info->prec;
  }
  return -1;
}

bool right_assoc(const std::string& op) {
  if (const OpInfo* info = lookup_op(op)) {
    return info->right_assoc;
  }
  return false;
}

} // namespace facet::internal
