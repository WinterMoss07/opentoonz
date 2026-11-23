#pragma once

#ifndef T_PINNED_RANGE_SET_INCLUDED
#define T_PINNED_RANGE_SET_INCLUDED

#include "tstageobject.h"

#undef DVAPI
#undef DVVAR
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

class DVAPI TPinnedRangeSet {
public:
  struct Range {
    int first, second;
    Range(int _first, int _second) : first(_first), second(_second) {}
  };

  TPinnedRangeSet();
  ~TPinnedRangeSet() {}


  void setPlacement(const TAffine &placement);
  const TAffine &getPlacement() const { return m_placement; }
  void setRange(int first, int second);
  void removeAllRanges();

  int isPinned(int frame) const { return getRangeIndex(frame) >= 0; }

  const Range *getRange(int frame) const {
    int rangeIndex = getRangeIndex(frame);
    return rangeIndex >= 0 ? &m_ranges[rangeIndex] : 0;
  }


  int getRangeCount() const { return (int)m_ranges.size(); }

  TPinnedRangeSet *clone();

  void loadData(TIStream &is);
  void saveData(TOStream &os);

private:
  TStageObject *m_stageObject;
  std::vector<Range> m_ranges;
  TAffine m_placement;
  int getRangeIndex(int frame) const;
};

#endif
