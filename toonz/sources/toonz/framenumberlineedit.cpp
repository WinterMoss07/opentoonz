#include "framenumberlineedit.h"

#include "toonz/preferences.h"
#include "toonz/filepathproperties.h"

namespace {

QChar numToLetter(int letterNum) {
  switch (letterNum) {
  case 0:
    return QChar();
    break;
  case 1:
    return 'A';
    break;
  case 2:
    return 'B';
    break;
  case 3:
    return 'C';
    break;
  case 4:
    return 'D';
    break;
  case 5:
    return 'E';
    break;
  case 6:
    return 'F';
    break;
  case 7:
    return 'G';
    break;
  case 8:
    return 'H';
    break;
  case 9:
    return 'I';
    break;
  default:
    return QChar();
    break;
  }
}

int letterToNum(QChar appendix) {
  if (appendix == QChar('A') || appendix == QChar('a'))
    return 1;
  else if (appendix == QChar('B') || appendix == QChar('b'))
    return 2;
  else if (appendix == QChar('C') || appendix == QChar('c'))
    return 3;
  else if (appendix == QChar('D') || appendix == QChar('d'))
    return 4;
  else if (appendix == QChar('E') || appendix == QChar('e'))
    return 5;
  else if (appendix == QChar('F') || appendix == QChar('f'))
    return 6;
  else if (appendix == QChar('G') || appendix == QChar('g'))
    return 7;
  else if (appendix == QChar('H') || appendix == QChar('h'))
    return 8;
  else if (appendix == QChar('I') || appendix == QChar('i'))
    return 9;
  else
    return 0;
}

QString convertToFrameWithLetter(int value, int length = -1) {
  QString str;
  str.setNum((int)(value / 10));
  while (str.length() < length) str.push_front(QLatin1Char('0'));
  QChar letter = numToLetter(value % 10);
  if (!letter.isNull()) str.append(letter);
  return str;
}

}  // namespace

//=============================================================================

FrameNumberLineEdit::FrameNumberLineEdit(QWidget* parent, TFrameId fId,
                                         bool acceptLetter)
    : LineEdit(parent)
    , m_regexpValidator(nullptr)
    , m_regexpValidator_alt(nullptr) {
  if (acceptLetter) {
    const QString fid = TFilePath::fidRegExpStr();
    QRegularExpression regExp(QString("^%1$").arg(fid));

    if (regExp.isValid() && !fid.isEmpty()) {
      // Use frame-id regular expression from TFilePath
      m_regexpValidator = new QRegularExpressionValidator(regExp, this);
    } else {
      // Fallback: allow digits only if the regular expression is invalid
      m_regexpValidator = new QRegularExpressionValidator(
          QRegularExpression("^\\d{1,4}$"), this);
    }

    // Listen for project changes to update the validator dynamically
    TProjectManager::instance()->addListener(this);
  } else {
    // Digits only (no letter suffix allowed)
    m_regexpValidator =
        new QRegularExpressionValidator(QRegularExpression("^\\d{1,4}$"), this);
  }

  // Alternative validator allowing optional A–I suffix
  m_regexpValidator_alt = new QRegularExpressionValidator(
      QRegularExpression("^\\d{1,3}[A-Ia-i]?$"), this);

  updateValidator();
  updateSize();
  setValue(fId);
}

//-----------------------------------------------------------------------------

void FrameNumberLineEdit::updateValidator() {
  if (Preferences::instance()->isShowFrameNumberWithLettersEnabled())
    setValidator(m_regexpValidator_alt);
  else
    setValidator(m_regexpValidator);
}

//-----------------------------------------------------------------------------

void FrameNumberLineEdit::updateSize() {
  FilePathProperties* fpProp =
      TProjectManager::instance()->getCurrentProject()->getFilePathProperties();
  bool useStandard = fpProp->useStandard();
  int letterCount  = fpProp->letterCountForSuffix();
  if (useStandard)
    setFixedWidth(60);
  else {
    // 4 digits + letters reserve 12 px each
    int lc = (letterCount == 0) ? 9 : letterCount + 4;
    setFixedWidth(12 * lc);
  }
  updateGeometry();
}
//-----------------------------------------------------------------------------

void FrameNumberLineEdit::setValue(TFrameId fId) {
  QString str;
  if (Preferences::instance()->isShowFrameNumberWithLettersEnabled()) {
    if (!fId.getLetter().isEmpty()) {
      // Need some warning?
    }
    str = convertToFrameWithLetter(fId.getNumber(), 3);
  } else {
    str = QString::fromStdString(fId.expand());
  }
  setText(str);
  setCursorPosition(0);
}

//-----------------------------------------------------------------------------

TFrameId FrameNumberLineEdit::getValue() {
  if (Preferences::instance()->isShowFrameNumberWithLettersEnabled()) {
    QString str = text();
    int f;
    // If no letters added
    if (str.at(str.size() - 1).isDigit())
      f = str.toInt() * 10;
    else {
      f = str.left(str.size() - 1).toInt() * 10 +
          letterToNum(str.at(str.size() - 1));
    }
    return TFrameId(f);
  } else {
    QString regExpStr = QString("^%1$").arg(TFilePath::fidRegExpStr());
    QRegularExpression rx(regExpStr);
    QRegularExpressionMatch match = rx.match(text());
    if (!match.hasMatch()) return TFrameId();
    QString cap1 = match.captured(1);
    QString cap2 = match.captured(2);
    if (cap2.isEmpty())
      return TFrameId(cap1.toInt());
    else
      return TFrameId(cap1.toInt(), cap2);
  }
}

//-----------------------------------------------------------------------------

void FrameNumberLineEdit::onProjectSwitched() {
  QRegularExpressionValidator* oldValidator = m_regexpValidator;
  QString regExpStr = QString("^%1$").arg(TFilePath::fidRegExpStr());
  // Migration to QRegularExpressionValidator
  m_regexpValidator =
      new QRegularExpressionValidator(QRegularExpression(regExpStr), this);
  updateValidator();
  if (oldValidator) delete oldValidator;
  updateSize();
}

void FrameNumberLineEdit::onProjectChanged() { onProjectSwitched(); }

//-----------------------------------------------------------------------------

void FrameNumberLineEdit::focusInEvent(QFocusEvent* e) {
  m_textOnFocusIn = text();
}

void FrameNumberLineEdit::focusOutEvent(QFocusEvent* e) {
  // If the field is empty, then revert the last input
  if (text().isEmpty()) setText(m_textOnFocusIn);

  LineEdit::focusOutEvent(e);
}
