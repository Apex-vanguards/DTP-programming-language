#include "../include/codegen.hpp"
#include "../include/dtp.hpp"
#include "../include/lexer.hpp"
#include "../include/parser.hpp"
#include "../include/typechecker.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

static bool use_color = true;
static const char *RED = "\033[1;31m";
static const char *YELLOW = "\033[1;33m";
static const char *CYAN = "\033[1;36m";
static const char *BOLD = "\033[1m";
static const char *RESET = "\033[0m";

static void print_diag(const dtp::Diagnostic &d, const std::string &src,
                       const std::string &file) {
  const char *col =
      d.level == dtp::ErrLevel::Error || d.level == dtp::ErrLevel::Fatal
          ? RED
          : YELLOW;
  if (!use_color)
    col = "";
  const char *lvl = d.level == dtp::ErrLevel::Error   ? "error"
                    : d.level == dtp::ErrLevel::Fatal ? "fatal"
                    : d.level == dtp::ErrLevel::Warn  ? "warning"
                                                      : "note";
  std::cerr << (use_color ? BOLD : "") << file << ":" << d.line << ":" << d.col
            << ": " << col << lvl << ": " << (use_color ? RESET : "")
            << (use_color ? BOLD : "") << d.msg << (use_color ? RESET : "")
            << "\n";

  if (d.line > 0 && !src.empty()) {
    std::istringstream ss(src);
    std::string line;
    uint32_t ln = 1;
    while (std::getline(ss, line)) {
      if (ln == d.line) {
        std::cerr << "  " << line << "\n";
        if (d.col > 0) {
          std::cerr << "  ";
          for (uint32_t i = 1; i < d.col; i++)
            std::cerr << " ";
          std::cerr << (use_color ? "\033[1;32m" : "") << "^"
                    << (use_color ? RESET : "") << "\n";
        }
        break;
      }
      ln++;
    }
  }
  if (!d.hint.empty())
    std::cerr << "  " << (use_color ? CYAN : "")
              << "hint: " << (use_color ? RESET : "") << d.hint << "\n";
}

// Returns stdlib directory: ~/.dtp/stdlib  or  /usr/local/share/dtp/stdlib
static std::vector<fs::path> stdlib_search_paths() {
  std::vector<fs::path> paths;
  const char *home = getenv("HOME");
  if (home) {
    paths.push_back(fs::path(home) / ".dtp" / "stdlib");
    paths.push_back(fs::path(home) / ".dtp" / "lib");
  }
  paths.push_back("/usr/local/share/dtp/stdlib");
  paths.push_back("/usr/share/dtp/stdlib");
  const char *dtppath = getenv("DTP_PATH");
  if (dtppath) {
    std::istringstream ss(dtppath);
    std::string p;
    while (std::getline(ss, p, ':'))
      if (!p.empty())
        paths.push_back(p);
  }
  return paths;
}

// Collect `use foo.dtp;` style imports from source text (fast pre-scan)
static std::vector<std::string> collect_use_imports(const std::string &src) {
  std::vector<std::string> result;
  std::istringstream ss(src);
  std::string line;
  while (std::getline(ss, line)) {
    // trim
    size_t s = line.find_first_not_of(" \t");
    if (s == std::string::npos)
      continue;
    line = line.substr(s);
    if (line.rfind("use ", 0) != 0)
      continue;
    // find content between 'use ' and ';'
    size_t end = line.find(';');
    std::string mod =
        line.substr(4, end == std::string::npos ? std::string::npos : end - 4);
    // trim
    size_t ms = mod.find_first_not_of(" \t");
    size_t me = mod.find_last_not_of(" \t");
    if (ms == std::string::npos)
      continue;
    mod = mod.substr(ms, me - ms + 1);
    // only collect if it ends in .dtp (file-style import)
    if (mod.size() > 4 && mod.substr(mod.size() - 4) == ".dtp") {
      result.push_back(mod);
    }
  }
  return result;
}

struct Options {
  std::string input;
  std::vector<std::string> extra_sources; // additional .dtp files on CLI
  std::string output;
  std::string c_output;
  bool emit_c = false;
  bool check_only = false;
  bool verbose = false;
  std::string opt = "-O2";
  std::string cc = "gcc";
  std::vector<std::string> link_flags;
  std::vector<std::string> cc_flags;
};

static void usage(const char *argv0) {
  std::cout << "DTP Compiler v0.2\n"
               "Usage: "
            << argv0
            << " <file.dtp> [lib1.dtp lib2.dtp ...] [options]\n\n"
               "Options:\n"
               "  -o <out>         Output binary (default: a.out)\n"
               "  --emit-c         Print generated C source to stdout\n"
               "  -c <file.c>      Write generated C to file\n"
               "  --check          Type-check only, no codegen\n"
               "  -O0/-O1/-O2/-O3  Optimization level (default: -O2)\n"
               "  --cc <compiler>  C compiler to use (default: gcc)\n"
               "  -l<lib>          Link with library\n"
               "  -v               Verbose output\n"
               "  --no-color       Disable colored output\n"
               "  --help           Show this help\n\n"
               "Multi-file example:\n"
               "  dtpc main.dtp math.dtp -o main\n\n"
               "Standard library (auto-resolved from ~/.dtp/stdlib/):\n"
               "  use math.dtp;   // resolved automatically\n"
               "  use io.dtp;\n";
}

// Compile one .dtp source string into a C string, merging into combined module.
// Returns false on error.
static bool compile_source(const std::string &src, const std::string &filename,
                           dtp::Module &mod, bool verbose) {
  if (verbose)
    std::cerr << "[dtpc] lexing " << filename << " ...\n";
  dtp::Lexer lexer(src, filename);
  auto tokens = lexer.tokenize();
  bool had_error = false;
  for (auto &d : lexer.diagnostics) {
    print_diag(d, src, filename);
    if (d.level == dtp::ErrLevel::Error || d.level == dtp::ErrLevel::Fatal)
      had_error = true;
  }
  if (had_error)
    return false;

  if (verbose)
    std::cerr << "[dtpc] parsing " << filename << " ...\n";
  std::string modname = fs::path(filename).stem().string();
  dtp::Parser parser(std::move(tokens), filename);
  auto submod = parser.parse_module(modname);
  for (auto &d : parser.diagnostics) {
    print_diag(d, src, filename);
    if (d.level == dtp::ErrLevel::Error || d.level == dtp::ErrLevel::Fatal)
      had_error = true;
  }
  if (had_error)
    return false;

  // Merge items into main module
  for (auto &item : submod.items) {
    mod.items.push_back(std::move(item));
  }
  return true;
}

int main(int argc, char **argv) {
  Options opts;
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      usage(argv[0]);
      return 0;
    } else if (a == "-o" && i + 1 < argc)
      opts.output = argv[++i];
    else if (a == "--emit-c")
      opts.emit_c = true;
    else if (a == "-c" && i + 1 < argc)
      opts.c_output = argv[++i];
    else if (a == "--check")
      opts.check_only = true;
    else if (a == "-O0" || a == "-O1" || a == "-O2" || a == "-O3" || a == "-Os")
      opts.opt = a;
    else if (a == "--cc" && i + 1 < argc)
      opts.cc = argv[++i];
    else if (a == "-v")
      opts.verbose = true;
    else if (a == "--no-color") {
      use_color = false;
      RED = YELLOW = CYAN = BOLD = RESET = "";
    } else if (a.substr(0, 2) == "-l")
      opts.link_flags.push_back(a);
    else if (a.substr(0, 2) == "-f")
      opts.cc_flags.push_back(a);
    else if (a[0] != '-') {
      if (opts.input.empty())
        opts.input = a;
      else
        opts.extra_sources.push_back(a);
    } else {
      std::cerr << "Unknown option: " << a << "\n";
      return 1;
    }
  }

  if (opts.input.empty()) {
    std::cerr << "No input file.\n";
    return 1;
  }

  auto stdlib_paths = stdlib_search_paths();

  // Resolve a module name / filename to a real path.
  // Search order: same dir as main file, then stdlib paths.
  auto resolve_module = [&](const std::string &name,
                            const fs::path &relative_to) -> std::string {
    // Try relative to caller
    fs::path rel = relative_to.parent_path() / name;
    if (fs::exists(rel))
      return rel.string();
    // Try stdlib
    for (auto &sp : stdlib_paths) {
      fs::path p = sp / name;
      if (fs::exists(p))
        return p.string();
    }
    return "";
  };

  // Read main source
  std::ifstream f(opts.input);
  if (!f) {
    std::cerr << "Cannot open file: " << opts.input << "\n";
    return 1;
  }
  std::string main_src((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());

  std::string modname = fs::path(opts.input).stem().string();
  if (opts.output.empty())
    opts.output = modname;

  // Collect all source files to compile (BFS/DFS through use imports)
  std::vector<std::pair<std::string, std::string>> to_compile; // (path, source)
  std::set<std::string> seen;

  // Helper: queue a file for compilation and scan its imports
  std::function<void(const std::string &, const std::string &)> queue_file;
  queue_file = [&](const std::string &path, const std::string &src) {
    if (seen.count(path))
      return;
    seen.insert(path);
    // Scan use imports before pushing so deps come first
    auto imports = collect_use_imports(src);
    for (auto &imp : imports) {
      std::string resolved = resolve_module(imp, fs::path(path));
      if (resolved.empty()) {
        if (opts.verbose)
          std::cerr << "[dtpc] note: cannot resolve '" << imp
                    << "' (stdlib not installed?)\n";
        continue;
      }
      if (!seen.count(resolved)) {
        std::ifstream rf(resolved);
        if (rf) {
          std::string rsrc((std::istreambuf_iterator<char>(rf)),
                           std::istreambuf_iterator<char>());
          queue_file(resolved, rsrc);
        }
      }
    }
    to_compile.push_back({path, src});
  };

  queue_file(opts.input, main_src);

  // Extra .dtp files given on command line
  for (auto &es : opts.extra_sources) {
    std::ifstream ef(es);
    if (!ef) {
      std::cerr << "Cannot open: " << es << "\n";
      return 1;
    }
    std::string esrc((std::istreambuf_iterator<char>(ef)),
                     std::istreambuf_iterator<char>());
    queue_file(es, esrc);
  }

  // Build combined module
  dtp::Module combined;
  combined.name = modname;

  bool had_error = false;
  for (auto &[path, src] : to_compile) {
    if (!compile_source(src, path, combined, opts.verbose))
      had_error = true;
  }
  if (had_error)
    return 1;

  // Type check
  if (opts.verbose)
    std::cerr << "[dtpc] type checking...\n";
  dtp::TypeChecker tc;
  tc.check(combined);
  for (auto &d : tc.diagnostics) {
    print_diag(d, main_src, opts.input);
    if (d.level == dtp::ErrLevel::Error || d.level == dtp::ErrLevel::Fatal)
      had_error = true;
  }
  if (had_error || opts.check_only)
    return had_error ? 1 : 0;

  // Code generation
  if (opts.verbose)
    std::cerr << "[dtpc] generating C...\n";
  dtp::CodeGen cg;
  std::string c_code = cg.generate(combined);
  for (auto &d : cg.diagnostics) {
    print_diag(d, main_src, opts.input);
    if (d.level == dtp::ErrLevel::Error || d.level == dtp::ErrLevel::Fatal)
      had_error = true;
  }
  if (had_error)
    return 1;

  if (opts.emit_c && opts.c_output.empty()) {
    std::cout << c_code;
    return 0;
  }

  std::string c_file = opts.c_output.empty()
                           ? (std::string("/tmp/__dtp_") + modname + ".c")
                           : opts.c_output;

  {
    std::ofstream of(c_file);
    if (!of) {
      std::cerr << "Cannot write C file: " << c_file << "\n";
      return 1;
    }
    of << c_code;
  }
  if (opts.verbose)
    std::cerr << "[dtpc] wrote C to " << c_file << "\n";

  if (!opts.c_output.empty() && opts.emit_c)
    return 0;

  std::string cmd = opts.cc + " " + opts.opt + " -std=c11" + " -Wall -Wextra" +
                    " -ffast-math" + " -funroll-loops" + " -march=native" +
                    " -pipe" + " " + c_file + " -o " + opts.output + " -lm";

  for (auto &lf : opts.link_flags)
    cmd += " " + lf;
  for (auto &cf : opts.cc_flags)
    cmd += " " + cf;

  if (opts.verbose)
    std::cerr << "[dtpc] compiling: " << cmd << "\n";

  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    std::cerr << (use_color ? RED : "") << "error: " << (use_color ? RESET : "")
              << "compilation failed (exit " << ret << ")\n";
    return 1;
  }

  if (opts.verbose)
    std::cerr << "[dtpc] done -> " << opts.output << "\n";

  if (opts.c_output.empty()) {
    fs::remove(c_file);
  }
  return 0;
}
