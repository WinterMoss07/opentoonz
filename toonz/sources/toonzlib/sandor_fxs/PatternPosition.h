#pragma once

// PatternPosition.h: interface for the CPatternPosition class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(                                                                  \
    AFX_PATTERNPOSITION_H__9FB82504_34F2_11D6_B9E8_0040F674BE6A__INCLUDED_)
#define AFX_PATTERNPOSITION_H__9FB82504_34F2_11D6_B9E8_0040F674BE6A__INCLUDED_

#include <vector>

#include <stdlib.h>
#include "STColSelPic.h"
#include "SError.h"
#include "SDef.h"

class CPatternPosition {
  void makeRandomPositions(const int nbPat, const int nbPixel, const int lX,
                           const int lY, const UCHAR *sel);
  void makeDDPositions(const int lX, const int lY, UCHAR *sel,
                       const double minD, const double maxD);
  void getPosAroundThis(const int lX, const int lY, const UCHAR *lSel,
                        const int xx, const int yy, int &xPos, int &yPos);
  bool findEmptyPos(const int lX, const int lY, const UCHAR *lSel, int &xPos,
                    int &yPos, SRECT &bb);
  void eraseCurrentArea(const int lX, const int lY, UCHAR *lSel,
                        std::vector<SPOINT> &ddc, const int xx, const int yy);
  void sel0255To01(const int lX, const int lY, UCHAR *sel, SRECT &bb);
  void prepareCircle(std::vector<SPOINT> &v, const double r);

public:
  std::vector<SPOINT> m_pos;

  CPatternPosition() : m_pos(0){};
  virtual ~CPatternPosition();

  template <class P>
  CPatternPosition(CSTColSelPic<P> &p, const int nbPixel, const double dens,
                   const double minD, const double maxD) {
    try {
      m_pos.clear();
      if (dens > 0.00001) {
        // Random position
        int nbPat = I_ROUND(((double)nbPixel * dens));
        //			nbPat= nbPat>(int)(0.9*nbPixel) ?
        //(int)(0.9*nbPixel) : nbPat;
        nbPat = nbPat > nbPixel ? nbPixel : nbPat;
        if (nbPat > 0)
          makeRandomPositions(nbPat, nbPixel, p.m_lX, p.m_lY, p.m_sel.get());
      } else {
        // Distance-driven position
        makeDDPositions(p.m_lX, p.m_lY, p.m_sel.get(), minD, maxD);
      }
    } catch (SMemAllocError) {
      throw;
    }
  }

};

#endif  // !defined(AFX_PATTERNPOSITION_H__9FB82504_34F2_11D6_B9E8_0040F674BE6A__INCLUDED_)
