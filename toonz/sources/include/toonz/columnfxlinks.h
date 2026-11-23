#pragma once

#ifndef COLUMNFXLINKS_H
#define COLUMNFXLINKS_H

// TnzCore includes
#include "tcommon.h"

// STD includes
#include <map>
#include <set>
#include <string>
#include <vector>

#undef DVAPI
#undef DVVAR
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//=============================================================================

//  Forward declarations

class TFx;
class TFxPort;
class TXsheet;

//=============================================================================

//! Fx schematic connections of columns, to restore them exactly when undoing
//! their removal. Dynamic port groups (e.g. Over Fx) drop and renumber their
//! empty ports once a column is removed, so their whole configuration is
//! stored by port name instead of port pointers. The xsheet link of each
//! column fx is restored as it was.
class DVAPI ColumnFxLinks {
  struct DynamicLink {
    int m_groupIndex;
    std::string m_portName;
    TFx *m_inputFx;
  };

  std::map<TFxPort *, TFx *> m_links;  //!< plain ports -> column fx
  std::map<TFx *, std::vector<DynamicLink>> m_dynamicLinks;
  std::map<TFx *, bool> m_terminal;  //!< column fx -> linked to the xsheet

public:
  //! Stores the output connections of the given columns' fxs
  void store(TXsheet *xsh, const std::set<int> &indices);
  //! Reconnects them; the column fxs must be back in the xsheet
  void restore(TXsheet *xsh) const;
};

#endif  // COLUMNFXLINKS_H
