

#include "tools/toolhandle.h"
#include "toonz/stage2.h"
#include "tools/tool.h"
#include "tools/toolcommandids.h"
#include "timage.h"
#include "toonz/preferences.h"

//=============================================================================
// ToolHandle
//-----------------------------------------------------------------------------

ToolHandle::ToolHandle()
    : m_tool(0)
    , m_toolName("")
    , m_toolTargetType(TTool::NoTarget)
    , m_storedToolName("")
    , m_toolIsBusy(false) {}

//-----------------------------------------------------------------------------

ToolHandle::~ToolHandle() {}

//-----------------------------------------------------------------------------

TTool *ToolHandle::getTool() const { return m_tool; }

//-----------------------------------------------------------------------------

namespace {
// Temporary viewer navigation (e.g. while holding Space)
bool isViewNavigationTool(const QString &name) {
  return name == T_HandView || name == T_ZoomView || name == T_RotateView;
}

bool isNavigationTool(const QString &name) {
  return name == T_Hand || name == T_Zoom || name == T_Rotate ||
         isViewNavigationTool(name);
}
}  // namespace

void ToolHandle::setTool(QString name) {
  // Leaving Camera Test or Cleanup Preview may switch tools itself, so do it
  // before recording the requested tool. setIsEnabled() unchecks the toggle
  // and runs its command, keeping the toggle and the mode consistent.
  if (name != "T_CameraTest" && !isViewNavigationTool(name))
    CameraTestCheck::instance()->setIsEnabled(false);
  // Cleanup Preview only allows tools that don't edit the drawing
  if (!isNavigationTool(name))
    CleanupPreviewCheck::instance()->setIsEnabled(false);

  m_oldToolName = m_toolName = name;

  TTool *tool = TTool::getTool(m_toolName.toStdString(),
                               (TTool::ToolTargetType)m_toolTargetType);
  if (tool == m_tool) return;

  if (m_tool) m_tool->onDeactivate();
  m_tool = tool;
  if (m_tool) {
    m_tool->onActivate();
    emit toolSwitched();
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::storeTool() {
  // navigation tools will not be stored
  if (!isViewerNavigationToolSelected()) {
    m_storedToolName = m_toolName;
    m_storedToolTime.start();
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::restoreTool() {
  // navigation tools will always return to the stored one even if the key press
  // time is short
  if (m_storedToolName != m_toolName && m_storedToolName != "" &&
      (m_storedToolTime.elapsed() >
           Preferences::instance()->getTempToolSwitchTimer() ||
       isViewerNavigationToolSelected())) {
    setTool(m_storedToolName);
  }
}

//-----------------------------------------------------------------------------

bool ToolHandle::isViewerNavigationToolSelected() {
  return isViewNavigationTool(m_toolName);
}

//-----------------------------------------------------------------------------

void ToolHandle::setPseudoTool(QString name) {
  QString oldToolName = m_oldToolName;
  setTool(name);
  m_oldToolName = oldToolName;
}

//-----------------------------------------------------------------------------

void ToolHandle::unsetPseudoTool() {
  if (m_toolName != m_oldToolName) setTool(m_oldToolName);
}

//-----------------------------------------------------------------------------

void ToolHandle::setToolBusy(bool value) {
  m_toolIsBusy = value;
  if (!m_toolIsBusy) emit toolEditingFinished();
}

//-----------------------------------------------------------------------------

void ToolHandle::onImageChanged(TImage::Type imageType) {
  TTool::ToolTargetType targetType = TTool::EmptyTarget;

  switch (imageType) {
  case TImage::RASTER:
    targetType = TTool::RasterImage;
    break;
  case TImage::TOONZ_RASTER:
    targetType = TTool::ToonzImage;
    break;
  case TImage::MESH:
    targetType = TTool::MeshImage;
    break;
  case TImage::META:
    targetType = TTool::MetaImage;
    break;
  case TImage::VECTOR:
    targetType = TTool::VectorImage;
    break;
  default:
    targetType = TTool::EmptyTarget;
    break;
  }

  if (targetType != m_toolTargetType) {
    m_toolTargetType = targetType;
    setTool(m_toolName);
  }

  if (m_tool) {
    m_tool->updateMatrix();
    m_tool->onImageChanged();
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::updateMatrix() {
  if (m_tool) m_tool->updateMatrix();
}
