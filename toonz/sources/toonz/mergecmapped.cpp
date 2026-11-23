// Toonz includes
#include "tapp.h"
#include "matchline.h"

// TnzTools includes
#include "tools/toolutils.h"

// TnzQt includes
#include "toonzqt/icongenerator.h"

// TnzLib includes
#include "toonz/toonzscene.h"
#include "toonz/levelset.h"
#include "toonz/txsheet.h"
#include "toonz/txshcell.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/levelproperties.h"
#include "toonz/scenefx.h"
#include "toonz/dpiscale.h"
#include "toonz/txsheethandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/palettecontroller.h"
#include "toonz/tpalettehandle.h"
#include "toonz/toonzimageutils.h"

// TnzCore includes
#include "tpalette.h"
#include "tpaletteutil.h"
#include "ttoonzimage.h"
#include "tropcm.h"
#include "tundo.h"

// Qt includes
#include <QApplication>

// STD includes
#include <map>
#include <set>


namespace {

void mergeCmapped(const std::vector<MergeCmappedPair> &matchingLevels) {
  if (matchingLevels.empty()) return;

  TPalette *palette = matchingLevels[0].m_cell->getImage(false)->getPalette();

  // Add the styles used by the match drawings to the palette once per source
  // palette, so every drawing of a match level gets the same target styles.
  std::map<TPalette *, std::set<int>> usedStyles;
  for (const MergeCmappedPair &pair : matchingLevels) {
    TToonzImageP match = (TToonzImageP)pair.m_mcell->getImage(false);
    if (!match) throw TRopException("Can merge only cmapped raster images!");
    ToonzImageUtils::getUsedStyles(usedStyles[match->getPalette()], match);
  }
  std::map<TPalette *, std::map<int, int>> indexTables;
  for (const auto &styles : usedStyles)
    mergePalette(palette, indexTables[styles.first], styles.first,
                 styles.second);

  // upInkId -> downInkId
  std::map<int, int> usedColors;

  for (const MergeCmappedPair &pair : matchingLevels) {
    TToonzImageP img = (TToonzImageP)pair.m_cell->getImage(true);
    // Work on a copy: the match level itself must not be modified
    TToonzImageP source = (TToonzImageP)pair.m_mcell->getImage(false);
    if (!img || !source)
      throw TRopException("Can merge only cmapped raster images!");
    TToonzImageP match = (TToonzImageP)source->cloneImage();

    ToonzImageUtils::scrambleStyles(match, indexTables[source->getPalette()]);
    match->setPalette(palette);

    TRasterCM32P ras      = img->getRaster();
    TRasterCM32P matchRas = match->getRaster();
    if (!ras || !matchRas)
      throw TRopException("Can merge only cmapped images!");

    TRasterCM32P target;
    TRect rect;
    if (!placeMatchRaster(ras, matchRas, pair.m_imgAff, pair.m_matchAff,
                          target, rect))
      continue;

    ras->lock();
    matchRas->lock();
    TRop::overlayCmapped(target, matchRas, palette, palette, usedColors);
    ras->unlock();
    matchRas->unlock();

    img->setSavebox(img->getSavebox() + rect);
  }
}

}  // namespace

//-----------------------------------------------------------------------------
//! Undo of a merge: restores the merged column's cells, swaps the merged level
//! and the original one in the cast, and restores the shared palette.
//! The original level's images are never modified by a merge.

class MergeCmappedUndo final : public TUndo {
  int m_column, m_row0;
  std::vector<TXshCell> m_oldCells, m_newCells;
  TXshLevelP m_oldLevel, m_newLevel;
  TPaletteP m_palette;  //!< Palette shared by both levels
  TPaletteP m_paletteBefore, m_paletteAfter;

  void apply(const std::vector<TXshCell> &cells, TXshLevel *removed,
             TXshLevel *inserted, const TPalette *palette) const {
    TApp *app    = TApp::instance();
    TXsheet *xsh = app->getCurrentXsheet()->getXsheet();
    xsh->setCells(m_row0, m_column, (int)cells.size(), &cells[0]);

    TLevelSet *levelSet = app->getCurrentScene()->getScene()->getLevelSet();
    levelSet->removeLevel(removed, true);
    levelSet->insertLevel(inserted);

    m_palette->assign(palette);
    m_palette->setDirtyFlag(true);

    app->getPaletteController()->getCurrentLevelPalette()->notifyPaletteChanged();
    app->getCurrentScene()->notifyCastChange();
    app->getCurrentXsheet()->notifyXsheetChanged();
  }

public:
  MergeCmappedUndo(int column, int row0, const std::vector<TXshCell> &oldCells,
                   const std::vector<TXshCell> &newCells, TXshLevel *oldLevel,
                   TXshLevel *newLevel, TPalette *palette,
                   TPalette *paletteBefore)
      : m_column(column)
      , m_row0(row0)
      , m_oldCells(oldCells)
      , m_newCells(newCells)
      , m_oldLevel(oldLevel)
      , m_newLevel(newLevel)
      , m_palette(palette)
      , m_paletteBefore(paletteBefore)
      , m_paletteAfter(palette->clone()) {}

  void undo() const override {
    apply(m_oldCells, m_newLevel.getPointer(), m_oldLevel.getPointer(),
          m_paletteBefore.getPointer());
  }

  void redo() const override {
    apply(m_newCells, m_oldLevel.getPointer(), m_newLevel.getPointer(),
          m_paletteAfter.getPointer());
  }

  int getSize() const override {
    return sizeof(*this) +
           (int)(m_oldCells.size() + m_newCells.size()) * sizeof(TXshCell);
  }

  QString getHistoryString() override {
    return QObject::tr("Merge Tlv Levels  : Column%1").arg(m_column + 1);
  }
};

//-----------------------------------------------------------------------------

class MergedPair {
public:
  TFrameId m_f0, m_f1;
  TAffine m_aff;
  MergedPair(const TFrameId &f0, const TFrameId &f1, const TAffine &aff)
      : m_f0(f0), m_f1(f1), m_aff(aff) {}

  bool operator<(const MergedPair &p) const {
    if (m_f0 != p.m_f0) return m_f0 < p.m_f0;
    if (m_f1 != p.m_f1) return m_f1 < p.m_f1;
    if (m_aff.a11 != p.m_aff.a11) return m_aff.a11 < p.m_aff.a11;
    if (m_aff.a12 != p.m_aff.a12) return m_aff.a12 < p.m_aff.a12;
    if (m_aff.a13 != p.m_aff.a13) return m_aff.a13 < p.m_aff.a13;
    if (m_aff.a21 != p.m_aff.a21) return m_aff.a21 < p.m_aff.a21;
    if (m_aff.a22 != p.m_aff.a22) return m_aff.a22 < p.m_aff.a22;
    if (m_aff.a23 != p.m_aff.a23) return m_aff.a23 < p.m_aff.a23;
    return false;
  }
};

//--------------------------------------------------------------------

void mergeCmapped(int column, int mColumn, const QString &fullpath) {
  TApp *app    = TApp::instance();
  TXsheet *xsh = app->getCurrentXsheet()->getXsheet();
  int start, end;
  int mStart, mEnd;
  xsh->getCellRange(column, start, end);
  xsh->getCellRange(mColumn, mStart, mEnd);

  if (start > end) return;
  int row0 = (mStart <= mEnd) ? std::min(start, mStart) : start;
  int row1 = (mStart <= mEnd) ? std::max(end, mEnd) : end;

  std::vector<TXshCell> cell(row1 - row0 + 1);
  std::vector<TXshCell> mCell(cell.size());
  xsh->getCells(row0, column, cell.size(), &cell[0]);
  xsh->getCells(row0, mColumn, cell.size(), &mCell[0]);
  const std::vector<TXshCell> oldCells = cell;

  TXshSimpleLevel *level = 0, *mLevel = 0;
  TXshLevel *xl          = 0;
  double dpix = 0, dpiy = 0;
  for (int i = 0; i < (int)cell.size(); i++) {
    if (!level && !cell[i].isEmpty()) {
      level = cell[i].getSimpleLevel();
      xl    = cell[i].m_level.getPointer();
    }
    if (!mLevel) mLevel = mCell[i].getSimpleLevel();
    if (dpix == 0 && !cell[i].isEmpty())
      if (TToonzImageP ti = (TToonzImageP)cell[i].getImage(false))
        ti->getDpi(dpix, dpiy);
  }

  if (!level || !mLevel) return;
  if (dpix == 0 && dpiy == 0) {
    TPointD dpi = level->getProperties()->getDpi();
    dpix = dpi.x, dpiy = dpi.y;
  }

  // The merge adds the match levels' styles to this palette, which the new
  // level shares with the original one.
  TPalette *palette       = level->getPalette();
  TPaletteP paletteBefore = palette->clone();

  TFilePath fp(fullpath.toStdString());

  TXshLevel *txl = level->getScene()->createNewLevel(
      level->getType(), fp.getWideName(), level->getResolution());
  TXshSimpleLevel *newLevel = txl->getSimpleLevel();
  newLevel->setPath(fp);
  newLevel->setPalette(palette);
  newLevel->clonePropertiesFrom(level);

  std::vector<MergeCmappedPair> matchingLevels;
  std::map<MergedPair, TFrameId> computedMergedMap;
  int count = 0;

  for (int i = 0; i < (int)cell.size(); i++) {
    if (cell[i].isEmpty() && mCell[i].isEmpty()) continue;

    int row = row0 + i;
    TAffine imgAff, matchAff;
    getColumnPlacement(imgAff, xsh, row, column, false);
    getColumnPlacement(matchAff, xsh, row, mColumn, false);

    MergedPair mp(cell[i].isEmpty() ? TFrameId() : cell[i].getFrameId(),
                  mCell[i].isEmpty() ? TFrameId() : mCell[i].getFrameId(),
                  imgAff.inv() * matchAff);

    auto computedMergedIt = computedMergedMap.find(mp);
    if (computedMergedIt != computedMergedMap.end()) {
      cell[i] = TXshCell(newLevel, computedMergedIt->second);
      xsh->setCell(row, column, cell[i]);
      continue;
    }

    TToonzImageP newImage;
    if (cell[i].isEmpty()) {
      // Blank drawing to merge the match drawing into. A default pixel is
      // "no ink, no paint": an unfilled raster would be all ink #0.
      TRasterCM32P ras(level->getResolution());
      ras->fill(TPixelCM32());
      newImage = TToonzImageP(ras, TRect());
      newImage->setDpi(dpix, dpiy);
    } else {
      TImageP srcImage = cell[i].getImage(false);
      if (!srcImage) continue;
      newImage = (TToonzImageP)srcImage->cloneImage();
    }
    if (!newImage) continue;
    newImage->setPalette(palette);

    TFrameId newFid(++count);
    newLevel->setFrame(newFid, newImage);
    cell[i]         = TXshCell(newLevel, newFid);
    xsh->setCell(row, column, cell[i]);
    computedMergedMap[mp] = newFid;

    if (mCell[i].isEmpty() || !mCell[i].getImage(false)) continue;

    TAffine dpiAff  = getDpiAffine(newLevel, newFid);
    TAffine mdpiAff = getDpiAffine(mLevel, mCell[i].getFrameId());
    matchingLevels.push_back(MergeCmappedPair(cell[i], imgAff * dpiAff,
                                              mCell[i], matchAff * mdpiAff));
  }

  QApplication::setOverrideCursor(Qt::WaitCursor);
  mergeCmapped(matchingLevels);
  QApplication::restoreOverrideCursor();

  for (const MergeCmappedPair &pair : matchingLevels) {
    const TFrameId &fid = pair.m_cell->getFrameId();
    ToolUtils::updateSaveBox(newLevel, fid);
    IconGenerator::instance()->invalidate(newLevel, fid);
  }

  // The undo keeps the original level alive once it leaves the cast
  TUndoManager::manager()->add(new MergeCmappedUndo(
      column, row0, oldCells, cell, xl, txl, palette, paletteBefore.getPointer()));
  app->getCurrentScene()->getScene()->getLevelSet()->removeLevel(xl, true);

  newLevel->setDirtyFlag(true);
  app->getCurrentScene()->notifySceneChanged();
  app->getCurrentScene()->notifyCastChange();
  app->getCurrentXsheet()->notifyXsheetChanged();
}
