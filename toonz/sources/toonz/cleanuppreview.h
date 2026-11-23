#pragma once

#ifndef CLEANUPPREVIEW_H
#define CLEANUPPREVIEW_H

// STL includes
#include <vector>

// ToonzLib includes
#include "toonz/txshsimplelevel.h"

// ToonzQt includes
#include "toonzqt/menubarcommand.h"

// Qt includes
#include <QTimer>

//========================================================

//**********************************************************************************
//    CleanupPreviewToggle declaration
//**********************************************************************************

//! Common part of the Cleanup Preview and Camera Test toggles: both substitute
//! the frame under cleanup focus with a processed image and must release the
//! substituted frames when switched off.
class CleanupPreviewToggle : public MenuItemHandler {
protected:
  TXshSimpleLevelP m_sl;         //!< Level under cleanup focus
  std::vector<TFrameId> m_fids;  //!< Previewed frames of said level
  bool m_enabled = false;

  CleanupPreviewToggle(CommandId cmdId) : MenuItemHandler(cmdId) {}

  //! Follows the frame under cleanup focus and marks it as previewed.
  //! Returns false when there is no level to preview.
  bool updatePreviewedFrame();
  //! Releases every previewed frame.
  void clean();
};

//**********************************************************************************
//    PreviewToggleCommand declaration
//**********************************************************************************

class PreviewToggleCommand final : public CleanupPreviewToggle {
  Q_OBJECT

  QTimer m_timer;  //!< Processing timer - allows processing only
                   //!< after half a sec. without parameter changes
public:
  PreviewToggleCommand();

  void execute() override;

private:
  void enable();
  void disable();

protected slots:

  void onPreviewDataChanged();
  void onModelChanged(bool needsPostProcess);
  void postProcess();
};

//**********************************************************************************
//    CameraTestToggleCommand declaration
//**********************************************************************************

class CameraTestToggleCommand final : public CleanupPreviewToggle {
  Q_OBJECT

  QString m_oldToolName;  //!< Tool restored when the camera test ends

public:
  CameraTestToggleCommand();

  void execute() override;

private:
  void enable();
  void disable();

protected slots:

  void onPreviewDataChanged();
  void postProcess();
};

#endif  // CLEANUPPREVIEW_H
