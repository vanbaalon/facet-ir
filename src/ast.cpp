#include "facet_internal.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

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

std::string key_of(const Node& node) {
  std::string out;
  out.reserve(node.text.size() + node.args.size() * 24 + node.attrs.size() * 32 + 16);
  out += std::to_string(static_cast<int>(node.tag));
  out += ':';
  out += node.text;
  out += '(';
  for (Ref arg : node.args) {
    out += std::to_string(arg->hash);
    out += ',';
  }
  out += ')';
  for (const auto& attr : node.attrs) {
    out += ':';
    out += attr.key;
    out += '=';
    out += std::to_string(attr.value->hash);
  }
  return out;
}

bool decimal_digits(const std::string& s) {
  std::size_t start = (!s.empty() && s.front() == '-') ? 1 : 0;
  return start < s.size() &&
         std::all_of(s.begin() + static_cast<std::ptrdiff_t>(start), s.end(),
                     [](unsigned char c) { return std::isdigit(c); });
}

bool all_zeroes(const std::string& s) {
  std::size_t start = (!s.empty() && s.front() == '-') ? 1 : 0;
  return start < s.size() &&
         std::all_of(s.begin() + static_cast<std::ptrdiff_t>(start), s.end(),
                     [](char c) { return c == '0'; });
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

Ref Arena::rational(std::string numerator, std::string denominator) {
  if (!decimal_digits(numerator)) {
    throw Error("invalid rational numerator: " + numerator);
  }
  if (!decimal_digits(denominator) || all_zeroes(denominator)) {
    throw Error("invalid rational denominator: " + denominator);
  }
  Node node;
  node.tag = Tag::Rat;
  node.text = std::move(numerator) + "/" + std::move(denominator);
  return intern(std::move(node));
}

Ref Arena::real(std::string value) {
  Node node;
  node.tag = Tag::Real;
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
  for (std::size_t i = 1; i < attrs.size(); ++i) {
    if (attrs[i - 1].key == attrs[i].key) {
      throw Error("duplicate attribute: " + attrs[i].key);
    }
  }
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
  if (auto found = index_.find(key); found != index_.end()) {
    return found->second;
  }
  nodes_.push_back(std::make_unique<Node>(std::move(node)));
  Ref ref = nodes_.back().get();
  index_.emplace(std::move(key), ref);
  return ref;
}

bool same_tree(Ref lhs, Ref rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (!lhs || !rhs || lhs->tag != rhs->tag || lhs->text != rhs->text ||
      lhs->args.size() != rhs->args.size() ||
      lhs->attrs.size() != rhs->attrs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs->args.size(); ++i) {
    if (!same_tree(lhs->args[i], rhs->args[i])) {
      return false;
    }
  }
  for (std::size_t i = 0; i < lhs->attrs.size(); ++i) {
    if (lhs->attrs[i].key != rhs->attrs[i].key ||
        !same_tree(lhs->attrs[i].value, rhs->attrs[i].value)) {
      return false;
    }
  }
  return true;
}

namespace internal {

std::string escape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        std::ostringstream hex;
        hex << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(c);
        out += hex.str();
      } else {
        out.push_back(static_cast<char>(c));
      }
    }
  }
  return out;
}

bool is_int_token(const std::string& s) {
  std::size_t start = (!s.empty() && s.front() == '-') ? 1 : 0;
  return start < s.size() &&
         std::all_of(s.begin() + static_cast<std::ptrdiff_t>(start), s.end(),
                     [](unsigned char c) { return std::isdigit(c); });
}

bool is_real_token(const std::string& s) {
  bool seen_dot = false;
  bool seen_digit = false;
  for (std::size_t i = (!s.empty() && s.front() == '-') ? 1 : 0; i < s.size(); ++i) {
    char c = s[i];
    if (std::isdigit(static_cast<unsigned char>(c))) {
      seen_digit = true;
    } else if (c == '.' && !seen_dot) {
      seen_dot = true;
    } else {
      return false;
    }
  }
  return seen_dot && seen_digit;
}

bool is_string_token(const std::string& s) {
  return s.size() >= 2 && s.front() == '"' && s.back() == '"';
}

Ref atom_from_token(Arena& arena, const std::string& token) {
  if (is_int_token(token)) {
    return arena.integer(token);
  }
  if (is_real_token(token)) {
    return arena.real(token);
  }
  if (is_string_token(token)) {
    return arena.string(token.substr(1, token.size() - 2));
  }
  return arena.sym(token);
}

Ref surface_atom_from_token(Arena& arena, const std::string& token) {
  if (token.size() > 1 && token.front() == '?') {
    std::string name = token.substr(1);
    std::string kind = "one";
    if (name.size() >= 4 && name.substr(name.size() - 4) == "...?") {
      name.resize(name.size() - 4);
      kind = "seq?";
    } else if (name.size() >= 3 && name.substr(name.size() - 3) == "...") {
      name.resize(name.size() - 3);
      kind = "seq";
    }
    return arena.compound("meta", {arena.sym(name)}, {{"kind", arena.sym(kind)}});
  }

  std::size_t under = token.find('_');
  if (under != std::string::npos && under > 0 && under + 1 < token.size()) {
    return arena.compound(
        "idx", {arena.sym(token.substr(0, under)),
                arena.compound("down", {arena.sym(token.substr(under + 1))})});
  }
  return atom_from_token(arena, token);
}

Ref attr_value(Ref ref, const std::string& key) {
  for (const auto& attr : ref->attrs) {
    if (attr.key == key) {
      return attr.value;
    }
  }
  return nullptr;
}

std::string print_atom(Ref ref) {
  if (ref->tag == Tag::Str) {
    return "\"" + escape(ref->text) + "\"";
  }
  return ref->text;
}

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

} // namespace internal
} // namespace facet
