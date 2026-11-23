

#include "pins.h"
#include "tfxparam.h"
#include "trop.h"
#include "tmathutil.h"
#include "tofflinegl.h"

#include <cmath>

//----------------------------------------------------------------------------

namespace {

//----------------------------------------------------------------------------

bool lineIntersection(const TPointD &P, const TPointD &R, const TPointD &Q,
                      const TPointD &S, TPointD &ret);

//----------------------------------------------------------------------------

bool lineIntersection(const TPointD &P, const TPointD &R, const TPointD &Q,
                      const TPointD &S, TPointD &ret) {
  TPointD u = R - P;
  TPointD v = S - Q;
  double r;
  if (u.y * v.x - u.x * v.y != 0) {
    r = (P.x * u.y - Q.x * u.y + u.x * (Q.y - P.y)) / (u.y * v.x - u.x * v.y);
    assert(!std::isnan(r));
    ret = Q + v * r;
    assert(!std::isnan(ret.x) && !std::isnan(ret.y));
    return true;
  } else {
    ret = P;
    assert(!std::isnan(ret.x) && !std::isnan(ret.y));
    return false;
  }
}

//----------------------------------------------------------------------------
};

#ifndef checkErrorsByGL
#define checkErrorsByGL                                                        \
  {                                                                            \
    GLenum err = glGetError();                                                 \
    assert(err != GL_INVALID_ENUM);                                            \
    assert(err != GL_INVALID_VALUE);                                           \
    assert(err != GL_INVALID_OPERATION);                                       \
    assert(err != GL_STACK_OVERFLOW);                                          \
    assert(err != GL_STACK_UNDERFLOW);                                         \
    assert(err != GL_OUT_OF_MEMORY);                                           \
    assert(err == GL_NO_ERROR);                                                \
  }
#endif

//----------------------------------------------------------------------------


// ------------------------------------------------------------------------

void subdivision(const TPointD &p00, const TPointD &p10, const TPointD &p11,
                 const TPointD &p01, const TPointD &tex00, const TPointD &tex10,
                 const TPointD &tex11, const TPointD &tex01,
                 const TRectD &clippingRect, int details) {
  if (details == 1) {
    glBegin(GL_QUADS);
    glTexCoord2d(tex00.x, tex00.y);
    tglVertex(p00);
    glTexCoord2d(tex10.x, tex10.y);
    tglVertex(p10);
    glTexCoord2d(tex11.x, tex11.y);
    tglVertex(p11);
    glTexCoord2d(tex01.x, tex01.y);
    tglVertex(p01);
    glEnd();
  } else {
    TPointD A = p00;
    TPointD B = p10;
    TPointD C = p11;
    TPointD D = p01;

/*
*     D                L2               C
*     +----------------+----------------+
*     |                |                |
*     |                |                |
*     |                |                |
*     |                |                |
*     |                |                |
*  H1 +----------------+----------------+ H2
*     |                | M              |
*     |                |                |
*     |                |                |
*     |                |                |
*     |                |                |
*     +----------------+----------------+
*     A                L1               B
*
*/
    TPointD M, L1, L2, H1, H2, P1, P2;
    bool intersection;

    // M
    intersection = lineIntersection(A, C, B, D, M);
    assert(intersection);

    // P1 (vanishing point)
    intersection = lineIntersection(D, C, A, B, P1);
    if (!intersection) {
      P1.x = 0.5 * (A.x + D.x);
      P1.y = 0.5 * (A.y + D.y);
    }
    // H1
    intersection = lineIntersection(A, D, P1, M, H1);
    assert(intersection);

    // H2
    intersection = lineIntersection(B, C, P1, M, H2);
    assert(intersection);

    // P2 (vanishing point)
    intersection = lineIntersection(A, D, B, C, P2);
    if (!intersection) {
      P2.x = 0.5 * (A.x + B.x);
      P2.y = 0.5 * (A.y + B.y);
    }
    // L1
    intersection = lineIntersection(A, B, P2, M, L1);
    assert(intersection);

    // L2
    intersection = lineIntersection(D, C, P2, M, L2);
    assert(intersection);

    TPointD texA = (tex00 + tex10) * 0.5;
    TPointD texB = (tex10 + tex11) * 0.5;
    TPointD texC = (tex11 + tex01) * 0.5;
    TPointD texD = (tex01 + tex00) * 0.5;
    TPointD texM = (texA + texC) * 0.5;

    details--;

    TRectD r1 = TRectD(
        std::min({A.x, L1.x, M.x, H1.x}), std::min({A.y, L1.y, M.y, H1.y}),
        std::max({A.x, L1.x, M.x, H1.x}), std::max({A.y, L1.y, M.y, H1.y}));

    TRectD r2 = TRectD(
        std::min({L1.x, B.x, H2.x, M.x}), std::min({L1.y, B.y, H2.y, M.y}),
        std::max({L1.x, B.x, H2.x, M.x}), std::max({L1.y, B.y, H2.y, M.y}));

    TRectD r3 = TRectD(
        std::min({M.x, H2.x, C.x, L2.x}), std::min({M.y, H2.y, C.y, L2.y}),
        std::max({M.x, H2.x, C.x, L2.x}), std::max({M.y, H2.y, C.y, L2.y}));

    TRectD r4 = TRectD(
        std::min({H1.x, M.x, L2.x, D.x}), std::min({H1.y, M.y, L2.y, D.y}),
        std::max({H1.x, M.x, L2.x, D.x}), std::max({H1.y, M.y, L2.y, D.y}));

    if (r1.overlaps(clippingRect))
      subdivision(A, L1, M, H1, tex00, texA, texM, texD, clippingRect, details);
    if (r2.overlaps(clippingRect))
      subdivision(L1, B, H2, M, texA, tex10, texB, texM, clippingRect, details);
    if (r3.overlaps(clippingRect))
      subdivision(M, H2, C, L2, texM, texB, tex11, texC, clippingRect, details);
    if (r4.overlaps(clippingRect))
      subdivision(H1, M, L2, D, texD, texM, texC, tex01, clippingRect, details);
  }
}

// ----------------------------------------------------------------------------

#define TINY 1.0e-20

static int splitMatrix(double **a, int n, int *index) {
  int i, imax = 0, j, k;
  double big, dum, sum, temp;
  double *vv, d;

  vv = new double[n];
  d = 1.00;
  
  for (i = 0; i < n; i++) {
    big = 0.0;
    for (j = 0; j < n; j++)
      if ((temp = fabs(a[i][j])) > big) big = temp;
    if (big == 0.0) {
      /* Matrix is singular! */
      delete[] vv; // Fixed: Clean up before returning (first case)
      return 0;
    }
    vv[i] = 1.0 / big;
  }
  
  for (j = 0; j < n; j++) {
    for (i = 0; i < j; i++) {
      sum = a[i][j];
      for (k  = 0; k < i; k++) sum -= a[i][k] * a[k][j];
      a[i][j] = sum;
    }
    
    big = 0.0;
    for (i = j; i < n; i++) {
      sum = a[i][j];
      for (k  = 0; k < j; k++) sum -= a[i][k] * a[k][j];
      a[i][j] = sum;
      if ((dum = vv[i] * fabs(sum)) >= big) {
        big  = dum;
        imax = i;
      }
    }
    
    if (j != imax) {
      for (k = 0; k < n; k++) {
        dum        = a[imax][k];
        a[imax][k] = a[j][k];
        a[j][k]    = dum;
      }
      d        = -d;
      vv[imax] = vv[j];
    }
    
    index[j] = imax;
    if (std::abs(a[j][j]) <= TINY && (j != n - 1)) {
      delete[] vv; // Fixed: Clean up before returning (second case)
      return imax + 1;
    }
    
    if (j != n - 1) {
      dum = 1.0 / a[j][j];
      for (i = j + 1; i < n; i++) a[i][j] *= dum;
    }
  }
  
  delete[] vv;
  return 0;
}

// ----------------------------------------------------------------------------

static void buildMatrixes(const FourPoints &ss, const FourPoints &dd,
                          double **a, double *b) {
  int i;
  TPointD s[4], d[4];

  s[0] = ss.m_p00, s[1] = ss.m_p01, s[2] = ss.m_p10, s[3] = ss.m_p11;
  d[0] = dd.m_p00, d[1] = dd.m_p01, d[2] = dd.m_p10, d[3] = dd.m_p11;

  for (i = 0; i < 4; i++) {
    a[i][0] = a[i + 4][3] = s[i].x;
    a[i][1] = a[i + 4][4] = s[i].y;
    a[i][2] = a[i + 4][5] = 1;
    a[i][3] = a[i + 4][0] = 0;
    a[i][4] = a[i + 4][1] = 0;
    a[i][5] = a[i + 4][2] = 0;
    a[i][6]               = -s[i].x * d[i].x;
    a[i + 4][6]           = -s[i].x * d[i].y;
    a[i][7]               = -s[i].y * d[i].x;
    a[i + 4][7]           = -s[i].y * d[i].y;

    b[i]     = d[i].x;
    b[i + 4] = d[i].y;
  }
}

// ----------------------------------------------------------------------------

static void computeSolutions(double **a, int *index, double *b) {
  int i, ii = 0, ip, j;
  double sum;

  for (i = 0; i < 8; i++) {
    ip    = index[i];
    sum   = b[ip];
    b[ip] = b[i];
    if (ii)
      for (j = ii - 1; j <= i - 1; j++) sum -= a[i][j] * b[j];
    else if (sum)
      ii = i + 1;
    b[i] = sum;
  }
  
  for (i = 7; i >= 0; i--) {
    sum = b[i];
    for (j = i + 1; j < 8; j++) sum -= a[i][j] * b[j];
    b[i]   = sum / a[i][i];
  }
}

// ----------------------------------------------------------------------------

static void solveSystems(double **a, double *bx) {
  int index[255], i, count = 0, bad_line;
  double **atmp;
  int n = 8;

  atmp = new double *[n];
  for (i = 0; i < n; i++) {
    atmp[i] = new double[n];
    memcpy(atmp[i], a[i], n * sizeof(double));
  }

  while ((bad_line = splitMatrix(atmp, n, index)) != 0 && n > 0) {
    /* Line %d is problematic!\n", bad_line);*/
    for (i = bad_line - 1; i < n - 1; i++)
      memcpy(atmp[i], atmp[i + 1], n * sizeof(double));
    n--;
    count++;
  }

  if (n > 0) {
    computeSolutions(atmp, index, bx);
  }
  // FIXED: Free all allocated rows (8 rows), not just n
  for (i = 0; i < 8; i++) {
    delete[] atmp[i];
  }
  delete[] atmp;
}

// ----------------------------------------------------------------------------

static void computeTransformation(const FourPoints &s, const FourPoints &d,
                                  TAffine &aff, TPointD &perspectDen) {
  double **a, *b; 
  int i;
  
  a = new double *[8];
  for (i = 0; i < 8; i++) a[i] = new double[8];
  b                            = new double[8];

  buildMatrixes(s, d, a, b);
  solveSystems(a, b);

  aff.a11       = b[0];
  aff.a12       = b[1];
  aff.a13       = b[2];
  aff.a21       = b[3];
  aff.a22       = b[4];
  aff.a23       = b[5];
  perspectDen.x = b[6];
  perspectDen.y = b[7];

  for (i = 0; i < 8; i++) delete[] a[i];
  delete[] b;
  delete[] a;
}

// ----------------------------------------------------------------------------

