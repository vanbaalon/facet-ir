#include "facet_internal.hpp"

#include <cctype>
#include <sstream>

namespace facet::internal {

Lexer::Lexer(std::string input) : input_(std::move(input)) { next(); }

const Tok& Lexer::peek() const { return tok_; }

bool Lexer::at(const std::string& text) const { return tok_.text == text; }

bool Lexer::take(const std::string& text) {
  if (!at(text)) {
    return false;
  }
  next();
  return true;
}

std::string Lexer::expect() {
  if (tok_.eof) {
    throw Error("unexpected end of input at " + location());
  }
  std::string text = tok_.text;
  next();
  return text;
}

void Lexer::expect(const std::string& text) {
  if (!take(text)) {
    throw Error("expected '" + text + "' at " + location() + ", got '" +
                tok_.text + "'");
  }
}

char Lexer::advance() {
  char c = input_[pos_++];
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

std::string Lexer::location() const {
  std::ostringstream out;
  out << "line " << tok_.line << ", column " << tok_.column;
  return out.str();
}

void Lexer::next() {
  while (pos_ < input_.size() &&
         std::isspace(static_cast<unsigned char>(input_[pos_]))) {
    advance();
  }
  std::size_t token_line = line_;
  std::size_t token_column = column_;
  if (pos_ >= input_.size()) {
    tok_ = {"", true, token_line, token_column};
    return;
  }

  char c = input_[pos_];
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '?' ||
      c == '\\') {
    std::size_t start = pos_;
    advance();
    while (pos_ < input_.size()) {
      char d = input_[pos_];
      if (!std::isalnum(static_cast<unsigned char>(d)) && d != '_' &&
          d != '?' && d != '\\') {
        break;
      }
      advance();
    }
    if (input_.compare(pos_, 3, "...") == 0) {
      advance();
      advance();
      advance();
      if (pos_ < input_.size() && input_[pos_] == '?') {
        advance();
      }
    }
    tok_ = {input_.substr(start, pos_ - start), false, token_line,
            token_column};
    return;
  }
  if (std::isdigit(static_cast<unsigned char>(c))) {
    std::size_t start = pos_;
    advance();
    while (pos_ < input_.size() &&
           std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
      advance();
    }
    if (pos_ < input_.size() && input_[pos_] == '.' &&
        pos_ + 1 < input_.size() && input_[pos_ + 1] != '.') {
      advance();
      while (pos_ < input_.size() &&
             std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
        advance();
      }
    }
    tok_ = {input_.substr(start, pos_ - start), false, token_line,
            token_column};
    return;
  }
  if (c == '"') {
    advance();
    std::string s;
    while (pos_ < input_.size() && input_[pos_] != '"') {
      if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) {
        advance();
      }
      s.push_back(advance());
    }
    if (pos_ >= input_.size()) {
      throw Error("unterminated string at line " + std::to_string(token_line) +
                  ", column " + std::to_string(token_column));
    }
    advance();
    tok_ = {'"' + s + '"', false, token_line, token_column};
    return;
  }

  static const std::vector<std::string> ops = {
      "===", "...?", "...", ":=", "~>", "~=", "=>", "|->",
      "|>",   "->",  "..",  ">=", "<=", "!=", ".("};
  for (const auto& op : ops) {
    if (input_.compare(pos_, op.size(), op) == 0) {
      for (std::size_t i = 0; i < op.size(); ++i) {
        advance();
      }
      tok_ = {op, false, token_line, token_column};
      return;
    }
  }
  tok_ = {std::string(1, advance()), false, token_line, token_column};
}

} // namespace facet::internal
