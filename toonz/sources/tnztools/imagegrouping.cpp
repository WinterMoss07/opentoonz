

#include "tools/imagegrouping.h"

// TnzTools includes
#include "tools/strokeselection.h"
#include "tools/tool.h"
#include "tools/toolhandle.h"
#include "tools/toolutils.h"
#include "vectorselectiontool.h"

// TnzQt includes
#include "toonzqt/tselectionhandle.h"
#include "toonzqt/selectioncommandids.h"
#include "toonzqt/dvdialog.h"

// TnzLib includes
#include "toonz/tscenehandle.h"
#include "toonz/txshlevelhandle.h"

// TnzCore includes
#include "tvectorimage.h"
#include "tundo.h"
#include "tthreadmessage.h"

// Qt includes
#include <QMenu>

//=============================================================================
namespace {
//-----------------------------------------------------------------------------

//! A moveStrokes() call, kept to be undone
struct StrokeMove {
  int m_from, m_count, m_moveBefore;
};

//! Groups the strokes at \b indexes (increasing). Strokes that aren't next to
//! each other are first brought up to the following ones, as listed in
//! \b moves. Returns the index of the first stroke of the new group.
int groupStrokes(TVectorImage *vimg, const std::vector<int> &indexes,
                 std::vector<StrokeMove> &moves) {
  int first = -1, count = 0, last = -1;
  for (int i : indexes) {
    if (first == -1)
      first = i;
    else if (last != i - 1) {
      vimg->moveStrokes(first, count, i);
      moves.push_back({first, count, i});
      first = i - count;
    }
    last = i;
    count++;
  }
  assert(count > 0);
  if (count > 0) vimg->group(first, count);
  return first;
}

//-----------------------------------------------------------------------------

//! Ungroups the outer group of every selected grouped stroke. Returns the
//! ungrouped ranges (first stroke, stroke count), so that each group can be
//! rebuilt on its own.
std::vector<std::pair<int, int>> ungroupStrokes(TVectorImage *vimg,
                                                StrokeSelection *selection) {
  std::vector<std::pair<int, int>> ranges;
  for (int i = 0; i < (int)vimg->getStrokeCount();)
    if (selection->isSelected(i) && vimg->isStrokeGrouped(i)) {
      int count = vimg->ungroup(i);
      if (count <= 0) count = 1;
      ranges.push_back({i, count});
      i += count;
    } else
      i++;
  return ranges;
}

//-----------------------------------------------------------------------------

std::vector<int> selectedIndexes(TVectorImage *vimg,
                                 StrokeSelection *selection) {
  std::vector<int> indexes;
  for (int i = 0; i < (int)vimg->getStrokeCount(); i++)
    if (selection->isSelected(i)) indexes.push_back(i);
  return indexes;
}

//-----------------------------------------------------------------------------

//! Selects the given stroke ranges in the current stroke selection, if any:
//! strokes change index when grouping
void selectStrokes(const std::vector<std::pair<int, int>> &ranges) {
  StrokeSelection *selection = dynamic_cast<StrokeSelection *>(
      TTool::getApplication()->getCurrentSelection()->getSelection());
  if (!selection) return;
  selection->selectNone();
  for (const auto &range : ranges)
    for (int j = range.first; j < range.first + range.second; j++)
      selection->select(j, true);
}

//=============================================================================
// GroupUndo
//-----------------------------------------------------------------------------

class GroupUndo final : public ToolUtils::TToolUndo {
  std::vector<int> m_indexes;  //!< selected strokes, before grouping
  mutable std::vector<StrokeMove> m_moves;
  mutable int m_first;

public:
  GroupUndo(TXshSimpleLevel *level, const TFrameId &frameId,
            const std::vector<int> &indexes,
            const std::vector<StrokeMove> &moves, int first)
      : ToolUtils::TToolUndo(level, frameId)
      , m_indexes(indexes)
      , m_moves(moves)
      , m_first(first) {}

  // Undo used to only ungroup: the strokes brought together stayed moved
  void undo() const override {
    TVectorImageP image = m_level->getFrame(m_frameId, true);
    if (!image) return;
    QMutexLocker lock(image->getMutex());
    image->ungroup(m_first);
    for (auto it = m_moves.rbegin(); it != m_moves.rend(); ++it)
      image->moveStrokes(it->m_moveBefore - it->m_count, it->m_count,
                         it->m_from);
    selectStrokes({});
    notifyImageChanged();
  }

  // Redo used to group with the selection the first run had modified
  void redo() const override {
    TVectorImageP image = m_level->getFrame(m_frameId, true);
    if (!image) return;
    QMutexLocker lock(image->getMutex());
    m_moves.clear();
    m_first = groupStrokes(image.getPointer(), m_indexes, m_moves);
    selectStrokes({{m_first, (int)m_indexes.size()}});
    notifyImageChanged();
  }

  int getSize() const override {
    return sizeof(*this) + m_indexes.size() * sizeof(int) +
           m_moves.size() * sizeof(StrokeMove);
  }

  QString getToolName() override { return QObject::tr("Group"); }
};

//=============================================================================
// UngroupUndo
//-----------------------------------------------------------------------------

class UngroupUndo final : public ToolUtils::TToolUndo {
  std::vector<std::pair<int, int>> m_ranges;  //!< ungrouped groups

public:
  UngroupUndo(TXshSimpleLevel *level, const TFrameId &frameId,
              const std::vector<std::pair<int, int>> &ranges)
      : ToolUtils::TToolUndo(level, frameId), m_ranges(ranges) {}

  // Each group is rebuilt on its own: regrouping the whole selection made
  // one group of all of them (#1840)
  void undo() const override {
    TVectorImageP image = m_level->getFrame(m_frameId, true);
    if (!image) return;
    QMutexLocker lock(image->getMutex());
    for (const auto &range : m_ranges) image->group(range.first, range.second);
    selectStrokes(m_ranges);
    notifyImageChanged();
  }

  void redo() const override {
    TVectorImageP image = m_level->getFrame(m_frameId, true);
    if (!image) return;
    QMutexLocker lock(image->getMutex());
    for (const auto &range : m_ranges) image->ungroup(range.first);
    selectStrokes(m_ranges);
    notifyImageChanged();
  }

  int getSize() const override {
    return sizeof(*this) + m_ranges.size() * sizeof(std::pair<int, int>);
  }

  QString getToolName() override { return QObject::tr("Ungroup"); }
};

//=============================================================================
// MoveGroupUndo
//-----------------------------------------------------------------------------

class MoveGroupUndo final : public ToolUtils::TToolUndo {
  UCHAR m_moveType;
  int m_refStroke, m_count, m_moveBefore;
  std::vector<std::pair<TStroke *, int>> m_selectedGroups;

public:
  MoveGroupUndo(TXshSimpleLevel *level, const TFrameId &frameId, UCHAR moveType,
                int refStroke, int count, int moveBefore,
                const std::vector<std::pair<TStroke *, int>> &selectedGroups)
      : ToolUtils::TToolUndo(level, frameId)
      , m_moveType(moveType)
      , m_refStroke(refStroke)
      , m_count(count)
      , m_moveBefore(moveBefore)
      , m_selectedGroups(selectedGroups) {}

  ~MoveGroupUndo() {}

  void undo() const override {
    // moved up (front, forward) or down (back, backward)
    bool movedUp = m_moveBefore > m_refStroke;
    move(movedUp ? m_moveBefore - m_count : m_moveBefore,
         movedUp ? m_refStroke : m_refStroke + m_count);
  }

  void redo() const override { move(m_refStroke, m_moveBefore); }

  void move(int refStroke, int moveBefore) const {
    TVectorImageP image = m_level->getFrame(m_frameId, true);
    if (!image) return;
    QMutexLocker lock(image->getMutex());
    image->moveStrokes(refStroke, m_count, moveBefore);
    // select the moved strokes in the current stroke selection
    std::vector<std::pair<int, int>> ranges;
    for (const auto &group : m_selectedGroups) {
      int index = image->getStrokeIndex(group.first);
      if (index != -1) ranges.push_back({index, group.second});
    }
    selectStrokes(ranges);
    TTool::getApplication()->getCurrentScene()->notifySceneChanged();
    notifyImageChanged();
  }

  int getSize() const override { return sizeof(*this); }

  QString getToolName() override {
    static QMap<int, QString> commandTypeStrMap{
        {TGroupCommand::FRONT, QObject::tr(" to Front")},
        {TGroupCommand::FORWARD, QObject::tr(" to Forward")},
        {TGroupCommand::BACK, QObject::tr(" to Back")},
        {TGroupCommand::BACKWARD, QObject::tr(" to Backward")}};

    return QObject::tr("Move Group") + commandTypeStrMap.value(m_moveType);
  }
};

//-----------------------------------------------------------------------------
}  // namespace
//-----------------------------------------------------------------------------
// TGroupCommand
//-----------------------------------------------------------------------------

namespace {

std::vector<std::pair<TStroke *, int>> getSelectedGroups(TVectorImage *vimg,
                                                         StrokeSelection *sel) {
  UINT i, j;
  std::vector<std::pair<TStroke *, int>> ret;

  for (i = 0; i < vimg->getStrokeCount(); i++)
    if (sel->isSelected(i)) {
      if (vimg->isStrokeGrouped(i)) {
        for (j = i + 1; j < vimg->getStrokeCount() && vimg->sameSubGroup(i, j);
             j++)
          if (!sel->isSelected(j))
            return std::vector<std::pair<TStroke *, int>>();
        ret.push_back(std::pair<TStroke *, int>(vimg->getStroke(i), j - i));
        i = j - 1;
      } else
        ret.push_back(std::pair<TStroke *, int>(vimg->getStroke(i), 1));
    }
  return ret;
}

}  // namespace

//--------------------------------------------------------------------------------------

UCHAR TGroupCommand::getGroupingOptions() {
  TTool *tool = TTool::getApplication()->getCurrentTool()->getTool();
  if (!tool) return 0;
  TVectorImage *vimg = dynamic_cast<TVectorImage *>(tool->getImage(false));
  if (!vimg) return 0;

  UCHAR mask = 0;
  UINT i, j;

  // spostamento: si possono  spostare solo gruppi interi  oppure  stroke   non
  // gruppate

  std::vector<std::pair<TStroke *, int>> strokeIndexes =
      getSelectedGroups(vimg, m_sel);

  if (strokeIndexes.empty()) return 0;  // no stroke selected

  int strokeIndex = vimg->getStrokeIndex(strokeIndexes[0].first);

  if (strokeIndexes.size() > 1 || strokeIndex > 0) {
    mask |= BACK;
    mask |= BACKWARD;
  }
  if (strokeIndexes.size() > 1 ||
      strokeIndex + strokeIndexes[0].second - 1 <
          (int)vimg->getStrokeCount() - 1) {
    mask |= FRONT;
    mask |= FORWARD;
  }

  // PER l'UNGROUP: si ungruppa solo se tutti gli stroke selezionati stanno nel
  // gruppo (anche piu' gruppi insieme)

  for (i = 0; i < vimg->getStrokeCount(); i++) {
    if (m_sel->isSelected(i)) {
      if (!vimg->isStrokeGrouped(i)) break;
      for (j = 0; j < vimg->getStrokeCount(); j++)
        if (!m_sel->isSelected(j) && vimg->sameSubGroup(i, j)) break;
      if (j < vimg->getStrokeCount()) break;
    }
  }
  if (i == vimg->getStrokeCount()) mask |= UNGROUP;

  // PER il GROUP: si raggruppa solo  se:
  // //almeno una delle  stroke selezionate non fa parte di gruppi o e' di un
  // gruppo diverso
  // e se c'e' una stroke di un gruppo, allora tutto il gruppo della stroke e'
  // selezionato

  bool groupingMakesSense = false;
  int refStroke           = -1;
  for (i = 0; i < vimg->getStrokeCount(); i++)
    if (m_sel->isSelected(i)) {
      if (vimg->isStrokeGrouped(i)) {
        if (refStroke == -1)
          refStroke = i;
        else if (!vimg->sameSubGroup(refStroke, i))
          groupingMakesSense = true;  // gli storke selezionati non sono gia'
                                      // tutti dello stesso gruppo
        for (j = 0; j < vimg->getStrokeCount(); j++)
          if (!m_sel->isSelected(j) && vimg->sameGroup(i, j)) return mask;
      } else
        groupingMakesSense = true;
    }

  if (groupingMakesSense) mask |= GROUP;

  return mask;
}

//-----------------------------------------------------------------------------

void TGroupCommand::group() {
  if (!(getGroupingOptions() & GROUP)) return;

  TTool *tool = TTool::getApplication()->getCurrentTool()->getTool();
  if (!tool) return;
  TVectorImage *vimg = (TVectorImage *)tool->getImage(true);

  if (!vimg) return;

  if (!m_sel->isEditable()) {
    DVGui::error(
        QObject::tr("The selection cannot be grouped. It is not editable."));
    return;
  }

  QMutexLocker lock(vimg->getMutex());
  std::vector<int> indexes = selectedIndexes(vimg, m_sel);
  std::vector<StrokeMove> moves;
  int first = groupStrokes(vimg, indexes, moves);
  // the grouped strokes may have moved: keep them selected
  m_sel->selectNone();
  for (int j = first; j < first + (int)indexes.size(); j++)
    m_sel->select(j, true);
  tool->notifyImageChanged();
  TXshSimpleLevel *level =
      TTool::getApplication()->getCurrentLevel()->getSimpleLevel();
  TUndoManager::manager()->add(
      new GroupUndo(level, tool->getCurrentFid(), indexes, moves, first));
}

//-----------------------------------------------------------------------------

void TGroupCommand::enterGroup() {
  TTool *tool = TTool::getApplication()->getCurrentTool()->getTool();
  if (!tool) return;
  TVectorImage *vimg = (TVectorImage *)tool->getImage(true);

  if (!vimg) return;

  if (!m_sel->isEditable()) {
    DVGui::error(
        QObject::tr("The selection cannot be entered. It is not editable."));
    return;
  }

  int index = -1;

  for (int i = 0; i < (int)vimg->getStrokeCount(); i++)
    if (m_sel->isSelected(i)) {
      index = i;
      break;
    }

  if (index == -1) return;

  if (!vimg->canEnterGroup(index)) return;
  vimg->enterGroup(index);
  TSelection *selection = TSelection::getCurrent();
  if (selection) selection->selectNone();

  TTool::getApplication()->getCurrentScene()->notifySceneChanged();
}

//-----------------------------------------------------------------------------

void TGroupCommand::exitGroup() {
  TTool *tool = TTool::getApplication()->getCurrentTool()->getTool();
  if (!tool) return;
  TVectorImage *vimg = (TVectorImage *)tool->getImage(true);

  if (!vimg) return;
  vimg->exitGroup();
  TTool::getApplication()->getCurrentScene()->notifySceneChanged();
}

//-----------------------------------------------------------------------------

void TGroupCommand::ungroup() {
  if (!(getGroupingOptions() & UNGROUP)) return;
  TTool *tool = TTool::getApplication()->getCurrentTool()->getTool();
  if (!tool) return;
  TVectorImage *vimg = (TVectorImage *)tool->getImage(true);
  if (!vimg) return;

  if (!m_sel->isEditable()) {
    DVGui::error(
        QObject::tr("The selection cannot be ungrouped. It is not editable."));
    return;
  }

  QMutexLocker lock(vimg->getMutex());
  std::vector<std::pair<int, int>> ranges = ungroupStrokes(vimg, m_sel);
  tool->notifyImageChanged();
  TXshSimpleLevel *level =
      TTool::getApplication()->getCurrentLevel()->getSimpleLevel();
  TUndoManager::manager()->add(
      new UngroupUndo(level, tool->getCurrentFid(), ranges));
}

//-----------------------------------------------------------------------------


static int doMoveGroup(
    UCHAR moveType, TVectorImage *vimg,
    const std::vector<std::pair<TStroke *, int>> &selectedGroups, int index) {
  int refStroke = vimg->getStrokeIndex(selectedGroups[index].first);
  int count     = selectedGroups[index].second;

  int moveBefore;
  switch (moveType) {
  case TGroupCommand::FRONT:
    moveBefore = vimg->getStrokeCount();

    while (moveBefore >= refStroke + count + 1 &&
           !vimg->canMoveStrokes(refStroke, count, moveBefore))
      moveBefore--;
    if (moveBefore == refStroke + count) return -1;

    break;
  case TGroupCommand::FORWARD:
    moveBefore = refStroke + count + 1;
    while (moveBefore <= (int)vimg->getStrokeCount() &&
           !vimg->canMoveStrokes(refStroke, count, moveBefore))
      moveBefore++;
    if (moveBefore == vimg->getStrokeCount() + 1) return -1;
    break;
  case TGroupCommand::BACK:
    moveBefore = 0;
    while (moveBefore <= refStroke - 1 &&
           !vimg->canMoveStrokes(refStroke, count, moveBefore))
      moveBefore++;
    if (moveBefore == refStroke) return -1;
    break;
  case TGroupCommand::BACKWARD:
    moveBefore = refStroke - 1;
    while (moveBefore >= 0 &&
           !vimg->canMoveStrokes(refStroke, count, moveBefore))
      moveBefore--;
    if (moveBefore == -1) return -1;
    break;
  default:
    assert(false);
  }

  vimg->moveStrokes(refStroke, count, moveBefore);

  TXshSimpleLevel *level =
      TTool::getApplication()->getCurrentLevel()->getSimpleLevel();
  TTool *tool = TTool::getApplication()->getCurrentTool()->getTool();
  TUndoManager::manager()->add(new MoveGroupUndo(level, tool->getCurrentFid(),
                                                 moveType, refStroke, count,
                                                 moveBefore, selectedGroups));

  return moveBefore;
}

//-----------------------------------------------------------------------------

void TGroupCommand::moveGroup(UCHAR moveType) {
  TTool *tool = TTool::getApplication()->getCurrentTool()->getTool();
  if (!tool) return;

  TVectorImage *vimg = (TVectorImage *)tool->getImage(true);
  if (!vimg) return;

  if (!m_sel->isEditable()) {
    DVGui::error(
        QObject::tr("The selection cannot be moved. It is not editable."));
    return;
  }

  std::vector<std::pair<TStroke *, int>> selectedGroups =
      getSelectedGroups(vimg, m_sel);
  if (selectedGroups.empty()) return;

  int i, j;

  TUndoManager::manager()->beginBlock();

  switch (moveType) {
  case TGroupCommand::FRONT:
  case TGroupCommand::BACKWARD:
    i = 0;
    if (moveType == TGroupCommand::BACKWARD &&
        vimg->getStrokeIndex(selectedGroups[i].first) ==
            0)  // tutti i gruppi adiacenti gia in fondo non possono essere
                // backwardati
    {
      i++;
      while (i < (int)selectedGroups.size() &&
             vimg->getStrokeIndex(selectedGroups[i - 1].first) +
                     selectedGroups[i - 1].second - 1 ==
                 vimg->getStrokeIndex(selectedGroups[i].first) - 1)
        i++;
    }
    for (; i <= (int)selectedGroups.size() - 1; i++)
      doMoveGroup(moveType, vimg, selectedGroups,
                  i);  // vimg->getStrokeIndex(selectedGroups[i].first),
                       // selectedGroups[i].second);
    break;

  case TGroupCommand::BACK:
  case TGroupCommand::FORWARD:
    i = selectedGroups.size() - 1;
    if (moveType == TGroupCommand::FORWARD &&
        vimg->getStrokeIndex(selectedGroups[i].first) +
                selectedGroups[i].second - 1 ==
            vimg->getStrokeCount() - 1)  // tutti i gruppi adiacenti gia in cime
                                         // non possono essere forwardati
    {
      i--;
      while (i >= 0 &&
             vimg->getStrokeIndex(selectedGroups[i + 1].first) - 1 ==
                 vimg->getStrokeIndex(selectedGroups[i].first) +
                     selectedGroups[i].second - 1)
        i--;
    }
    for (; i >= 0; i--) doMoveGroup(moveType, vimg, selectedGroups, i);
    break;

  default:
    assert(false);
  }

  TUndoManager::manager()->endBlock();

  m_sel->selectNone();

  for (i = 0; i < (int)selectedGroups.size(); i++) {
    int index = vimg->getStrokeIndex(selectedGroups[i].first);
    for (j = index; j < index + selectedGroups[i].second; j++)
      m_sel->select(j, true);
  }

  tool->notifyImageChanged();
}

//-----------------------------------------------------------------------------

void TGroupCommand::addMenuItems(QMenu *menu) {
  UCHAR optionMask = getGroupingOptions();
  if (optionMask == 0) return;

  CommandManager *cm = CommandManager::instance();

  if (optionMask & TGroupCommand::GROUP)
    menu->addAction(cm->getAction(MI_Group));

  if (optionMask & TGroupCommand::UNGROUP)
    menu->addAction(cm->getAction(MI_Ungroup));

  if (optionMask & (TGroupCommand::GROUP | TGroupCommand::UNGROUP) &&
      optionMask & (TGroupCommand::FORWARD | TGroupCommand::BACKWARD))
    menu->addSeparator();

  if (optionMask & TGroupCommand::FORWARD) {
    menu->addAction(cm->getAction(MI_BringToFront));
    menu->addAction(cm->getAction(MI_BringForward));
  }
  if (optionMask & TGroupCommand::BACKWARD) {
    menu->addAction(cm->getAction(MI_SendBack));
    menu->addAction(cm->getAction(MI_SendBackward));
  }
  menu->addSeparator();
}
