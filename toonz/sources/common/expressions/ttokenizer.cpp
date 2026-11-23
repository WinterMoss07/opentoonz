

#include "ttokenizer.h"
#include <QString>

namespace TSyntax {

int Token::getIntValue() const {
  return QString::fromStdString(getText()).toInt();
}

double Token::getDoubleValue() const {
  return QString::fromStdString(getText()).toDouble();
}

//===================================================================

Tokenizer::Tokenizer() : m_buffer(), m_index(0) {}

//-------------------------------------------------------------------

Tokenizer::Tokenizer(std::string buffer) : m_buffer(), m_index(0) {
  setBuffer(buffer);
}

//===================================================================

Tokenizer::~Tokenizer() {}

//-------------------------------------------------------------------

void Tokenizer::setBuffer(std::string buffer) {
  m_buffer = buffer + '\0';
  m_index  = 0;
  m_tokens.clear();

  bool stringBlock = false;

  int i         = 0;
  const char *s = &m_buffer[0];
  for (;;) {
    while (isascii(s[i]) && isspace(s[i])) i++;

    int j = i;

    if (s[i] == '\0') {
      m_tokens.push_back(Token("", Token::Eos, j));
      break;
    }

    if (s[i] == '"') {
      stringBlock = !stringBlock;
      m_tokens.push_back(Token("\"", Token::Punct, j));

      ++i;
      continue;
    }

    std::string token;

    if (stringBlock) {
      // string block - read mercilessly until either another '"' or EOS
      token = std::string(1, s[i++]);

      while (s[i] != '"' && s[i] != '\0') token.append(1, s[i++]);

      m_tokens.push_back(Token(token, Token::Ident, j));
    } else if ((isascii(s[i]) && isalpha(s[i])) || s[i] == '_') {
      // ident
      token = std::string(1, s[i++]);

      while (isascii(s[i]) && (isalpha(s[i]) || s[i] == '_' || isdigit(s[i])))
        token.append(1, s[i++]);

      m_tokens.push_back(Token(token, Token::Ident, j));
    } else if ((isascii(s[i]) && isdigit(s[i])) || s[i] == '.') {
      // number
      while (isascii(s[i]) && isdigit(s[i])) token.append(1, s[i++]);

      if (s[i] == '.') {
        token.append(1, s[i++]);

        while (isascii(s[i]) && isdigit(s[i])) token.append(1, s[i++]);
      }

      if (token != "." && (s[i] == 'e' || s[i] == 'E')) {
        // Only consume the exponent if its optional sign is followed by a
        // digit.
        int exponent = i + 1;
        if (s[exponent] == '-' || s[exponent] == '+') ++exponent;

        if (isascii(s[exponent]) && isdigit(s[exponent])) {
          token.append(s + i, exponent - i);
          i = exponent;
          while (isascii(s[i]) && isdigit(s[i])) token.append(1, s[i++]);
        }
      }
      m_tokens.push_back(Token(token, Token::Number, j));
    } else {
      // punct.
      if (s[i + 1] != '\0') {
        token = std::string(s + i, 2);

        const std::string ss[] = {"==", "!=", ">=", "<=", "||", "&&"};

        if (std::find(std::begin(ss), std::end(ss), token) != std::end(ss))
          i += 2;
        else
          token = std::string(1, s[i++]);
      } else
        token = std::string(1, s[i++]);

      m_tokens.push_back(Token(token, Token::Punct, j));
    }
  }
}

//-------------------------------------------------------------------

int Tokenizer::getTokenCount() const { return m_tokens.size(); }

//-------------------------------------------------------------------

const Token &Tokenizer::getToken(int index) const {
  assert(0 <= index && index < getTokenCount());
  return m_tokens[index];
}

//-------------------------------------------------------------------

void Tokenizer::reset() { m_index = 0; }

//-------------------------------------------------------------------

const Token &Tokenizer::getToken() { return getToken(m_index); }

//-------------------------------------------------------------------

Token Tokenizer::nextToken() {
  Token token = getToken();
  if (m_index + 1 < getTokenCount()) m_index++;
  return token;
}

//-------------------------------------------------------------------

bool Tokenizer::eos() const { return m_index + 1 == getTokenCount(); }

//-------------------------------------------------------------------


//===================================================================

}  // namespace TSyntax
