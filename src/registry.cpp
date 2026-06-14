#include "facet_internal.hpp"

#include <algorithm>

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
  for (const auto& op : registry()) {
    if (head == op.head || head == op.surface) {
      return &op;
    }
  }
  return nullptr;
}

bool is_binder_head(const std::string& head) {
  static const std::vector<std::string> heads = {
      "sum", "int", "prod", "lim", "forall", "exists", "solve"};
  return std::find(heads.begin(), heads.end(), head) != heads.end();
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
