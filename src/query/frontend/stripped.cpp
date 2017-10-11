#include "query/frontend/stripped.hpp"

#include <cctype>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "glog/logging.h"

#include "query/common.hpp"
#include "query/exceptions.hpp"
#include "query/frontend/opencypher/generated/CypherBaseVisitor.h"
#include "query/frontend/opencypher/generated/CypherLexer.h"
#include "query/frontend/opencypher/generated/CypherParser.h"
#include "query/frontend/stripped_lexer_constants.hpp"
#include "utils/hashing/fnv.hpp"
#include "utils/string.hpp"

namespace query {

using namespace lexer_constants;

StrippedQuery::StrippedQuery(const std::string &query) : original_(query) {
  enum class Token {
    UNMATCHED,
    KEYWORD,  // Including true, false and null.
    SPECIAL,  // +, .., +=, (, { and so on.
    STRING,
    INT,  // Decimal, octal and hexadecimal.
    REAL,
    PARAMETER,
    ESCAPED_NAME,
    UNESCAPED_NAME,
    SPACE
  };

  std::vector<std::pair<Token, std::string>> tokens;
  for (int i = 0; i < static_cast<int>(original_.size());) {
    Token token = Token::UNMATCHED;
    int len = 0;
    auto update = [&](int new_len, Token new_token) {
      if (new_len > len) {
        len = new_len;
        token = new_token;
      }
    };
    update(MatchKeyword(i), Token::KEYWORD);
    update(MatchSpecial(i), Token::SPECIAL);
    update(MatchString(i), Token::STRING);
    update(MatchDecimalInt(i), Token::INT);
    update(MatchOctalInt(i), Token::INT);
    update(MatchHexadecimalInt(i), Token::INT);
    update(MatchReal(i), Token::REAL);
    update(MatchParameter(i), Token::PARAMETER);
    update(MatchEscapedName(i), Token::ESCAPED_NAME);
    update(MatchUnescapedName(i), Token::UNESCAPED_NAME);
    update(MatchWhitespaceAndComments(i), Token::SPACE);
    if (token == Token::UNMATCHED) throw LexingException("Invalid query");
    tokens.emplace_back(token, original_.substr(i, len));
    i += len;
  }

  std::vector<std::string> token_strings;
  // A helper function that stores literal and its token position in a
  // literals_. In stripped query text literal is replaced with a new_value.
  // new_value can be any value that is lexed as a literal.
  auto replace_stripped = [this, &token_strings](int position,
                                                 const TypedValue &value,
                                                 const std::string &new_value) {
    literals_.Add(position, value);
    token_strings.push_back(new_value);
  };

  // Copy original tokens because we need to use original case in named
  // expressions and keywords in tokens will be lowercased in the next loop.
  auto original_tokens = tokens;
  // For every token in original query remember token index in stripped query.
  std::vector<int> position_mapping(tokens.size(), -1);

  // Convert tokens to strings, perform lowercasing and filtering, store
  // literals and nonaliased named expressions in return.
  for (int i = 0; i < static_cast<int>(tokens.size()); ++i) {
    auto &token = tokens[i];
    // Position is calculated in query after stripping and whitespace
    // normalisation, not before. There will be twice as much tokens before
    // this one because space tokens will be inserted between every one we also
    // need to shift token index for every parameter since antlr's parser thinks
    // of parameter as two tokens.
    int token_index = token_strings.size() * 2 + parameters_.size();
    switch (token.first) {
      case Token::UNMATCHED:
        LOG(FATAL) << "Shouldn't happen";
      case Token::KEYWORD: {
        token.second = utils::ToLowerCase(token.second);
        const auto &s = token.second;
        // We don't strip NULL, since it can appear in special expressions
        // like IS NULL and IS NOT NULL, but we strip true and false keywords.
        if (s == "true") {
          replace_stripped(token_index, true, kStrippedBooleanToken);
        } else if (s == "false") {
          replace_stripped(token_index, false, kStrippedBooleanToken);
        } else {
          token_strings.push_back(s);
        }
      } break;
      case Token::SPACE:
        break;
      case Token::STRING:
        replace_stripped(token_index, ParseStringLiteral(token.second),
                         kStrippedStringToken);
        break;
      case Token::INT:
        replace_stripped(token_index, ParseIntegerLiteral(token.second),
                         kStrippedIntToken);
        break;
      case Token::REAL:
        replace_stripped(token_index, ParseDoubleLiteral(token.second),
                         kStrippedDoubleToken);
        break;
      case Token::SPECIAL:
      case Token::ESCAPED_NAME:
      case Token::UNESCAPED_NAME:
        token_strings.push_back(token.second);
        break;
      case Token::PARAMETER:
        parameters_[token_index] = ParseParameter(token.second);
        token_strings.push_back(token.second);
        break;
    }

    if (token.first != Token::SPACE) {
      position_mapping[i] = token_index;
    }
  }

  query_ = utils::Join(token_strings, " ");
  hash_ = fnv(query_);

  // Store nonaliased named expressions in returns in named_exprs_.
  auto it = std::find_if(tokens.begin(), tokens.end(),
                         [](const std::pair<Token, std::string> &a) {
                           return a.second == "return";
                         });
  // There is no RETURN so there is nothing to do here.
  if (it == tokens.end()) return;
  // Skip RETURN;
  ++it;

  // Now we need to parse cypherReturn production from opencypher grammar.
  // Skip leading whitespaces and DISTINCT statemant if there is one.
  while (it != tokens.end() && it->first == Token::SPACE) {
    ++it;
  }
  if (it != tokens.end() && it->second == "distinct") {
    ++it;
  }

  // We assume there is only one return statement and that return statement is
  // the last one. Otherwise, query is invalid and either antlr parser or
  // cypher_main_visitor will report an error.
  // TODO: we shouldn't rely on the fact that those checks will be done
  // after this step. We should do them here.
  while (it < tokens.end()) {
    // Disregard leading whitespace
    while (it != tokens.end() && it->first == Token::SPACE) {
      ++it;
    }
    // There is only whitespace, nothing to do...
    if (it == tokens.end()) break;
    bool has_as = false;
    auto last_non_space = it;
    auto jt = it;
    // We should track number of opened braces and parantheses so that we can
    // recognize if comma is a named expression separator or part of the
    // list literal / function call.
    int num_open_braces = 0;
    int num_open_parantheses = 0;
    for (;
         jt != tokens.end() &&
         (jt->second != "," || num_open_braces || num_open_parantheses) &&
         jt->second != "order" && jt->second != "skip" && jt->second != "limit";
         ++jt) {
      if (jt->second == "(") {
        ++num_open_parantheses;
      } else if (jt->second == ")") {
        --num_open_parantheses;
      } else if (jt->second == "[") {
        ++num_open_braces;
      } else if (jt->second == "]") {
        --num_open_braces;
      }
      has_as |= jt->second == "as";
      if (jt->first != Token::SPACE) {
        last_non_space = jt;
      }
    }
    if (!has_as) {
      // Named expression is not aliased. Save string disregarding leading and
      // trailing whitespaces. Use original_tokens in which case of the keywords
      // is not lowercased.
      std::string s;
      auto begin_token = it - tokens.begin() + original_tokens.begin();
      auto end_token =
          last_non_space - tokens.begin() + original_tokens.begin() + 1;
      for (auto kt = begin_token; kt != end_token; ++kt) {
        s += kt->second;
      }
      named_exprs_[position_mapping[it - tokens.begin()]] = s;
    }
    if (jt != tokens.end() && jt->second == ",") {
      // There are more named expressions.
      it = jt + 1;
    } else {
      // We hit ORDER, SKIP or LIMIT -> we are done.
      break;
    }
  }
}

std::string GetFirstUtf8Symbol(const char *_s) {
  // According to
  // https://stackoverflow.com/questions/16260033/reinterpret-cast-between-char-and-stduint8-t-safe
  // this checks if casting from const char * to uint8_t is undefined behaviour.
  static_assert(std::is_same<std::uint8_t, unsigned char>::value,
                "This library requires std::uint8_t to be implemented as "
                "unsigned char.");
  const uint8_t *s = reinterpret_cast<const uint8_t *>(_s);
  if ((*s >> 7) == 0x00) return std::string(_s, _s + 1);
  if ((*s >> 5) == 0x06) {
    auto *s1 = s + 1;
    if ((*s1 >> 6) != 0x02) throw LexingException("Invalid character");
    return std::string(_s, _s + 2);
  }
  if ((*s >> 4) == 0x0e) {
    auto *s1 = s + 1;
    if ((*s1 >> 6) != 0x02) throw LexingException("Invalid character");
    auto *s2 = s + 2;
    if ((*s2 >> 6) != 0x02) throw LexingException("Invalid character");
    return std::string(_s, _s + 3);
  }
  if ((*s >> 3) == 0x1e) {
    auto *s1 = s + 1;
    if ((*s1 >> 6) != 0x02) throw LexingException("Invalid character");
    auto *s2 = s + 2;
    if ((*s2 >> 6) != 0x02) throw LexingException("Invalid character");
    auto *s3 = s + 3;
    if ((*s3 >> 6) != 0x02) throw LexingException("Invalid character");
    return std::string(_s, _s + 4);
  }
  throw LexingException("Invalid character");
}

// Return codepoint of first utf8 symbol and its encoded length.
std::pair<int, int> GetFirstUtf8SymbolCodepoint(const char *_s) {
  static_assert(std::is_same<std::uint8_t, unsigned char>::value,
                "This library requires std::uint8_t to be implemented as "
                "unsigned char.");
  const uint8_t *s = reinterpret_cast<const uint8_t *>(_s);
  if ((*s >> 7) == 0x00) return {*s & 0x7f, 1};
  if ((*s >> 5) == 0x06) {
    auto *s1 = s + 1;
    if ((*s1 >> 6) != 0x02) throw LexingException("Invalid character");
    return {((*s & 0x1f) << 6) | (*s1 & 0x3f), 2};
  }
  if ((*s >> 4) == 0x0e) {
    auto *s1 = s + 1;
    if ((*s1 >> 6) != 0x02) throw LexingException("Invalid character");
    auto *s2 = s + 2;
    if ((*s2 >> 6) != 0x02) throw LexingException("Invalid character");
    return {((*s & 0x0f) << 12) | ((*s1 & 0x3f) << 6) | (*s2 & 0x3f), 3};
  }
  if ((*s >> 3) == 0x1e) {
    auto *s1 = s + 1;
    if ((*s1 >> 6) != 0x02) throw LexingException("Invalid character");
    auto *s2 = s + 2;
    if ((*s2 >> 6) != 0x02) throw LexingException("Invalid character");
    auto *s3 = s + 3;
    if ((*s3 >> 6) != 0x02) throw LexingException("Invalid character");
    return {((*s & 0x07) << 18) | ((*s1 & 0x3f) << 12) | ((*s2 & 0x3f) << 6) |
                (*s3 & 0x3f),
            4};
  }
  throw LexingException("Invalid character");
}

// From here until end of file there are functions that calculate matches for
// every possible token. Functions are more or less compatible with Cypher.g4
// grammar. Unfortunately, they contain a lof of special cases and shouldn't
// be changed without good reasons.
//
// Here be dragons, do not touch!
//           ____ __
//          { --.\  |          .)%%%)%%
//           '-._\\ | (\___   %)%%(%%(%%%
//               `\\|{/ ^ _)-%(%%%%)%%;%%%
//           .'^^^^^^^  /`    %%)%%%%)%%%'
//          //\   ) ,  /       '%%%%(%%'
//    ,  _.'/  `\<-- \<
//     `^^^`     ^^   ^^
int StrippedQuery::MatchKeyword(int start) const {
  return kKeywords.Match<tolower>(original_.c_str() + start);
}

int StrippedQuery::MatchSpecial(int start) const {
  return kSpecialTokens.Match(original_.c_str() + start);
}

int StrippedQuery::MatchString(int start) const {
  if (original_[start] != '"' && original_[start] != '\'') return 0;
  char start_char = original_[start];
  for (auto *p = original_.data() + start + 1; *p; ++p) {
    if (*p == start_char) return p - (original_.data() + start) + 1;
    if (*p == '\\') {
      ++p;
      if (*p == '\\' || *p == '\'' || *p == '"' || *p == 'B' || *p == 'b' ||
          *p == 'F' || *p == 'f' || *p == 'N' || *p == 'n' || *p == 'R' ||
          *p == 'r' || *p == 'T' || *p == 't') {
        // Allowed escaped characters.
        continue;
      } else if (*p == 'U' || *p == 'u') {
        int cnt = 0;
        auto *r = p + 1;
        while (isxdigit(*r) && cnt < 8) {
          ++cnt;
          ++r;
        }
        if (!*r) return 0;
        if (cnt < 4) return 0;
        if (cnt >= 4 && cnt < 8) {
          p += 4;
        }
        if (cnt >= 8) {
          p += 8;
        }
      } else {
        return 0;
      }
    }
  }
  return 0;
}

int StrippedQuery::MatchDecimalInt(int start) const {
  if (original_[start] == '0') return 1;
  int i = start;
  while (i < static_cast<int>(original_.size()) && isdigit(original_[i])) {
    ++i;
  }
  return i - start;
}

int StrippedQuery::MatchOctalInt(int start) const {
  if (original_[start] != '0') return 0;
  int i = start + 1;
  while (i < static_cast<int>(original_.size()) && '0' <= original_[i] &&
         original_[i] <= '7') {
    ++i;
  }
  if (i == start + 1) return 0;
  return i - start;
}

int StrippedQuery::MatchHexadecimalInt(int start) const {
  if (original_[start] != '0') return 0;
  if (start + 1 >= static_cast<int>(original_.size())) return 0;
  if (original_[start + 1] != 'x') return 0;
  int i = start + 2;
  while (i < static_cast<int>(original_.size()) && isxdigit(original_[i])) {
    ++i;
  }
  if (i == start + 2) return 0;
  return i - start;
}

int StrippedQuery::MatchReal(int start) const {
  enum class State { START, BEFORE_DOT, DOT, AFTER_DOT, E, E_MINUS, AFTER_E };
  State state = State::START;
  auto i = start;
  while (i < static_cast<int>(original_.size())) {
    if (original_[i] == '.') {
      if (state != State::BEFORE_DOT && state != State::START) break;
      state = State::DOT;
    } else if ('0' <= original_[i] && original_[i] <= '9') {
      if (state == State::START) {
        state = State::BEFORE_DOT;
      } else if (state == State::DOT) {
        state = State::AFTER_DOT;
      } else if (state == State::E || state == State::E_MINUS) {
        state = State::AFTER_E;
      }
    } else if (original_[i] == 'e' || original_[i] == 'E') {
      if (state != State::BEFORE_DOT && state != State::AFTER_DOT) break;
      state = State::E;
    } else if (original_[i] == '-') {
      if (state != State::E) break;
      state = State::E_MINUS;
    } else {
      break;
    }
    ++i;
  }
  if (state == State::DOT) --i;
  if (state == State::E) --i;
  if (state == State::E_MINUS) i -= 2;
  return i - start;
}

int StrippedQuery::MatchParameter(int start) const {
  int len = original_.size();
  if (start + 1 == len) return 0;
  if (original_[start] != '$') return 0;
  int max_len = 0;
  max_len = std::max(max_len, MatchUnescapedName(start + 1));
  max_len = std::max(max_len, MatchEscapedName(start + 1));
  max_len = std::max(max_len, MatchKeyword(start + 1));
  max_len = std::max(max_len, MatchDecimalInt(start + 1));
  if (max_len == 0) return 0;
  return 1 + max_len;
}

int StrippedQuery::MatchEscapedName(int start) const {
  int len = original_.size();
  int i = start;
  while (i < len) {
    if (original_[i] != '`') break;
    int j = i + 1;
    while (j < len && original_[j] != '`') {
      ++j;
    }
    if (j == len) break;
    i = j + 1;
  }
  return i - start;
}

int StrippedQuery::MatchUnescapedName(int start) const {
  auto i = start;
  auto got = GetFirstUtf8SymbolCodepoint(original_.data() + i);
  if (got.first >= lexer_constants::kBitsetSize ||
      !kUnescapedNameAllowedStarts[got.first]) {
    return 0;
  }
  i += got.second;
  while (i < static_cast<int>(original_.size())) {
    got = GetFirstUtf8SymbolCodepoint(original_.data() + i);
    if (got.first >= lexer_constants::kBitsetSize ||
        !kUnescapedNameAllowedParts[got.first]) {
      break;
    }
    i += got.second;
  }
  return i - start;
}

int StrippedQuery::MatchWhitespaceAndComments(int start) const {
  enum class State { OUT, IN_LINE_COMMENT, IN_BLOCK_COMMENT };
  State state = State::OUT;
  int i = start;
  int len = original_.size();
  // We need to remember at which position comment started because if we faile
  // to match comment finish we have a match until comment start position.
  int comment_position = -1;
  while (i < len) {
    if (state == State::OUT) {
      auto got = GetFirstUtf8SymbolCodepoint(original_.data() + i);
      if (got.first < lexer_constants::kBitsetSize && kSpaceParts[got.first]) {
        i += got.second;
      } else if (i + 1 < len && original_[i] == '/' &&
                 original_[i + 1] == '*') {
        comment_position = i;
        state = State::IN_BLOCK_COMMENT;
        i += 2;
      } else if (i + 1 < len && original_[i] == '/' &&
                 original_[i + 1] == '/') {
        comment_position = i;
        state = State::IN_LINE_COMMENT;
        i += 2;
      } else {
        break;
      }
    } else if (state == State::IN_LINE_COMMENT) {
      if (original_[i] == '\n') {
        state = State::OUT;
        ++i;
      } else if (i + 1 < len && original_[i] == '\r' &&
                 original_[i + 1] == '\n') {
        state = State::OUT;
        i += 2;
      } else if (original_[i] == '\r') {
        break;
      } else if (i + 1 == len) {
        state = State::OUT;
        ++i;
      } else {
        ++i;
      }
    } else if (state == State::IN_BLOCK_COMMENT) {
      if (i + 1 < len && original_[i] == '*' && original_[i + 1] == '/') {
        i += 2;
        state = State::OUT;
      } else {
        ++i;
      }
    }
  }
  if (state != State::OUT) return comment_position - start;
  return i - start;
}
}  // namespace query
