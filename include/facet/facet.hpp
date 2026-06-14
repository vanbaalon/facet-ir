#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace facet {

enum class Tag { Sym, Int, Rat, Real, Str, Compound };

struct Node;
using Ref = const Node*;

struct Attr {
  std::string key;
  Ref value;
};

struct Node {
  Tag tag{};
  std::string text;
  std::vector<Ref> args;
  std::vector<Attr> attrs;
  std::uint64_t hash{};
};

struct Diagnostic {
  std::string code;
  std::string message;
};

struct Unmapped {
  std::string head;
  std::string kernel;
  std::string path;
};

struct Coverage {
  std::string kernel;
  int supported = 0;
  int total = 0;
  std::vector<Unmapped> missing;
};

struct CompareResult {
  std::string status;
  std::string by;
  std::string strength;
  std::string detail;
  int samples = 0;
  double tol = 0.0;
  std::string witness;
  bool agreement = false;
};

class Arena {
public:
  Ref sym(std::string name);
  Ref integer(std::string value);
  Ref rational(std::string numerator, std::string denominator);
  Ref real(std::string value);
  Ref string(std::string value);
  Ref compound(std::string head, std::vector<Ref> args = {},
               std::vector<Attr> attrs = {});

private:
  std::vector<std::unique_ptr<Node>> nodes_;
  std::unordered_map<std::uint64_t, std::vector<Ref>> index_;

  Ref intern(Node node);
};

class Error : public std::runtime_error {
public:
  explicit Error(const std::string& message);
};

Ref read_core(Arena& arena, const std::string& input);
std::string print_core(Ref ref);

Ref read_strict(Arena& arena, const std::string& input);
std::string print_strict(Ref ref);

Ref read_surface(Arena& arena, const std::string& input);
std::string print_surface(Ref ref);

Ref read_object(Arena& arena, const std::string& input);
std::string print_object(Ref ref);

Ref read_sympy_srepr(Arena& arena, const std::string& input);
std::string print_latex(Ref ref);
std::string print_sympy(Ref ref);
std::string print_sympy_srepr(Ref ref);
Ref evaluate_sympy(Arena& arena, Ref ref);
Coverage coverage(Ref ref, const std::string& kernel);
CompareResult compare(Arena& arena, Ref lhs, Ref rhs, const std::string& by);

std::vector<Diagnostic> validate(Ref ref);
std::string render_svg(Ref ref);
bool same_tree(Ref lhs, Ref rhs);

} // namespace facet
