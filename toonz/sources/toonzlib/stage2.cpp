

#include "toonz/stage2.h"

#include <QAction>

namespace Stage {
const double bigBoxSize[] = {500, 500, 1000};
}  // namespace Stage

//=============================================================================
//    ToonzCheck
//=============================================================================

ToonzCheck *ToonzCheck::instance() {
  static ToonzCheck _instance;
  return &_instance;
}

//=============================================================================
//    ToggleCheck
//=============================================================================

bool ToggleCheck::isEnabled() const {
  return m_toggle ? m_toggle->isChecked() : false;
}

//-----------------------------------------------------------------------------

void ToggleCheck::setIsEnabled(bool on) {
  // trigger(), not setChecked(): the action's command has to run
  if (m_toggle && m_toggle->isChecked() != on) m_toggle->trigger();
}

//-----------------------------------------------------------------------------

CameraTestCheck *CameraTestCheck::instance() {
  static CameraTestCheck _instance;
  return &_instance;
}

//-----------------------------------------------------------------------------

CleanupPreviewCheck *CleanupPreviewCheck::instance() {
  static CleanupPreviewCheck _instance;
  return &_instance;
}

