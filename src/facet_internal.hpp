#pragma once

#include "facet/facet.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace facet::internal {

struct Tok {
  std::string text;
  bool eof = false;
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t column = 1;
};

struct LayoutTok {
  std::string text;
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t column = 1;
};

class Lexer {
public:
  explicit Lexer(std::string input);

  const Tok& peek() const;
  bool at(const std::string& text) const;
  bool take(const std::string& text);
  std::string expect();
  void expect(const std::string& text);

private:
  std::string input_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
  Tok tok_;

  char advance();
  void skip_whitespace_and_comments();
  void skip_line_comment();
  void skip_block_comment();
  void next();
  std::string location() const;
};

std::vector<LayoutTok> lex_layout_for_test(const std::string& input);

struct OpInfo {
  const char* head;
  const char* surface;
  int prec;
  bool right_assoc;
  const char* latex;
};

const std::vector<OpInfo>& registry();
const OpInfo* lookup_op(const std::string& head);
bool is_binder_head(const std::string& head);
bool is_known_nonindexed_function(const std::string& head);
int prec_of(const std::string& op);
bool right_assoc(const std::string& op);

std::string escape(const std::string& s);
bool is_int_token(const std::string& s);
bool is_real_token(const std::string& s);
bool is_string_token(const std::string& s);
Ref atom_from_token(Arena& arena, const std::string& token);
Ref surface_atom_from_token(Arena& arena, const std::string& token);
Ref attr_value(Ref ref, const std::string& key);
std::string print_atom(Ref ref);
std::string join(const std::vector<std::string>& parts, const std::string& sep);

} // namespace facet::internal
