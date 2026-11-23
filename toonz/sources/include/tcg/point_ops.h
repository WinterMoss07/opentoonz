#pragma once

#ifndef TCG_POINT_OPS_H
#define TCG_POINT_OPS_H

// tcg includes
#include "point.h"
#include "numeric_ops.h"

/*!
  \file     tcg_point_ops.h

  \brief    This file contains useful functions to deal with \a planar objects
            (\a two dimensions) up to points and lines.
*/

//*****************************************************************************
//    (Planar) Point  Operations
//*****************************************************************************

namespace tcg {

/*!
  Contains useful functions to deal with \a planar objects
  (\a two dimensions) up to points and lines.
*/

namespace point_ops {

template <typename Point>
inline Point NaP() {
  return Point(numeric_ops::NaN<typename point_traits<Point>::value_type>(),
               numeric_ops::NaN<typename point_traits<Point>::value_type>());
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline Point operator+(const Point &a, const Point &b) {
  return Point(a.x + b.x, a.y + b.y);
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline Point operator-(const Point &a, const Point &b) {
  return Point(a.x - b.x, a.y - b.y);
}

//-------------------------------------------------------------------------------------------

template <typename Point, typename Scalar>
inline Point operator*(Scalar a, const Point &b) {
  return Point(a * b.x, a * b.y);
}

//-------------------------------------------------------------------------------------------

template <typename Point, typename Scalar>
inline Point operator/(const Point &a, Scalar b) {
  return Point(a.x / b, a.y / b);
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline typename point_traits<Point>::value_type operator*(const Point &a,
                                                          const Point &b) {
  return a.x * b.x + a.y * b.y;
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline typename point_traits<Point>::value_type cross(const Point &a,
                                                      const Point &b) {
  return a.x * b.y - a.y * b.x;
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline typename point_traits<Point>::value_type norm2(const Point &a) {
  return a * a;
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline typename point_traits<Point>::float_type norm(const Point &a) {
  return sqrt(typename point_traits<Point>::float_type(a * a));
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline typename point_traits<Point>::value_type dist2(const Point &a,
                                                      const Point &b) {
  return norm2(b - a);
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline typename point_traits<Point>::value_type dist(const Point &a,
                                                     const Point &b) {
  return norm(b - a);
}

//-------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------

template <typename Point>
inline Point normalized(const Point &v) {
  return v / norm(v);
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline Point normalized(const Point &v,
                        typename point_traits<Point>::value_type tol) {
  typename point_traits<Point>::value_type vNorm = norm(v);
  return (vNorm >= tol) ? v / vNorm : NaP<Point>();
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline void normalize(Point &v) {
  v = normalized(v);
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline bool normalize(Point &v, typename point_traits<Point>::value_type tol) {
  typename point_traits<Point>::value_type vNorm = norm(v);
  return (vNorm >= tol) ? (v = v / vNorm, true) : (v = NaP<Point>(), false);
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline Point direction(const Point &a, const Point &b) {
  return normalized(b - a);
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline Point direction(const Point &a, const Point &b,
                       typename point_traits<Point>::value_type tol) {
  return normalized<Point>(b - a, tol);
}

//-------------------------------------------------------------------------------------------

//! Returns the left orthogonal of passed point
template <typename Point>
inline Point ortLeft(const Point &p) {
  return Point(-p.y, p.x);
}

//-------------------------------------------------------------------------------------------

//! Returns the right orthogonal of passed point
template <typename Point>
inline Point ortRight(const Point &p) {
  return Point(p.y, -p.x);
}

//-------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------

//! Returns the projection of point \p p on line <TT>a -> d</TT>
template <typename Point>
inline Point projection(const Point &p, const Point &a, const Point &d) {
  return a + ((p.x - a.x) * d.x + (p.y - a.y) * d.y) * d;
}

//-------------------------------------------------------------------------------------------

//! Returns res: <TT>p = p0 + res.x * (p1 - p0) + res.y * (p1 - p0)_orth</TT>
template <typename Point>
Point ortCoords(const Point &p, const Point &p0, const Point &p1) {
  Point v01(p1.x - p0.x, p1.y - p0.y);
  Point v02(p.x - p0.x, p.y - p0.y);

  typename point_traits<Point>::value_type den = v01.x * v01.x + v01.y * v01.y;

  return Point((v01.x * v02.x + v01.y * v02.y) / den,
               (v01.y * v02.x - v01.x * v02.y) / den);
}

//-------------------------------------------------------------------------------------------

//! Returns res: <TT>p = p0 + res.x * (p1 - p0) + res.y * (p2 - p0)</TT>
template <typename Point>
Point triCoords(const Point &p, const Point &p0, const Point &p1,
                const Point &p2) {
  Point v01(p1.x - p0.x, p1.y - p0.y);
  Point v02(p2.x - p0.x, p2.y - p0.y);
  Point p_p0(p.x - p0.x, p.y - p0.y);

  typename point_traits<Point>::value_type det = v01.x * v02.y - v02.x * v01.y;

  return Point((v02.y * p_p0.x - v02.x * p_p0.y) / det,
               (v01.x * p_p0.y - v01.y * p_p0.x) / det);
}

//-------------------------------------------------------------------------------------------

//! Returns the signed distance of point \p p from line <TT>a->d</TT>, where
//! positive means <I>at its left</I>.
template <typename Point>
inline typename point_traits<Point>::value_type lineSignedDist(const Point &p,
                                                               const Point &a,
                                                               const Point &d) {
  return (p.y - a.y) * d.x - (p.x - a.x) * d.y;
}

//-------------------------------------------------------------------------------------------

//! Returns the distance of point \p p from line <TT>a->d</TT>
template <typename Point>
inline typename point_traits<Point>::value_type lineDist(const Point &p,
                                                         const Point &a,
                                                         const Point &d) {
  return std::abs(lineSignedDist(p, a, d));
}

//-------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------

template <typename Point>
typename point_traits<Point>::value_type segDist(const Point &a, const Point &b,
                                                 const Point &p) {
  Point v(b - a), w;
  v = v / norm(v);

  if ((w = p - a) * v < 0) return norm(w);
  if ((w = p - b) * (-v) < 0) return norm(w);

  v = Point(-v.y, v.x);
  return std::abs((p - a) * v);
}

//-------------------------------------------------------------------------------------------

template <typename Point>
inline typename point_traits<Point>::value_type rad(const Point &p) {
  return atan2(p.y, p.x);
}

//-------------------------------------------------------------------------------------------

/*!
  Computes the angle, in radians, between \a unnormalized directions \p v1 and
  \p v2.
  \return The angle between the specified directions, in the range
  <TT>[-consts::pi, consts::pi]</TT>.
*/
template <typename Point>
inline typename point_traits<Point>::value_type angle(const Point &v1,
                                                      const Point &v2) {
  return numeric_ops::mod<typename point_traits<Point>::value_type>(
      rad(v2) - rad(v1), -M_PI, M_PI);
}

//-------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------

/*!
  Calculates scalars \p s and \p t:  <TT>a + s * v == c + t * w</TT>
  \return Whether the calculation succeeded with the specified tolerance
  parameter.

  \note In case the absolute value of the system's determinant is less or equal
  to the
        passed tolerance, the values numeric_ops::NaN() are returned for \p s
  and \p t.
*/
template <typename Point>
bool intersectionCoords(const Point &a, const Point &v, const Point &c,
                        const Point &w,
                        typename point_traits<Point>::value_type &s,
                        typename point_traits<Point>::value_type &t,
                        typename point_traits<Point>::value_type detTol = 0) {
  typedef typename point_traits<Point>::value_type value_type;

  value_type det = v.y * w.x - v.x * w.y;
  if (std::abs(det) < detTol) {
    s = t = numeric_ops::NaN<value_type>();
    return false;
  }

  value_type c_aX = c.x - a.x, c_aY = c.y - a.y;

  s = (w.x * c_aY - w.y * c_aX) / det;
  t = (v.x * c_aY - v.y * c_aX) / det;

  return true;
}

//-------------------------------------------------------------------------------------------

/*!
  Calculates scalars \p s and \p t:  <TT>a + s * (b-a) == c + t * (d-c)</TT>
  \return Whether the calculation succeeded with the specified tolerance
  parameter.

  \note In case the absolute value of the system's determinant is less or equal
  to the
        passed tolerance, the values numeric_ops::NaN() are returned for \p s
  and \p t.
*/
template <typename Point>
inline bool intersectionSegCoords(
    const Point &a, const Point &b, const Point &c, const Point &d,
    typename point_traits<Point>::value_type &s,
    typename point_traits<Point>::value_type &t,
    typename point_traits<Point>::value_type detTol = 0) {
  return intersectionCoords(a, b - a, c, d - c, s, t, detTol);
}

//-------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------

/*!
  \brief  Calculates the best fit line passing through \p p whose \p n samples
  have the specified
          coordinate sums

  \note   Returned unnormalized direction \p v may be NaP() in case no
  preferential direction could
          be chosen
*/
template <typename Point>
bool bestFit(const Point &p, Point &v,
             typename point_traits<Point>::value_type sums_x,
             typename point_traits<Point>::value_type sums_y,
             typename point_traits<Point>::value_type sums2_x,
             typename point_traits<Point>::value_type sums2_y,
             typename point_traits<Point>::value_type sums_xy,
             typename point_traits<Point>::value_type n) {
  typedef typename point_traits<Point>::value_type value_type;

  sums_x /= n;
  sums_y /= n;
  sums2_x /= n;
  sums2_y /= n;
  sums_xy /= n;

  value_type a = sums2_x - 2.0 * p.x * sums_x + sq(p.x);  // Always >= 0
  value_type b = sums_xy - p.x * sums_y - p.y * sums_x + p.x * p.y;
  value_type c = sums2_y - 2.0 * p.y * sums_y + sq(p.y);  // Always >= 0

  // The best-fit direction is an eigenvector of matrix [a, b; b, c]'s highest
  // eigenvalue.

  double trace = 0.5 * (a + c);
  double det   = a * c - b * b;

  double traceSq = trace * trace;
  if (traceSq < det) {
    v = NaP<Point>();
    return false;
  }

  double eigen = trace + sqrt(traceSq - det);  // plus takes the greater eigen
  a -= eigen, c -= eigen;

  // Take the most 'normed' obvious eigenvector
  if (std::abs(a) > std::abs(c))
    v.x = b, v.y = -a;
  else
    v.x = -c, v.y = b;

  return true;
}

//-------------------------------------------------------------------------------------------

/*!
  Calculates the best fit line whose n samples have the specified coordinate
  sums.

  \note  Returned point \p p is the samples mean, while (not normalized)
  direction \p v
         may be NaP in case no preferential direction could be chosen.
*/
template <typename Point>
bool bestFit(Point &p, Point &v,
             typename point_traits<Point>::value_type sums_x,
             typename point_traits<Point>::value_type sums_y,
             typename point_traits<Point>::value_type sums2_x,
             typename point_traits<Point>::value_type sums2_y,
             typename point_traits<Point>::value_type sums_xy,
             typename point_traits<Point>::value_type n) {
  p.x = sums_x / n, p.y = sums_y / n;
  return bestFit((const Point &)p, v, sums_x, sums_y, sums2_x, sums2_y, sums_xy,
                 n);
}

//-------------------------------------------------------------------------------------------

/*!
  Calculates the best fit line of the specified points.
  This is a mostly overloaded version of bestFit without explicit sums.
*/
template <typename Point_ref, typename Point, typename point_iterator>
bool bestFit(Point_ref p, Point &d, point_iterator begin, point_iterator end) {
  typedef typename point_traits<Point>::value_type value_type;

  value_type sums_x, sums_y, sums2_x, sums2_y, sums_xy;
  sums_x = sums_y = sums2_x = sums2_y = sums_xy = 0;

  size_t n = 0;

  point_iterator it;
  for (it = begin; it != end; ++it, ++n) {
    const Point &p = *it;

    sums_x += p.x, sums_y += p.y;
    sums2_x += p.x * p.x, sums2_y += p.y * p.y;
    sums_xy += p.x * p.y;
  }

  return bestFit(p, d, sums_x, sums_y, sums2_x, sums2_y, sums_xy, n);
}
}
}  // namespace tcg::point_ops

#endif  // TCG_POINT_OPS_H
