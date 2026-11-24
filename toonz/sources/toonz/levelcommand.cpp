
#include "levelcommand.h"
#include "toonzqt/menubarcommand.h"
#include "menubarcommandids.h"
#include "tapp.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/txsheethandle.h"
#include "filmstripselection.h"
#include "castselection.h"
#include "cellselection.h"
#include "timagecache.h"

#include "toonz/txshsimplelevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshleveltypes.h"
#include "toonz/levelset.h"
#include "toonz/txshcell.h"
#include "toonz/childstack.h"
#include "toonz/txshchildlevel.h"

#include "toonzqt/dvdialog.h"
#include "toonzqt/icongenerator.h"

#include "tundo.h"
#include "tconvert.h"
#include "tlevel_io.h"
#include "ttoonzimage.h"
#include "tsystem.h"
#include "tfiletype.h"

#include "toonzqt/gutil.h"
#include "toonz/namebuilder.h"

#include <QProgressDialog>
#include <QMainWindow>
#include <QApplication>

namespace {

class DeleteLevelUndo final : public TUndo {
  TXshLevelP m_xl;

public:
  DeleteLevelUndo(TXshLevel *xl) : m_xl(xl) {}

  void undo() const override {
    ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
    scene->getLevelSet()->insertLevel(m_xl.getPointer());
    TApp::instance()->getCurrentScene()->notifyCastChange();
  }
  void redo() const override {
    ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
    scene->getLevelSet()->removeLevel(m_xl.getPointer());
    TApp::instance()->getCurrentScene()->notifyCastChange();
  }

  int getSize() const override { return sizeof *this + 100; }

  QString getHistoryString() override {
    return QObject::tr("Delete Level  : %1")
        .arg(QString::fromStdWString(m_xl->getName()));
  }
};

}  // namespace

//-----------------------------------------------------------------------------

bool LevelCmd::removeUnusedLevelsFromCast(bool showMessage) {
  TApp *app         = TApp::instance();
  ToonzScene *scene = app->getCurrentScene()->getScene();

  TLevelSet *levelSet = scene->getLevelSet();

  std::set<TXshLevel *> usedLevels;
  scene->getTopXsheet()->getUsedLevels(usedLevels);

  std::vector<TXshLevel *> unused;

  for (int i = 0; i < levelSet->getLevelCount(); i++) {
    TXshLevel *xl = levelSet->getLevel(i);
    if (usedLevels.count(xl) == 0) unused.push_back(xl);
  }
  if (unused.empty()) {
    if (showMessage) DVGui::error(QObject::tr("No unused levels"));
    return false;
  } else {
    TUndoManager *um = TUndoManager::manager();
    um->beginBlock();
    for (int i = 0; i < (int)unused.size(); i++) {
      TXshLevel *xl = unused[i];
      um->add(new DeleteLevelUndo(xl));
      scene->getLevelSet()->removeLevel(xl);
    }
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentScene()->notifyCastChange();

    um->endBlock();
  }
  return true;
}

bool LevelCmd::removeLevelFromCast(TXshLevel *level, ToonzScene *scene,
                                   bool showMessage) {
  if (!scene) scene = TApp::instance()->getCurrentScene()->getScene();
  if (scene->getChildStack()->getTopXsheet()->isLevelUsed(level)) {
    if (showMessage) {
      DVGui::error(
          QObject::tr("It is not possible to delete the used level %1.")
              .arg(QString::fromStdWString(
                  level->getName())));  //"E_CantDeleteUsedLevel_%1"
    }
    return false;
  } else {
    TUndoManager *um = TUndoManager::manager();
    um->add(new DeleteLevelUndo(level));
    scene->getLevelSet()->removeLevel(level);
  }
  return true;
}

void LevelCmd::loadAllUsedRasterLevelsAndPutInCache(bool cacheImagesAsWell) {
  TApp *app         = TApp::instance();
  ToonzScene *scene = app->getCurrentScene()->getScene();

  TLevelSet *levelSet = scene->getLevelSet();

  std::set<TXshLevel *> usedLevels;
  scene->getTopXsheet()->getUsedLevels(usedLevels);

  std::map<TXshSimpleLevel *, int>
      targetLevels;  // level pointer and its frame amount
  int totalFrames = 0;
  // estimate the amount
  for (auto xl : usedLevels) {
    TXshSimpleLevel *simpleLevel = xl->getSimpleLevel();
    if (simpleLevel && (simpleLevel->getType() == TZP_XSHLEVEL ||
                        simpleLevel->getType() == OVL_XSHLEVEL)) {
      targetLevels[simpleLevel] = simpleLevel->getFrameCount();
      totalFrames += simpleLevel->getFrameCount();
    }
  }

  // if the amount of frames is more than 10, open a progress dialog and
  // shows WaitCursor
  QProgressDialog *pd = nullptr;
  if (totalFrames > 10) {
    pd = new QProgressDialog(QObject::tr("Loading Raster Images To Cache..."),
                             QObject::tr("Cancel"), 0, totalFrames,
                             app->getMainWindow());
    pd->setAttribute(Qt::WA_DeleteOnClose, true);
    pd->setWindowModality(Qt::WindowModal);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    pd->show();
  }
  std::map<TXshSimpleLevel *, int>::iterator i = targetLevels.begin();
  while (i != targetLevels.end()) {
    if (pd && pd->wasCanceled()) break;
    i->first->loadAllIconsAndPutInCache(cacheImagesAsWell);
    if (pd) {
      pd->setValue(pd->value() + i->second);
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    ++i;
  }

  if (pd) {
    QApplication::restoreOverrideCursor();
    pd->close();
  }
  return;
}

//=============================================================================
// RemoveUnusedLevelCommand
//-----------------------------------------------------------------------------

class RemoveUnusedLevelsCommand final : public MenuItemHandler {
public:
  RemoveUnusedLevelsCommand() : MenuItemHandler(MI_RemoveUnused) {}

  void execute() override { LevelCmd::removeUnusedLevelsFromCast(); }
} removeUnusedLevelsCommand;

//=============================================================================
// RemoveLevelCommand
//-----------------------------------------------------------------------------

class RemoveLevelCommand final : public MenuItemHandler {
public:
  RemoveLevelCommand() : MenuItemHandler(MI_RemoveLevel) {}

  void execute() override {
    TXsheet *xsheet = TApp::instance()->getCurrentXsheet()->getXsheet();
    CastSelection *castSelection =
        dynamic_cast<CastSelection *>(TSelection::getCurrent());
    if (!castSelection) return;

    std::vector<TXshLevel *> levels;
    castSelection->getSelectedLevels(levels);
    if (levels.empty()) {
      DVGui::error("No level selected");  // E_NoSelectedLevel
      return;
    }
    int count = 0;
    for (int i = 0; i < (int)levels.size(); i++)
      if (LevelCmd::removeLevelFromCast(levels[i])) count++;
    if (count == 0) return;
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentScene()->notifyCastChange();
  }

} removeLevelCommand;

//=============================================================================
namespace {
//-----------------------------------------------------------------------------

TFilePath getUnpaintedLevelPath(TXshSimpleLevel *simpleLevel) {
  ToonzScene *scene   = simpleLevel->getScene();
  TFilePath levelPath = scene->decodeFilePath(simpleLevel->getPath());
  if (levelPath.isEmpty()) return TFilePath();
  std::string name = levelPath.getName() + "_np." + levelPath.getType();
  return levelPath.getParentDir() + "nopaint\\" + TFilePath(name);
}

//-----------------------------------------------------------------------------

void getLevelSelectedFids(std::set<TFrameId> &fids, TXshSimpleLevel *level,
                          int r0, int c0, int r1, int c1) {
  TXsheet *xsheet = TApp::instance()->getCurrentXsheet()->getXsheet();
  int c, r;
  for (c = c0; c <= c1; c++)
    for (r = r0; r <= r1; r++) {
      TXshCell cell             = xsheet->getCell(r, c);
      TXshSimpleLevel *curLevel = (!cell.isEmpty()) ? cell.getSimpleLevel() : 0;
      if (curLevel != level) continue;
      fids.insert(cell.getFrameId());
    }
}

//-----------------------------------------------------------------------------

//! Replaces the selected frames of sl with the ones of the file at path. When
//! addedFids is given, frames of the file missing from the level are added
//! too (they are loaded when shown) and listed in addedFids.
bool loadFids(const TFilePath &path, TXshSimpleLevel *sl,
              const std::set<TFrameId> &selectedFids,
              std::set<TFrameId> *addedFids = nullptr) {
  assert(sl && !selectedFids.empty());

  TLevelReaderP levelReader = TLevelReaderP(path);
  if (!levelReader.getPointer()) return false;

  TLevelP level = levelReader->loadInfo();
  if (!level || level->getFrameCount() == 0) return false;
  std::set<TFrameId> loadedFids;
  for (TLevel::Iterator levelIt = level->begin(); levelIt != level->end();
       ++levelIt) {
    TFrameId fid = levelIt->first;
    if (selectedFids.count(fid)) {
      TImageP img = levelReader->getFrameReader(fid)->load();
      if (!img.getPointer()) continue;
      sl->setFrame(fid, img);
      loadedFids.insert(fid);
    } else if (addedFids && !sl->isFid(fid)) {
      sl->setFrame(fid, TImageP());
      addedFids->insert(fid);
      loadedFids.insert(fid);
    }
  }
  if (loadedFids.empty()) return false;
  invalidateIcons(sl, loadedFids);
  return true;
}

//-----------------------------------------------------------------------------

bool loadUnpaintedFids(TXshSimpleLevel *sl,
                       const std::set<TFrameId> &selectedFids) {
  TFilePath path = getUnpaintedLevelPath(sl);

  if (!TSystem::doesExistFileOrLevel(path)) {
    DVGui::error(QObject::tr(
        "No cleaned up drawings available for the current selection."));
    return false;
  }

  return loadFids(path, sl, selectedFids);
}

//-----------------------------------------------------------------------------

//! Image sequences are often rendered by other applications: reloading one
//! also brings the frames added to the sequence since it was loaded.
bool loadLastSaveFids(TXshSimpleLevel *sl,
                      const std::set<TFrameId> &selectedFids,
                      std::set<TFrameId> *addedFids) {
  TFilePath path = sl->getScene()->decodeFilePath(sl->getPath());

  if (!TSystem::doesExistFileOrLevel(path)) {
    DVGui::error(
        QObject::tr("No saved drawings available for the current selection."));
    return false;
  }

  bool isSequence = sl->getType() == OVL_XSHLEVEL &&
                    TFileType::isSequenceExtension(
                        QString::fromStdString(path.getType()));
  return loadFids(path, sl, selectedFids, isSequence ? addedFids : nullptr);
}

//=============================================================================
// Undo RevertToCommandUndo
//-----------------------------------------------------------------------------

class RevertToCommandUndo final : public TUndo {
  TXshSimpleLevel *m_sl;
  std::vector<std::pair<TFrameId, QString>> m_replacedImgs;
  std::set<TFrameId> m_selectedFids, m_addedFids;
  bool m_isCleanedUp;

public:
  RevertToCommandUndo(TXshSimpleLevel *sl,
                      const std::set<TFrameId> &selectedFids, bool isCleanedUp)
      : m_sl(sl), m_selectedFids(selectedFids), m_isCleanedUp(isCleanedUp) {
    static int revertToCommandCount = 0;
    for (auto const &fid : m_selectedFids) {
      if (!sl->isFid(fid)) continue;
      TImageP image = sl->getFrame(fid, false);
      if (!image) continue;
      QString newImageId = "RevertToUndo" +
                           QString::number(revertToCommandCount) + "-" +
                           QString::fromStdString(fid.expand());
      TImageCache::instance()->add(newImageId, image->cloneImage());
      m_replacedImgs.push_back({fid, newImageId});
    }
    revertToCommandCount++;
  }

  ~RevertToCommandUndo() {
    for (const auto &replaced : m_replacedImgs)
      TImageCache::instance()->remove(replaced.second);
  }

  //! Loads the frames; false if nothing was loaded
  bool execute() {
    m_addedFids.clear();
    return m_isCleanedUp ? loadUnpaintedFids(m_sl, m_selectedFids)
                         : loadLastSaveFids(m_sl, m_selectedFids, &m_addedFids);
  }

  void undo() const override {
    for (const auto &replaced : m_replacedImgs) {
      TImageP img = TImageCache::instance()->get(replaced.second, false);
      if (!img) continue;
      m_sl->setFrame(replaced.first, img->cloneImage());
    }
    for (const TFrameId &fid : m_addedFids) m_sl->eraseFrame(fid);
    std::set<TFrameId> fids = m_selectedFids;
    fids.insert(m_addedFids.begin(), m_addedFids.end());
    invalidateIcons(m_sl, fids);
    notify();
  }

  void redo() const override {
    const_cast<RevertToCommandUndo *>(this)->execute();
    notify();
  }

  static void notify() {
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentLevel()->notifyLevelChange();
  }

  int getSize() const override {
    return sizeof(*this) +
           (m_selectedFids.size() + m_addedFids.size()) * sizeof(TFrameId);
  }

  QString getHistoryString() override {
    return QObject::tr("Revert To %1  : Level %2")
        .arg((m_isCleanedUp) ? QString("Cleaned Up") : QString("Last Saved"))
        .arg(QString::fromStdWString(m_sl->getName()));
  }
};

//-----------------------------------------------------------------------------

//! Reverts the frames of one level; false if nothing could be loaded
bool revertFids(TXshSimpleLevel *sl, const std::set<TFrameId> &fids,
                bool isCleanedUp) {
  RevertToCommandUndo *undo = new RevertToCommandUndo(sl, fids, isCleanedUp);
  if (!undo->execute()) {
    delete undo;
    return false;
  }
  TUndoManager::manager()->add(undo);
  if (isCleanedUp) sl->setDirtyFlag(true);
  return true;
}

//-----------------------------------------------------------------------------
/*-- isCleanedUp true: "Revert to Cleaned Up", false: "Reload" (revert to last
     saved) --*/
void revertTo(bool isCleanedUp) {
  TApp *app = TApp::instance();

  TFilmstripSelection *filmstripSelection =
      dynamic_cast<TFilmstripSelection *>(TSelection::getCurrent());
  TCellSelection *cellSelection =
      dynamic_cast<TCellSelection *>(TSelection::getCurrent());

  if (filmstripSelection) {
    std::set<TFrameId> selectedFids = filmstripSelection->getSelectedFids();
    TXshSimpleLevel *sl             = app->getCurrentLevel()->getSimpleLevel();
    if (!sl || selectedFids.empty()) {
      DVGui::error(QObject::tr("The current selection is invalid."));
      return;
    }
    revertFids(sl, selectedFids, isCleanedUp);
    RevertToCommandUndo::notify();
  } else if (cellSelection) {
    std::set<TXshSimpleLevel *> levels;
    int r0, r1, c0, c1;
    cellSelection->getSelectedCells(r0, c0, r1, c1);
    TXsheet *xsheet = app->getCurrentXsheet()->getXsheet();
    // the levels of the selection that can be reverted
    for (int c = c0; c <= c1; c++)
      for (int r = r0; r <= r1; r++) {
        TXshCell cell          = xsheet->getCell(r, c);
        TXshSimpleLevel *level = (!cell.isEmpty()) ? cell.getSimpleLevel() : 0;
        if (!level) continue;
        int type = level->getType();
        if ((isCleanedUp && type == TZP_XSHLEVEL) ||
            (!isCleanedUp && (type == TZP_XSHLEVEL || type == PLI_XSHLEVEL ||
                              type == OVL_XSHLEVEL)))
          levels.insert(level);
      }
    if (levels.empty()) {
      DVGui::error(
          QObject::tr("The Reload command is not supported for "
                      "the current selection."));
      return;
    }
    TUndoManager::manager()->beginBlock();
    for (auto const sl : levels) {
      std::set<TFrameId> selectedFids;
      getLevelSelectedFids(selectedFids, sl, r0, c0, r1, c1);
      revertFids(sl, selectedFids, isCleanedUp);
    }
    TUndoManager::manager()->endBlock();
    RevertToCommandUndo::notify();
  } else
    DVGui::error(QObject::tr("The current selection is invalid."));
}

//-----------------------------------------------------------------------------
}  // namespace
//=============================================================================

//=============================================================================
// RevertToCleanedUpCommand
//-----------------------------------------------------------------------------

class RevertToCleanedUpCommand final : public MenuItemHandler {
public:
  RevertToCleanedUpCommand() : MenuItemHandler(MI_RevertToCleanedUp) {}

  void execute() override { revertTo(true); }

} revertToCleanedUpCommand;

//=============================================================================
// RevertToLastSaveCommand
//-----------------------------------------------------------------------------

class RevertToLastSaveCommand final : public MenuItemHandler {
public:
  RevertToLastSaveCommand() : MenuItemHandler(MI_RevertToLastSaved) {}

  void execute() override { revertTo(false); }

} revertToLastSaveCommand;

//-----------------------------------------------------------------------------
namespace {
class addLevelToCastUndo final : public TUndo {
  TXshLevelP m_xl;
  std::wstring m_newName, m_oldName;

public:
  addLevelToCastUndo(TXshLevel *xl, const std::wstring newName = L"",
                     const std::wstring oldName = L"")
      : m_xl(xl), m_newName(newName), m_oldName(oldName) {}

  void undo() const override {
    TLevelSet *levelSet =
        TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
    levelSet->removeLevel(m_xl.getPointer());
    if (m_oldName != L"") m_xl->setName(m_oldName);
    if (m_isLastInBlock)
      TApp::instance()->getCurrentScene()->notifyCastChange();
  }
  void redo() const override {
    TLevelSet *levelSet =
        TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
    if (m_newName != L"") m_xl->setName(m_newName);
    levelSet->insertLevel(m_xl.getPointer());
    if (m_isLastInRedoBlock)
      TApp::instance()->getCurrentScene()->notifyCastChange();
  }

  int getSize() const override { return sizeof *this + 100; }

  QString getHistoryString() override {
    return QObject::tr("Add Level to Scene Cast : %1")
        .arg(QString::fromStdWString(m_xl->getName()));
  }
};

};  // namespace

void LevelCmd::addMissingLevelsToCast(const QList<TXshColumnP> &columns) {
  // make sure that the levels contained in the pasted columns are registered in
  // the scene cast it may rename the level if there is another level with the
  // same name
  std::set<TXshLevel *> levels;
  // obtain level set contained in the specified columns
  // it is used for checking and updating the scene cast when pasting
  // based on TXsheet::getUsedLevels
  for (auto column : columns) {
    if (!column) continue;

    TXshCellColumn *cellColumn = column->getCellColumn();
    if (!cellColumn) continue;

    int r0, r1;
    if (!cellColumn->getRange(r0, r1)) continue;

    TXshLevel *level = 0;
    for (int r = r0; r <= r1; r++) {
      TXshCell cell = cellColumn->getCell(r);
      if (cell.isEmpty() || !cell.m_level) continue;

      if (level != cell.m_level.getPointer()) {
        level = cell.m_level.getPointer();
        levels.insert(level);
        if (level->getChildLevel()) {
          TXsheet *childXsh = level->getChildLevel()->getXsheet();
          childXsh->getUsedLevels(levels);
        }
      }
    }
  }
  LevelCmd::addMissingLevelsToCast(levels);
}

void LevelCmd::addMissingLevelsToCast(std::set<TXshLevel *> &levels) {
  // remove zerary fx levels which are not registered in the cast
  for (auto it = levels.begin(); it != levels.end();) {
    if ((*it)->getZeraryFxLevel())
      it = levels.erase(it);
    else
      ++it;
  }

  if (levels.empty()) return;
  TUndoManager::manager()->beginBlock();
  TLevelSet *levelSet =
      TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
  bool castChanged = false;
  // for each level
  for (auto level : levels) {
    std::wstring levelName = level->getName();

    // search by level name
    if (TXshLevel *levelInCast = levelSet->getLevel(levelName)) {
      // continue if it is the same level. This should be in most cases
      if (level == levelInCast) continue;

      // if the the name is occupied by another level, then rename and register
      // it
      std::wstring oldName = levelName;
      NameModifier nm(levelName);
      levelName = nm.getNext();
      while (1) {
        TXshLevel *existingLevel = levelSet->getLevel(levelName);
        // if the level name is not used in the cast, nothing to do
        if (!existingLevel) break;
        // try if the existing level is unused in the xsheet and remove from the
        // cast
        else if (Preferences::instance()->isAutoRemoveUnusedLevelsEnabled() &&
                 LevelCmd::removeLevelFromCast(
                     existingLevel,
                     TApp::instance()->getCurrentScene()->getScene(), false)) {
          DVGui::info(
              QObject::tr("Removed unused level %1 from the scene cast. (This "
                          "behavior can be disabled in Preferences.)")
                  .arg(QString::fromStdWString(levelName)));
          break;
        }
        levelName = nm.getNext();
      }
      addLevelToCastUndo *undo =
          new addLevelToCastUndo(level, levelName, oldName);
      undo->m_isLastInRedoBlock = false;  // prevent to emit signal
      undo->redo();
      TUndoManager::manager()->add(undo);
      castChanged = true;
    }
    // if not found
    else {
      // register the level
      addLevelToCastUndo *undo = new addLevelToCastUndo(level);
      undo->redo();
      TUndoManager::manager()->add(undo);
      castChanged = true;
    }
  }

  TUndoManager::manager()->endBlock();

  if (castChanged) TApp::instance()->getCurrentScene()->notifyCastChange();
}