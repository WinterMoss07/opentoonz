

#include "toonz/columnfxlinks.h"

// TnzLib includes
#include "toonz/txsheet.h"
#include "toonz/txshcolumn.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"

// TnzBase includes
#include "tfx.h"
#include "trasterfx.h"

//-----------------------------------------------------------------------------

void ColumnFxLinks::store(TXsheet *xsh, const std::set<int> &indices) {
  FxDag *dag = xsh->getFxDag();
  for (int index : indices) {
    TXshColumn *column = xsh->getColumn(index);
    TFx *fx            = column ? column->getFx() : 0;
    if (!fx) continue;
    m_terminal[fx] = dag->getTerminalFxs()->containsFx(fx);
    for (int i = 0; i < fx->getOutputConnectionCount(); ++i) {
      TFxPort *port = fx->getOutputConnection(i);
      TFx *outFx    = port->getOwnerFx();
      // Only links whose output-connected fx is in the xsheet (ie not
      // temporary) are stored. Temporaries may be erased without an undo
      // notification...
      if (!outFx || !dag->getInternalFxs()->containsFx(outFx)) continue;
      if (port->getGroupIndex() < 0) {
        m_links[port] = fx;
        continue;
      }
      if (m_dynamicLinks.count(outFx)) continue;
      std::vector<DynamicLink> &links = m_dynamicLinks[outFx];
      for (int p = 0; p < outFx->getInputPortCount(); ++p) {
        TFxPort *inPort = outFx->getInputPort(p);
        if (inPort->getGroupIndex() >= 0)
          links.push_back({inPort->getGroupIndex(), outFx->getInputPortName(p),
                           inPort->getFx()});
      }
    }
  }
}

//-----------------------------------------------------------------------------

void ColumnFxLinks::restore(TXsheet *xsh) const {
  for (const auto &link : m_links) link.first->setFx(link.second);

  for (const auto &entry : m_dynamicLinks) {
    TFx *outFx = entry.first;
    for (int g = 0; g < outFx->dynamicPortGroupsCount(); ++g)
      outFx->clearDynamicPortGroup(g);
    for (const DynamicLink &link : entry.second) {
      TFxPort *port = new TRasterFxPort;
      port->setFx(link.m_inputFx);
      outFx->addInputPort(link.m_portName, port, link.m_groupIndex);
    }
  }

  // Restoring a column may link it to the xsheet node when it reached it
  // through another fx: keep the original state.
  TFxSet *terminals = xsh->getFxDag()->getTerminalFxs();
  for (const auto &entry : m_terminal) {
    if (entry.second)
      terminals->addFx(entry.first);
    else
      terminals->removeFx(entry.first);
  }
}
