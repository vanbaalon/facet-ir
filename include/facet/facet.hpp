#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
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
  std::vector<std::pair<std::string, Ref>> index_;

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

std::string print_latex(Ref ref);

bool same_tree(Ref lhs, Ref rhs);

} // namespace facet
