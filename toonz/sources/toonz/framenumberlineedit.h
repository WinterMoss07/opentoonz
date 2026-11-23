#pragma once

#ifndef FRAMENUMBERLINEEDIT_H
#define FRAMENUMBERLINEEDIT_H

#include "toonzqt/lineedit.h"
#include "toonz/tproject.h"
#include "tfilepath.h"

#include <QRegularExpressionValidator>

//=============================================================================
// FrameNumberLineEdit
// Special Line Edit which accepts alphabets input if the preference option
// "Show ABC Appendix to the Frame Number in Xsheet Cell" is active
//-----------------------------------------------------------------------------

class FrameNumberLineEdit : public DVGui::LineEdit,
                            public TProjectManager::Listener {
  Q_OBJECT

  QRegularExpressionValidator *m_regexpValidator, *m_regexpValidator_alt;

  void updateValidator();
  void updateSize();
  QString m_textOnFocusIn;

public:
  FrameNumberLineEdit(QWidget* parent = nullptr, TFrameId fId = TFrameId(1),
                      bool acceptLetter = true);
  ~FrameNumberLineEdit() {}

  /*! Set text in field to \b value. */
  void setValue(TFrameId fId);
  /*! Return an integer with text field value. */
  TFrameId getValue();

  // TProjectManager::Listener
  void onProjectSwitched() override;
  void onProjectChanged() override;

protected:
  /*! If focus is lost and current text value is out of range emit signal
   * \b editingFinished.*/
  void focusInEvent(QFocusEvent*) override;
  void focusOutEvent(QFocusEvent*) override;
  void showEvent(QShowEvent* event) override { updateValidator(); }
};

#endif  // FRAMENUMBERLINEEDIT_H
