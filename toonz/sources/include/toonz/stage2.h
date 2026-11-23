#pragma once

#ifndef STAGE2_INCLUDED
#define STAGE2_INCLUDED

// TnzLib includes
#include "toonz/stage.h"
#include "toonz/autoclose.h"

#undef DVAPI
#undef DVVAR
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//=========================================================

//    Forward declarations

class QAction;

namespace Stage {
DVVAR extern const double bigBoxSize[];
}

//=========================================================

//*****************************************************************************
//    ToonzCheck  declaration
//*****************************************************************************

/*!
  \brief    Singleton class storing on/off switches related to special
            level viewing modes.
*/

class DVAPI ToonzCheck {
public:
  enum Type {
    eTransparency = 0x1,   //!< Transparency check.
    eBlackBg      = 0x2,   //!< Black background check.
    eInk          = 0x4,   //!< Ink check (show current color on inks).
    ePaint        = 0x8,   //!< Paint check (show current color on paints).
    eGap          = 0x10,  //!< Gaps check.
    eAutoclose    = 0x20,  //!< Autoclose check.
    eInksOnly     = 0x40,  //!< Inks only (transparent paints).
    eInk1         = 0x80   //!< Ink#1 check (show style#1 on inks).
  };
  AutocloseSettings autoCloseSettings;

public:
  static ToonzCheck *instance();

  int getChecks() const { return m_mask; }
  void toggleCheck(int checkType) {
    m_mask ^= checkType;
    // make "ink check" and "ink#1 check" exclusive
    if (checkType == eInk)
      m_mask = m_mask & ~eInk1;
    else if (checkType == eInk1)
      m_mask = m_mask & ~eInk;
  }

  /*! \note   The colorIndex is used for eInk and ePaint */
  int getColorIndex() const { return m_colorIndex; }
  bool setColorIndex(int index) {
    if (index <= 0 || m_colorIndex == index) return false;

    m_colorIndex = index;
    return true;
  }

  AutocloseSettings getAutocloseSettings() {
    return autoCloseSettings;
  };

  void setAutocloseSettings(int distance, double angle, int opacity,
                            bool ignoreAP) {
    autoCloseSettings =
        AutocloseSettings(distance, angle, opacity, ignoreAP);
    TAutocloser::clearSegmentCache();
  }

  private : int m_mask;
  int m_colorIndex;

private:
  ToonzCheck() : m_mask(0), m_colorIndex(-1) {}
};

//*****************************************************************************
//    Isolated check  singletons
//*****************************************************************************

/*!
  \brief    On/off viewing mode bound to a checkable menu action.

  setIsEnabled() triggers the action when its state has to change, so the
  command attached to the action runs and the menu stays in sync.
*/

class DVAPI ToggleCheck {
  QAction *m_toggle = nullptr;

public:
  void setToggle(QAction *toggle) { m_toggle = toggle; }

  bool isEnabled() const;
  void setIsEnabled(bool on);

protected:
  ToggleCheck() = default;
};

//=============================================================================

class DVAPI CameraTestCheck final : public ToggleCheck {
public:
  static CameraTestCheck *instance();

private:
  CameraTestCheck() = default;
};

//=============================================================================

class DVAPI CleanupPreviewCheck final : public ToggleCheck {
public:
  static CleanupPreviewCheck *instance();

private:
  CleanupPreviewCheck() = default;
};

//=============================================================================

#endif
