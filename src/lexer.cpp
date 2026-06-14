#include "facet_internal.hpp"

#include <cctype>
#include <sstream>
#include <string_view>

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
  std::size_t token_offset = pos_;
  std::size_t token_line = line_;
  std::size_t token_column = column_;
  if (pos_ >= input_.size()) {
    tok_ = {"", true, token_offset, token_line, token_column};
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
    tok_ = {input_.substr(start, pos_ - start), false, token_offset, token_line,
            token_column};
    return;
  }
  if (std::isdigit(static_cast<unsigned char>(c)) ||
      (c == '-' && pos_ + 1 < input_.size() &&
       std::isdigit(static_cast<unsigned char>(input_[pos_ + 1])))) {
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
    tok_ = {input_.substr(start, pos_ - start), false, token_offset, token_line,
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
    tok_ = {'"' + s + '"', false, token_offset, token_line, token_column};
    return;
  }

  static const std::vector<std::string> ops = {
      "===", "...?", "...", ":=", "~>", "~=", "=>", "|->",
      "|>",   "->",  "<-",  "..",  ">=", "<=", "!=", ".("};
  for (const auto& op : ops) {
    if (input_.compare(pos_, op.size(), op) == 0) {
      for (std::size_t i = 0; i < op.size(); ++i) {
        advance();
      }
      tok_ = {op, false, token_offset, token_line, token_column};
      return;
    }
  }
  tok_ = {std::string(1, advance()), false, token_offset, token_line,
          token_column};
}

namespace {

std::string location(std::size_t line, std::size_t column) {
  return "line " + std::to_string(line) + ", column " +
         std::to_string(column);
}

std::string_view rtrim(std::string_view line) {
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t' ||
                           line.back() == '\r')) {
    line.remove_suffix(1);
  }
  return line;
}

void push_layout(std::vector<LayoutTok>& out, std::string text,
                 std::size_t offset, std::size_t line, std::size_t column) {
  out.push_back({std::move(text), offset, line, column});
}

} // namespace

std::vector<LayoutTok> lex_layout_for_test(const std::string& input) {
  std::vector<LayoutTok> out;
  std::vector<std::size_t> indents{0};
  bool pending_block = false;
  int bracket_depth = 0;
  std::size_t line_no = 1;
  std::size_t pos = 0;

  while (pos <= input.size()) {
    std::size_t line_start = pos;
    std::size_t line_end = input.find('\n', pos);
    bool final_line = line_end == std::string::npos;
    if (final_line) {
      line_end = input.size();
    }
    pos = final_line ? input.size() + 1 : line_end + 1;

    std::string_view raw(input.data() + line_start, line_end - line_start);
    raw = rtrim(raw);
    std::size_t indent = 0;
    while (indent < raw.size() && raw[indent] == ' ') {
      ++indent;
    }
    if (indent < raw.size() && raw[indent] == '\t') {
      throw Error("tabs are not allowed in layout indentation at " +
                  location(line_no, indent + 1));
    }
    if (indent == raw.size()) {
      ++line_no;
      continue;
    }

    if (bracket_depth == 0) {
      std::size_t current = indents.back();
      if (indent > current) {
        if (!pending_block) {
          throw Error("unexpected indent at " + location(line_no, indent + 1));
        }
        indents.push_back(indent);
        push_layout(out, "INDENT", line_start + indent, line_no, indent + 1);
      } else {
        if (pending_block) {
          throw Error("expected indented block at " +
                      location(line_no, indent + 1));
        }
        while (indent < indents.back()) {
          indents.pop_back();
          push_layout(out, "DEDENT", line_start + indent, line_no,
                      indent + 1);
        }
        if (indent != indents.back()) {
          throw Error("dedent does not match any outer indentation at " +
                      location(line_no, indent + 1));
        }
      }
      pending_block = false;
    }

    std::string text(raw.substr(indent));
    Lexer lex(text);
    std::string last;
    while (!lex.peek().eof) {
      Tok tok = lex.peek();
      last = tok.text;
      push_layout(out, tok.text, line_start + indent + tok.offset, line_no,
                  indent + tok.column);
      if (tok.text == "(" || tok.text == "[" || tok.text == "{") {
        ++bracket_depth;
      } else if (tok.text == ")" || tok.text == "]" || tok.text == "}") {
        if (bracket_depth > 0) {
          --bracket_depth;
        }
      }
      lex.expect();
    }
    if (bracket_depth == 0) {
      push_layout(out, "NEWLINE", line_start + raw.size(), line_no,
                  raw.size() + 1);
      pending_block = last == ":";
    }
    ++line_no;
  }

  if (pending_block) {
    throw Error("expected indented block at end of input");
  }
  while (indents.size() > 1) {
    indents.pop_back();
    push_layout(out, "DEDENT", input.size(), line_no, 1);
  }
  return out;
}

} // namespace facet::internal
