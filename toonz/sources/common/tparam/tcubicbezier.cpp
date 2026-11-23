

#include "tcubicbezier.h"

//-----------------------------------------------------------------------------

DVAPI double invCubicBezierX(double x, const TPointD &a, const TPointD &aSpeed,
                             const TPointD &bSpeed, const TPointD &b) {
  double aSpeedX            = aSpeed.x;
  double bSpeedX            = bSpeed.x;
  if (aSpeedX == 0) aSpeedX = epsilon;
  if (bSpeedX == 0) bSpeedX = -epsilon;

  // a*u^3+b*u^2+c*u+d
  double x0 = a.x;
  double x1 = x0 + aSpeedX;
  double x3 = b.x;
  double x2 = x3 + bSpeedX;

  double aX = x3 + 3 * (x1 - x2) - x0;
  double bX = 3 * (x2 - 2 * x1 + x0);
  double cX = 3 * (x1 - x0);
  double dX = x0 - x;

  return cubicRoot(aX, bX, cX, dX);
}

//-----------------------------------------------------------------------------

DVAPI double getCubicBezierY(double x, const TPointD &a, const TPointD &aSpeed,
                             const TPointD &bSpeed, const TPointD &b) {
  double y0 = a.y;
  double y1 = y0 + aSpeed.y;
  double y3 = b.y;
  double y2 = y3 + bSpeed.y;

  double aY = y3 + 3 * (y1 - y2) - y0;
  double bY = 3 * (y2 - 2 * y1 + y0);
  double cY = 3 * (y1 - y0);
  double dY = y0;

  double u = invCubicBezierX(x, a, aSpeed, bSpeed, b);
  u        = tcrop(u, 0.0, 1.0);
  return aY * u * u * u + bY * u * u + cY * u + dY;
}

//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
