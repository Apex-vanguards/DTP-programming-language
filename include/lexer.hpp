#pragma once
#include "dtp.hpp"
#include <stdexcept>
#include <cctype>
#include <unordered_map>

namespace dtp {

class Lexer {
public:
    std::vector<Diagnostic> diagnostics;

    explicit Lexer(std::string src, std::string filename = "<input>", uint32_t file_id = 0)
        : src_(std::move(src)), filename_(std::move(filename)),
          file_id_(file_id), pos_(0), line_(1), col_(1) {}

    std::vector<Token> tokenize() {
        std::vector<Token> toks;
        toks.reserve(src_.size() / 4);
        while (true) {
            skip_ws_and_comments();
            if (pos_ >= src_.size()) {
                toks.push_back({TK::EOF_TOK, "", line_, col_, file_id_});
                break;
            }
            toks.push_back(next_token());
        }
        return toks;
    }

private:
    std::string  src_;
    std::string  filename_;
    uint32_t     file_id_;
    size_t       pos_;
    uint32_t     line_, col_;

    char cur()  const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char peek(int n=1) const {
        size_t p = pos_ + n;
        return p < src_.size() ? src_[p] : '\0';
    }

    char advance() {
        char c = src_[pos_++];
        if (c == '\n') { line_++; col_ = 1; }
        else col_++;
        return c;
    }

    bool match(char c) {
        if (cur() == c) { advance(); return true; }
        return false;
    }

    void skip_ws_and_comments() {
        while (pos_ < src_.size()) {
            char c = cur();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance(); continue;
            }
            // Line comment //
            if (c == '/' && peek() == '/') {
                while (pos_ < src_.size() && cur() != '\n') advance();
                continue;
            }
            // Block comment /* */
            if (c == '/' && peek() == '*') {
                advance(); advance();
                while (pos_ < src_.size()) {
                    if (cur() == '*' && peek() == '/') { advance(); advance(); break; }
                    advance();
                }
                continue;
            }
            break;
        }
    }

    Token make(TK t, std::string v = "") {
        return {t, std::move(v), line_, col_, file_id_};
    }

    Token next_token() {
        uint32_t sl = line_, sc = col_;
        char c = advance();

        // Numbers
        if (std::isdigit(c) || (c == '0' && (cur() == 'x' || cur() == 'b' || cur() == 'o'))) {
            return lex_number(c, sl, sc);
        }

        // Identifiers / keywords
        if (std::isalpha(c) || c == '_') {
            return lex_ident(c, sl, sc);
        }

        // Strings
        if (c == '"') return lex_string(sl, sc);
        if (c == '\'') return lex_char(sl, sc);

        // Operators & punctuation
        Token t{TK::EOF_TOK, std::string(1,c), sl, sc, file_id_};
        switch(c) {
            case '+': if(match('=')){t.type=TK::PLUS_ASSIGN;  t.value="+=";} else t.type=TK::PLUS;  break;
            case '-': if(match('>')){t.type=TK::ARROW;        t.value="->";}
                      else if(match('=')){t.type=TK::MINUS_ASSIGN;t.value="-=";}
                      else t.type=TK::MINUS; break;
            case '*': if(match('=')){t.type=TK::STAR_ASSIGN;  t.value="*=";} else t.type=TK::STAR;  break;
            case '/': if(match('=')){t.type=TK::SLASH_ASSIGN; t.value="/=";} else t.type=TK::SLASH; break;
            case '%': t.type = TK::PERCENT; break;
            case '&': if(match('=')){t.type=TK::AMP_ASSIGN;   t.value="&=";} else t.type=TK::AMP;   break;
            case '|': if(match('=')){t.type=TK::PIPE_ASSIGN;  t.value="|=";} else t.type=TK::PIPE;  break;
            case '^': if(match('=')){t.type=TK::CARET_ASSIGN; t.value="^=";} else t.type=TK::CARET; break;
            case '~': t.type = TK::TILDE; break;
            case '<': if(cur()=='<'){advance();t.type=TK::LSHIFT;t.value="<<";}
                      else if(match('=')){t.type=TK::LE;t.value="<=";}
                      else t.type=TK::LT; break;
            case '>': if(cur()=='>'){advance();t.type=TK::RSHIFT;t.value=">>";}
                      else if(match('=')){t.type=TK::GE;t.value=">=";}
                      else t.type=TK::GT; break;
            case '=': if(cur()=='>'){advance();t.type=TK::FAT_ARROW;t.value="=>";}
                      else if(match('=')){t.type=TK::EQ;t.value="==";}
                      else t.type=TK::ASSIGN; break;
            case '!': if(match('=')){t.type=TK::NEQ;t.value="!=";} else t.type=TK::BANG; break;
            case '.': if(cur()=='.'){advance();
                        if(cur()=='.'){advance();t.type=TK::DOTS;t.value="...";}
                        else{t.type=TK::DOTDOT;t.value="..";}}
                      else t.type=TK::DOT; break;
            case '?': t.type = TK::QUESTION; break;
            case ':': if(match(':')){t.type=TK::DCOLON;t.value="::";} else t.type=TK::COLON; break;
            case ';': t.type = TK::SEMICOLON; break;
            case ',': t.type = TK::COMMA;     break;
            case '(': t.type = TK::LPAREN;    break;
            case ')': t.type = TK::RPAREN;    break;
            case '{': t.type = TK::LBRACE;    break;
            case '}': t.type = TK::RBRACE;    break;
            case '[': t.type = TK::LBRACKET;  break;
            case ']': t.type = TK::RBRACKET;  break;
            case '@': t.type = TK::AT;        break;
            case '#': t.type = TK::HASH;      break;
            case '$': t.type = TK::DOLLAR;    break;
            default:
                diagnostics.push_back({ErrLevel::Error,
                    std::string("Unknown character: ") + c,
                    sl, sc, filename_, ""});
                break;
        }
        return t;
    }

    Token lex_number(char first, uint32_t sl, uint32_t sc) {
        std::string val(1, first);
        bool is_float = false;

        // Hex / Binary / Octal
        if (first == '0') {
            if (cur() == 'x' || cur() == 'X') {
                val += advance();
                while (std::isxdigit(cur()) || cur()=='_') {
                    if (cur()!='_') val += cur(); advance();
                }
                return {TK::INT, val, sl, sc, file_id_};
            }
            if (cur() == 'b' || cur() == 'B') {
                val += advance();
                while (cur()=='0'||cur()=='1'||cur()=='_') {
                    if (cur()!='_') val += cur(); advance();
                }
                return {TK::INT, val, sl, sc, file_id_};
            }
            if (cur() == 'o' || cur() == 'O') {
                val += advance();
                while ((cur()>='0'&&cur()<='7')||cur()=='_') {
                    if (cur()!='_') val += cur(); advance();
                }
                return {TK::INT, val, sl, sc, file_id_};
            }
        }

        while (std::isdigit(cur()) || cur()=='_') {
            if (cur()!='_') val += cur(); advance();
        }
        if (cur()=='.' && peek()!='.' && std::isdigit(peek())) {
            is_float = true;
            val += advance();
            while (std::isdigit(cur()) || cur()=='_') {
                if (cur()!='_') val += cur(); advance();
            }
        }
        if (cur()=='e' || cur()=='E') {
            is_float = true;
            val += advance();
            if (cur()=='+' || cur()=='-') val += advance();
            while (std::isdigit(cur())) val += advance();
        }
        // Optional suffix: i32 u64 f32 etc.
        if (std::isalpha(cur())) {
            std::string suffix;
            while (std::isalnum(cur())) suffix += advance();
            val += suffix;
        }
        return {is_float ? TK::FLOAT : TK::INT, val, sl, sc, file_id_};
    }

    Token lex_ident(char first, uint32_t sl, uint32_t sc) {
        std::string val(1, first);
        while (std::isalnum(cur()) || cur()=='_') val += advance();

        static const std::unordered_map<std::string, TK> kw = {
            {"fn",TK::KW_fn},{"ret",TK::KW_ret},{"let",TK::KW_let},
            {"mut",TK::KW_mut},{"if",TK::KW_if},{"elif",TK::KW_elif},
            {"else",TK::KW_else},{"loop",TK::KW_loop},{"while",TK::KW_while},
            {"for",TK::KW_for},{"in",TK::KW_in},{"break",TK::KW_break},
            {"next",TK::KW_next},{"struct",TK::KW_struct},{"enum",TK::KW_enum},
            {"impl",TK::KW_impl},{"trait",TK::KW_trait},{"use",TK::KW_use},
            {"pub",TK::KW_pub},{"ext",TK::KW_ext},{"inline",TK::KW_inline},
            {"comptime",TK::KW_comptime},{"new",TK::KW_new},{"drop",TK::KW_drop},
            {"sizeof",TK::KW_sizeof},{"alignof",TK::KW_alignof},
            {"null",TK::KW_null},{"as",TK::KW_as},{"and",TK::KW_and},
            {"or",TK::KW_or},{"not",TK::KW_not},{"defer",TK::KW_defer},
            {"unsafe",TK::KW_unsafe},{"asm",TK::KW_asm},
            {"true",TK::BOOL_TRUE},{"false",TK::BOOL_FALSE},
            // Types
            {"i8",TK::TY_i8},{"i16",TK::TY_i16},{"i32",TK::TY_i32},{"i64",TK::TY_i64},
            {"u8",TK::TY_u8},{"u16",TK::TY_u16},{"u32",TK::TY_u32},{"u64",TK::TY_u64},
            {"f32",TK::TY_f32},{"f64",TK::TY_f64},{"bool",TK::TY_bool},
            {"char",TK::TY_char},{"void",TK::TY_void},{"str",TK::TY_str},
        };
        auto it = kw.find(val);
        if (it != kw.end()) return {it->second, val, sl, sc, file_id_};
        return {TK::IDENT, val, sl, sc, file_id_};
    }

    Token lex_string(uint32_t sl, uint32_t sc) {
        std::string val;
        while (pos_ < src_.size() && cur() != '"') {
            if (cur() == '\\') {
                advance();
                switch(cur()) {
                    case 'n': val+='\n'; break; case 't': val+='\t'; break;
                    case 'r': val+='\r'; break; case '\\': val+='\\'; break;
                    case '"': val+='"';  break; case '0': val+='\0'; break;
                    case 'x': {
                        advance();
                        std::string hex; hex+=advance(); hex+=advance();
                        val += (char)std::stoi(hex,nullptr,16);
                        continue;
                    }
                    default: val += cur();
                }
            } else {
                val += cur();
            }
            advance();
        }
        if (cur()=='"') advance();
        else diagnostics.push_back({ErrLevel::Error,"Unterminated string",sl,sc,filename_});
        return {TK::STRING, val, sl, sc, file_id_};
    }

    Token lex_char(uint32_t sl, uint32_t sc) {
        std::string val;
        if (cur() == '\\') {
            advance();
            switch(cur()) {
                case 'n': val="\n"; break; case 't': val="\t"; break;
                case '\'': val="'"; break; case '\\': val="\\"; break;
                case '0': val=std::string(1,'\0'); break;
                default: val=std::string(1,cur());
            }
        } else { val=std::string(1,cur()); }
        advance();
        if (cur()=='\'') advance();
        else diagnostics.push_back({ErrLevel::Error,"Unterminated char literal",sl,sc,filename_});
        return {TK::CHAR, val, sl, sc, file_id_};
    }
};

} // namespace dtp
