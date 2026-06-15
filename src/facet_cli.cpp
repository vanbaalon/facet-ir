#include "facet/facet.hpp"

#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::string option_value(const std::string& arg, const std::string& key) {
  std::string prefix = key + "=";
  if (arg.rfind(prefix, 0) == 0) {
    return arg.substr(prefix.size());
  }
  return "";
}

std::size_t mode_offset(const std::string& mode, const std::string& prefix) {
  if (mode.rfind(prefix, 0) != 0) {
    return 0;
  }
  return static_cast<std::size_t>(std::stoull(mode.substr(prefix.size())));
}

void usage() {
  std::cerr << "usage: facet read=<surface|strict|core|object|sympy-srepr> "
               "emit=<surface|strict|core|object|latex|directive|semantic-tokens|completions:N|hover:N|signature:N|diagnostics|render:svg|render:pdf|render:png|render:html|coverage:K|source:K|source:sympy-srepr|source:sympy-core|sympy|sympy-srepr|sympy-core> "
               "[compare=EXPR] [by=<structural|simplify|numeric|numeric(samples=N,tol=E)>] < input\n";
}

std::string json_escape(const std::string& text) {
  std::string out;
  for (char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
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
      out.push_back(c);
      break;
    }
  }
  return out;
}

std::string format_semantic_tokens(
    const std::vector<facet::SemanticToken>& tokens) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const auto& token = tokens[i];
    if (i) {
      out << ",";
    }
    out << "{\"offset\":" << token.offset << ",\"length\":" << token.length
        << ",\"type\":\"" << json_escape(token.type) << "\",\"modifiers\":[";
    for (std::size_t j = 0; j < token.modifiers.size(); ++j) {
      if (j) {
        out << ",";
      }
      out << "\"" << json_escape(token.modifiers[j]) << "\"";
    }
    out << "]}";
  }
  out << "]";
  return out.str();
}

std::string format_completions(
    const std::vector<facet::CompletionItem>& items) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i) {
      out << ",";
    }
    out << "{\"label\":\"" << json_escape(items[i].label) << "\",\"kind\":\""
        << json_escape(items[i].kind) << "\",\"detail\":\""
        << json_escape(items[i].detail) << "\",\"documentation\":\""
        << json_escape(items[i].documentation) << "\"}";
  }
  out << "]";
  return out.str();
}

std::string format_diagnostics(
    const std::vector<facet::Diagnostic>& diagnostics) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < diagnostics.size(); ++i) {
    if (i) {
      out << ",";
    }
    out << "{\"code\":\"" << json_escape(diagnostics[i].code)
        << "\",\"message\":\"" << json_escape(diagnostics[i].message)
        << "\",\"offset\":" << diagnostics[i].source_offset
        << ",\"length\":" << diagnostics[i].source_length << "}";
  }
  out << "]";
  return out.str();
}

std::string format_hover(const facet::HoverInfo& hover) {
  std::ostringstream out;
  out << "{\"surface\":\"" << json_escape(hover.surface) << "\",\"strict\":\""
      << json_escape(hover.strict) << "\",\"core\":\""
      << json_escape(hover.core) << "\",\"latex\":\""
      << json_escape(hover.latex) << "\",\"coverage\":{\"sympy\":{\"supported\":"
      << hover.sympy_coverage.supported << ",\"total\":"
      << hover.sympy_coverage.total << ",\"missing\":[";
  for (std::size_t i = 0; i < hover.sympy_coverage.missing.size(); ++i) {
    if (i) {
      out << ",";
    }
    const auto& missing = hover.sympy_coverage.missing[i];
    out << "{\"head\":\"" << json_escape(missing.head) << "\",\"path\":\""
        << json_escape(missing.path) << "\"}";
  }
  out << "]}}}";
  return out.str();
}

std::string format_signature(const facet::SignatureHelp& sig) {
  std::ostringstream out;
  out << "{\"head\":\"" << json_escape(sig.head)
      << "\",\"parameters\":[";
  for (std::size_t i = 0; i < sig.parameters.size(); ++i) {
    if (i) {
      out << ",";
    }
    out << "\"" << json_escape(sig.parameters[i]) << "\"";
  }
  out << "],\"activeParameter\":" << sig.active_parameter
      << ",\"documentation\":\"" << json_escape(sig.documentation) << "\"}";
  return out.str();
}

std::string format_directive(const facet::KernelDirective& directive) {
  std::ostringstream out;
  out << "{\"kind\":\"controller-directive\",\"verb\":\""
      << json_escape(directive.verb) << "\",\"scoped\":"
      << (directive.scoped ? "true" : "false") << ",\"args\":[";
  for (std::size_t i = 0; i < directive.args.size(); ++i) {
    if (i) {
      out << ",";
    }
    out << "{\"named\":" << (directive.args[i].named ? "true" : "false");
    if (directive.args[i].named) {
      out << ",\"key\":\"" << json_escape(directive.args[i].key) << "\"";
    }
    if (directive.args[i].list) {
      out << ",\"values\":[";
      for (std::size_t j = 0; j < directive.args[i].values.size(); ++j) {
        if (j) {
          out << ",";
        }
        out << "\"" << json_escape(directive.args[i].values[j]) << "\"";
      }
      out << "]";
    } else {
      out << ",\"value\":\"" << json_escape(directive.args[i].value)
          << "\"";
    }
    out << "}";
  }
  out << "]}";
  return out.str();
}

std::string json_string_field(const std::string& json, const std::string& key) {
  std::string marker = "\"" + key + "\":";
  std::size_t pos = json.find(marker);
  if (pos == std::string::npos) {
    return "";
  }
  pos += marker.size();
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  if (pos >= json.size() || json[pos] != '"') {
    return "";
  }
  ++pos;
  std::string out;
  while (pos < json.size()) {
    char c = json[pos++];
    if (c == '"') {
      break;
    }
    if (c == '\\' && pos < json.size()) {
      char escaped = json[pos++];
      if (escaped == 'n') {
        out.push_back('\n');
      } else if (escaped == 't') {
        out.push_back('\t');
      } else if (escaped == 'r') {
        out.push_back('\r');
      } else {
        out.push_back(escaped);
      }
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string json_id(const std::string& json) {
  std::size_t pos = json.find("\"id\":");
  if (pos == std::string::npos) {
    return "";
  }
  pos += 5;
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  if (pos < json.size() && json[pos] == '"') {
    return "\"" + json_string_field(json.substr(pos - 5), "id") + "\"";
  }
  std::size_t end = pos;
  while (end < json.size() && json[end] != ',' && json[end] != '}') {
    ++end;
  }
  return json.substr(pos, end - pos);
}

int json_int_field(const std::string& json, const std::string& key) {
  std::string marker = "\"" + key + "\":";
  std::size_t pos = json.find(marker);
  if (pos == std::string::npos) {
    return 0;
  }
  pos += marker.size();
  while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  return std::stoi(json.substr(pos));
}

std::size_t offset_for_position(const std::string& text, int line, int character) {
  std::size_t offset = 0;
  int current_line = 0;
  while (offset < text.size() && current_line < line) {
    if (text[offset++] == '\n') {
      ++current_line;
    }
  }
  return std::min(text.size(), offset + static_cast<std::size_t>(character));
}

std::string lsp_write_response(const std::string& id, const std::string& result) {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}";
}

std::string semantic_token_data(const std::string& text) {
  static const std::unordered_map<std::string, int> type_ids = {
      {"keyword", 0},       {"function", 1},      {"operator", 2},
      {"number", 3},        {"string", 4},        {"enumMember", 5},
      {"property", 6},      {"variable", 7},      {"decorator", 8},
      {"punctuation", 9},   {"comment", 10},
      {"binder_head", 1},   {"binder_var", 7},    {"free_var", 7},
      {"function_call", 1}, {"meta_keyword", 8},  {"special_constant", 5}};
  std::vector<facet::SemanticToken> toks;
  try {
    toks = facet::semantic_tokens(text);
  } catch (const facet::Error&) {
    return "{\"data\":[]}";
  }
  std::ostringstream out;
  out << "{\"data\":[";
  int prev_line = 0;
  int prev_start = 0;
  for (std::size_t i = 0; i < toks.size(); ++i) {
    int line = 0;
    int col = 0;
    for (std::size_t j = 0; j < toks[i].offset && j < text.size(); ++j) {
      if (text[j] == '\n') {
        ++line;
        col = 0;
      } else {
        ++col;
      }
    }
    int delta_line = line - prev_line;
    int delta_start = delta_line == 0 ? col - prev_start : col;
    int type = 7;
    auto found = type_ids.find(toks[i].type);
    if (found != type_ids.end()) {
      type = found->second;
    }
    int mods = 0;
    for (const auto& mod : toks[i].modifiers) {
      if (mod == "declaration")   { mods |= 1; }
      if (mod == "defaultLibrary") { mods |= 2; }
      if (mod == "documentation") { mods |= 4; }
    }
    if (i) {
      out << ",";
    }
    out << delta_line << "," << delta_start << "," << toks[i].length << ","
        << type << "," << mods;
    prev_line = line;
    prev_start = col;
  }
  out << "]}";
  return out.str();
}

std::string lsp_completions(const std::string& text, std::size_t offset) {
  std::vector<facet::CompletionItem> items;
  try {
    items = facet::completions(text, offset);
  } catch (const facet::Error&) {
    return "{\"isIncomplete\":false,\"items\":[]}";
  }
  std::ostringstream out;
  out << "{\"isIncomplete\":false,\"items\":[";
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i) {
      out << ",";
    }
    out << "{\"label\":\"" << json_escape(items[i].label)
        << "\",\"detail\":\"" << json_escape(items[i].detail)
        << "\",\"documentation\":\"" << json_escape(items[i].documentation)
        << "\"}";
  }
  out << "]}";
  return out.str();
}

std::string lsp_hover(const std::string& text, std::size_t offset) {
  try {
    facet::Arena arena;
    facet::HoverInfo info = facet::hover(arena, text, offset);
    return "{\"contents\":{\"kind\":\"markdown\",\"value\":\"```facet\\n" +
           json_escape(info.surface) + "\\n```\\n```core\\n" +
           json_escape(info.core) + "\\n```\\nLaTeX: `" +
           json_escape(info.latex) + "`\"}}";
  } catch (const facet::Error&) {
    return "null";
  }
}

std::string lsp_signature(const std::string& text, std::size_t offset) {
  try {
  facet::SignatureHelp sig = facet::signature_help(text, offset);
  std::ostringstream out;
  out << "{\"signatures\":[{\"label\":\"" << json_escape(sig.head) << "(";
  for (std::size_t i = 0; i < sig.parameters.size(); ++i) {
    if (i) {
      out << ", ";
    }
    out << json_escape(sig.parameters[i]);
  }
  out << ")\",\"documentation\":\"" << json_escape(sig.documentation)
      << "\",\"parameters\":[";
  for (std::size_t i = 0; i < sig.parameters.size(); ++i) {
    if (i) {
      out << ",";
    }
    out << "{\"label\":\"" << json_escape(sig.parameters[i]) << "\"}";
  }
  out << "]}],\"activeSignature\":0,\"activeParameter\":"
      << sig.active_parameter << "}";
  return out.str();
  } catch (const facet::Error&) {
    return "{\"signatures\":[],\"activeSignature\":0,\"activeParameter\":0}";
  }
}

std::string lsp_diagnostics(const std::string& text) {
  // Directives and comment-only cells are not surface expressions — no errors.
  if (facet::is_kernel_directive(text) || facet::is_blank_or_comment(text)) {
    return "{\"kind\":\"full\",\"items\":[]}";
  }
  try {
    facet::Arena arena;
    facet::Ref ref = facet::read_surface(arena, text);
    auto diagnostics = facet::validate(ref);
    std::ostringstream out;
    out << "{\"kind\":\"full\",\"items\":[";
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
      if (i) {
        out << ",";
      }
      out << "{\"severity\":2,\"code\":\"" << json_escape(diagnostics[i].code)
          << "\",\"message\":\"" << json_escape(diagnostics[i].message)
          << "\",\"range\":{\"start\":{\"line\":0,\"character\":0},"
             "\"end\":{\"line\":0,\"character\":1}}}";
    }
    out << "]}";
    return out.str();
  } catch (const facet::Error& error) {
    return "{\"kind\":\"full\",\"items\":[{\"severity\":1,\"message\":\"" +
           json_escape(error.what()) +
           "\",\"range\":{\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":0,\"character\":1}}}]}";
  }
}

void write_lsp_message(const std::string& payload) {
  std::cout << "Content-Length: " << payload.size()
            << "\r\n\r\n" << payload << std::flush;
}

int run_lsp() {
  std::unordered_map<std::string, std::string> documents;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("Content-Length:", 0) != 0) {
      continue;
    }
    std::size_t length = static_cast<std::size_t>(
        std::stoull(line.substr(std::string("Content-Length:").size())));
    while (std::getline(std::cin, line)) {
      if (line == "\r" || line.empty()) {
        break;
      }
    }
    std::string body(length, '\0');
    std::cin.read(body.data(), static_cast<std::streamsize>(length));
    std::string method = json_string_field(body, "method");
    std::string id = json_id(body);
    std::cerr << "[facet-lsp] " << method << "\n" << std::flush;
    std::string result;
    if (method == "initialize") {
      result =
          "{\"capabilities\":{\"textDocumentSync\":2,\"semanticTokensProvider\":"
          "{\"legend\":{\"tokenTypes\":[\"keyword\",\"function\",\"operator\","
          "\"number\",\"string\",\"enumMember\",\"property\",\"variable\","
          "\"decorator\",\"punctuation\",\"comment\"],\"tokenModifiers\":"
          "[\"declaration\",\"defaultLibrary\",\"documentation\"]},"
          "\"full\":true,\"range\":true},"
          "\"completionProvider\":{\"triggerCharacters\":[\"[\",\"(\",\"{\","
          "\"@\"]},\"hoverProvider\":true,"
          "\"signatureHelpProvider\":{\"triggerCharacters\":[\"(\",\",\",\"[\"]},"
          "\"diagnosticProvider\":{\"interFileDependencies\":false,"
          "\"workspaceDiagnostics\":false}}}";
    } else if (method == "textDocument/didOpen" ||
               method == "textDocument/didChange") {
      std::string uri = json_string_field(body, "uri");
      std::string text = json_string_field(body, "text");
      if (!uri.empty()) {
        documents[uri] = text;
      }
      continue;
    } else {
      std::string uri = json_string_field(body, "uri");
      std::string text = documents[uri];
      int line_no = json_int_field(body, "line");
      int character = json_int_field(body, "character");
      std::size_t offset = offset_for_position(text, line_no, character);
      if (method == "textDocument/semanticTokens/full" ||
          method == "textDocument/semanticTokens/range") {
        result = semantic_token_data(text);
      } else if (method == "textDocument/completion") {
        result = lsp_completions(text, offset);
      } else if (method == "textDocument/hover") {
        result = lsp_hover(text, offset);
      } else if (method == "textDocument/signatureHelp") {
        result = lsp_signature(text, offset);
      } else if (method == "textDocument/diagnostic") {
        result = lsp_diagnostics(text);
      } else {
        result = "null";
      }
    }
    if (!id.empty()) {
      write_lsp_message(lsp_write_response(id, result));
    }
  }
  return 0;
}

std::string format_coverage(const facet::Coverage& coverage) {
  std::ostringstream out;
  out << "coverage: " << coverage.supported << "/" << coverage.total
      << " supported[kernel=" << coverage.kernel << "]";
  for (const auto& missing : coverage.missing) {
    out << "\nunmapped: " << missing.path << " " << missing.head
        << " kernel=" << missing.kernel;
  }
  return out.str();
}

std::string format_compare(const facet::CompareResult& result) {
  std::ostringstream out;
  out << "agreement: " << result.status << "[by=" << result.by
      << ", strength=" << result.strength << ", detail=" << result.detail;
  if (result.by == "numeric") {
    out << ", tol=" << result.tol << ", samples=" << result.samples;
  }
  if (!result.witness.empty()) {
    out << ", witness=" << result.witness;
  }
  out << "]";
  return out.str();
}

} // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--lsp") {
      return run_lsp();
    }
  }
  std::string read = "surface";
  std::string emit = "core";
  std::string compare_expr;
  std::string compare_by = "structural";
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (auto value = option_value(arg, "read"); !value.empty()) {
      read = value;
    } else if (auto value = option_value(arg, "emit"); !value.empty()) {
      emit = value;
    } else if (auto value = option_value(arg, "compare"); !value.empty()) {
      compare_expr = value;
    } else if (auto value = option_value(arg, "by"); !value.empty()) {
      compare_by = value;
    } else {
      usage();
      return 2;
    }
  }

  std::string input((std::istreambuf_iterator<char>(std::cin)),
                    std::istreambuf_iterator<char>());

  try {
    if (emit == "semantic-tokens") {
      std::cout << format_semantic_tokens(facet::semantic_tokens(input))
                << "\n";
      return 0;
    }
    if (emit == "directive") {
      std::cout << format_directive(facet::read_kernel_directive(input))
                << "\n";
      return 0;
    }
    if (emit.rfind("completions:", 0) == 0) {
      std::cout << format_completions(
                       facet::completions(input, mode_offset(emit, "completions:")))
                << "\n";
      return 0;
    }

    facet::Arena arena;
    if (emit.rfind("hover:", 0) == 0) {
      std::cout << format_hover(
                       facet::hover(arena, input, mode_offset(emit, "hover:")))
                << "\n";
      return 0;
    }
    if (emit.rfind("signature:", 0) == 0) {
      std::cout << format_signature(
                       facet::signature_help(input, mode_offset(emit, "signature:")))
                << "\n";
      return 0;
    }
    facet::Ref expr = nullptr;
    if (read == "surface") {
      expr = facet::read_surface(arena, input);
    } else if (read == "strict") {
      expr = facet::read_strict(arena, input);
    } else if (read == "core") {
      expr = facet::read_core(arena, input);
    } else if (read == "object") {
      expr = facet::read_object(arena, input);
    } else if (read == "sympy-srepr" || read == "source:sympy-srepr") {
      expr = facet::read_sympy_srepr(arena, input);
    } else {
      usage();
      return 2;
    }

    if (read == "surface") {
      for (const auto& diagnostic : facet::validate(expr)) {
        std::cerr << "facet warning [" << diagnostic.code
                  << "]: " << diagnostic.message << "\n";
      }
    }

    if (!compare_expr.empty()) {
      facet::Ref rhs = nullptr;
      if (read == "surface") {
        rhs = facet::read_surface(arena, compare_expr);
      } else if (read == "strict") {
        rhs = facet::read_strict(arena, compare_expr);
      } else if (read == "core") {
        rhs = facet::read_core(arena, compare_expr);
      } else if (read == "object") {
        rhs = facet::read_object(arena, compare_expr);
      } else {
        throw facet::Error("compare is not available for read mode: " + read);
      }
      std::cout << format_compare(facet::compare(arena, expr, rhs, compare_by))
                << "\n";
      return 0;
    }

    if (emit == "diagnostics") {
      std::cout << format_diagnostics(facet::validate(expr)) << "\n";
    } else if (emit == "surface") {
      std::cout << facet::print_surface(expr) << "\n";
    } else if (emit == "strict") {
      std::cout << facet::print_strict(expr) << "\n";
    } else if (emit == "core") {
      std::cout << facet::print_core(expr) << "\n";
    } else if (emit == "object") {
      std::cout << facet::print_object(expr) << "\n";
    } else if (emit == "latex") {
      std::cout << facet::print_latex(expr) << "\n";
    } else if (emit == "render:svg") {
      std::cout << facet::render_svg(expr) << "\n";
    } else if (emit == "render:pdf") {
      std::cout << facet::render_pdf(expr);
    } else if (emit == "render:png") {
      std::cout << facet::render_png(expr) << "\n";
    } else if (emit == "render:html") {
      std::cout << facet::render_html(expr) << "\n";
    } else if (emit.rfind("coverage:", 0) == 0) {
      std::cout << format_coverage(facet::coverage(expr, emit.substr(9)))
                << "\n";
    } else if (emit.rfind("source:", 0) == 0 &&
               emit != "source:sympy-srepr" && emit != "source:sympy-core") {
      std::cout << facet::print_source(expr, emit.substr(7)) << "\n";
    } else if (emit == "sympy" || emit == "source:sympy") {
      std::cout << facet::print_sympy(expr) << "\n";
    } else if (emit == "sympy-srepr" || emit == "source:sympy-srepr") {
      std::cout << facet::print_sympy_srepr(expr) << "\n";
    } else if (emit == "sympy-core" || emit == "source:sympy-core") {
      std::cout << facet::print_core(facet::evaluate_sympy(arena, expr)) << "\n";
    } else {
      usage();
      return 2;
    }
  } catch (const facet::Error& error) {
    std::cerr << "facet: " << error.what() << "\n";
    return 1;
  }
}
