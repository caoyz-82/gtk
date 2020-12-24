/*
 * Copyright © 2020 Benjamin Otte
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 */

#include "config.h"

#include "gskcurveprivate.h"

#define MIN_PROGRESS (1/1024.f)

typedef struct _GskCurveClass GskCurveClass;

struct _GskCurveClass
{
  void                          (* init)                (GskCurve               *curve,
                                                         gskpathop               op);
  void                          (* eval)                (const GskCurve         *curve,
                                                         float                   progress,
                                                         graphene_point_t       *pos,
                                                         graphene_vec2_t        *tangent);
  void                          (* split)               (const GskCurve         *curve,
                                                         float                   progress,
                                                         GskCurve               *result1,
                                                         GskCurve               *result2);
  gboolean                      (* decompose)           (const GskCurve         *curve,
                                                         float                   tolerance,
                                                         GskCurveAddLineFunc     add_line_func,
                                                         gpointer                user_data);
  gskpathop                     (* pathop)              (const GskCurve         *curve);
  const graphene_point_t *      (* get_start_point)     (const GskCurve         *curve);
  const graphene_point_t *      (* get_end_point)       (const GskCurve         *curve);
};

/** LINE **/

static void
gsk_line_curve_init_from_points (GskLineCurve           *self,
                                 GskPathOperation        op,
                                 const graphene_point_t *start,
                                 const graphene_point_t *end)
{
  self->op = op;
  self->points[0] = *start;
  self->points[1] = *end;
}

static void
gsk_line_curve_init (GskCurve  *curve,
                     gskpathop  op)
{
  GskLineCurve *self = &curve->line;
  const graphene_point_t *pts = gsk_pathop_points (op);

  gsk_line_curve_init_from_points (self, gsk_pathop_op (op), &pts[0], &pts[1]);
}

static void
gsk_line_curve_eval (const GskCurve   *curve,
                     float             progress,
                     graphene_point_t *pos,
                     graphene_vec2_t  *tangent)
{
  const GskLineCurve *self = &curve->line;

  if (pos)
    graphene_point_interpolate (&self->points[0], &self->points[1], progress, pos);

  if (tangent)
    {
      graphene_vec2_init (tangent, self->points[1].x - self->points[0].x, self->points[1].y - self->points[0].y);
      graphene_vec2_normalize (tangent, tangent);
    }
}

static void
gsk_line_curve_split (const GskCurve   *curve,
                      float             progress,
                      GskCurve         *start,
                      GskCurve         *end)
{
  const GskLineCurve *self = &curve->line;
  graphene_point_t point;

  graphene_point_interpolate (&self->points[0], &self->points[1], progress, &point);

  if (start)
    gsk_line_curve_init_from_points (&start->line, GSK_PATH_LINE, &self->points[0], &point);
  if (end)
    gsk_line_curve_init_from_points (&end->line, GSK_PATH_LINE, &point, &self->points[1]);
}

static gboolean
gsk_line_curve_decompose (const GskCurve      *curve,
                          float                tolerance,
                          GskCurveAddLineFunc  add_line_func,
                          gpointer             user_data)
{
  const GskLineCurve *self = &curve->line;

  return add_line_func (&self->points[0], &self->points[1], 0.0f, 1.0f, user_data);
}

static gskpathop
gsk_line_curve_pathop (const GskCurve *curve)
{
  const GskLineCurve *self = &curve->line;

  return gsk_pathop_encode (self->op, self->points);
}

static const graphene_point_t *
gsk_line_curve_get_start_point (const GskCurve *curve)
{
  const GskLineCurve *self = &curve->line;

  return &self->points[0];
}

static const graphene_point_t *
gsk_line_curve_get_end_point (const GskCurve *curve)
{
  const GskLineCurve *self = &curve->line;

  return &self->points[1];
}

static const GskCurveClass GSK_LINE_CURVE_CLASS = {
  gsk_line_curve_init,
  gsk_line_curve_eval,
  gsk_line_curve_split,
  gsk_line_curve_decompose,
  gsk_line_curve_pathop,
  gsk_line_curve_get_start_point,
  gsk_line_curve_get_end_point,
};

/** CURVE **/

static void
gsk_curve_curve_init_from_points (GskCurveCurve          *self,
                                  const graphene_point_t  pts[4])
{
  self->op = GSK_PATH_CURVE;
  self->has_coefficients = FALSE;
  memcpy (self->points, pts, sizeof (graphene_point_t) * 4);
}
                                  
static void
gsk_curve_curve_init (GskCurve  *curve,
                      gskpathop  op)
{
  GskCurveCurve *self = &curve->curve;

  gsk_curve_curve_init_from_points (self, gsk_pathop_points (op));
}

static void
gsk_curve_curve_ensure_coefficients (const GskCurveCurve *curve)
{
  GskCurveCurve *self = (GskCurveCurve *) curve;
  const graphene_point_t *pts = &self->points[0];

  if (self->has_coefficients)
    return;

  self->coeffs[0] = GRAPHENE_POINT_INIT (pts[3].x - 3.0f * pts[2].x + 3.0f * pts[1].x - pts[0].x,
                                         pts[3].y - 3.0f * pts[2].y + 3.0f * pts[1].y - pts[0].y);
  self->coeffs[1] = GRAPHENE_POINT_INIT (3.0f * pts[2].x - 6.0f * pts[1].x + 3.0f * pts[0].x,
                                         3.0f * pts[2].y - 6.0f * pts[1].y + 3.0f * pts[0].y);
  self->coeffs[2] = GRAPHENE_POINT_INIT (3.0f * pts[1].x - 3.0f * pts[0].x,
                                         3.0f * pts[1].y - 3.0f * pts[0].y);
  self->coeffs[3] = pts[0];

  self->has_coefficients = TRUE;
}

static void
gsk_curve_curve_eval (const GskCurve   *curve,
                      float             progress,
                      graphene_point_t *pos,
                      graphene_vec2_t  *tangent)
{
  const GskCurveCurve *self = &curve->curve;
  const graphene_point_t *c = self->coeffs;

  gsk_curve_curve_ensure_coefficients (self);

  if (pos)
    *pos = GRAPHENE_POINT_INIT (((c[0].x * progress + c[1].x) * progress +c[2].x) * progress + c[3].x,
                                ((c[0].y * progress + c[1].y) * progress +c[2].y) * progress + c[3].y);
  if (tangent)
    {
      graphene_vec2_init (tangent,
                          (3.0f * c[0].x * progress + 2.0f * c[1].x) * progress + c[2].x,
                          (3.0f * c[0].y * progress + 2.0f * c[1].y) * progress + c[2].y);
      graphene_vec2_normalize (tangent, tangent);
    }
}

static void
gsk_curve_curve_split (const GskCurve   *curve,
                       float             progress,
                       GskCurve         *start,
                       GskCurve         *end)
{
  const GskCurveCurve *self = &curve->curve;
  const graphene_point_t *pts = self->points;
  graphene_point_t ab, bc, cd;
  graphene_point_t abbc, bccd;
  graphene_point_t final;

  graphene_point_interpolate (&pts[0], &pts[1], progress, &ab);
  graphene_point_interpolate (&pts[1], &pts[2], progress, &bc);
  graphene_point_interpolate (&pts[2], &pts[3], progress, &cd);
  graphene_point_interpolate (&ab, &bc, progress, &abbc);
  graphene_point_interpolate (&bc, &cd, progress, &bccd);
  graphene_point_interpolate (&abbc, &bccd, progress, &final);

  if (start)
    gsk_curve_curve_init_from_points (&start->curve, (graphene_point_t[4]) { pts[0], ab, abbc, final });
  if (end)
    gsk_curve_curve_init_from_points (&end->curve, (graphene_point_t[4]) { final, bccd, cd, pts[3] });
}

/* taken from Skia, including the very descriptive name */
static gboolean
gsk_curve_curve_too_curvy (const GskCurveCurve *self,
                           float                tolerance)
{
  const graphene_point_t *pts = self->points;
  graphene_point_t p;

  graphene_point_interpolate (&pts[0], &pts[3], 1.0f / 3, &p);
  if (ABS (p.x - pts[1].x) + ABS (p.y - pts[1].y)  > tolerance)
    return TRUE;

  graphene_point_interpolate (&pts[0], &pts[3], 2.0f / 3, &p);
  if (ABS (p.x - pts[2].x) + ABS (p.y - pts[2].y)  > tolerance)
    return TRUE;

  return FALSE;
}

static gboolean
gsk_curce_curve_decompose_step (const GskCurve      *curve,
                                float                start_progress,
                                float                end_progress,
                                float                tolerance,
                                GskCurveAddLineFunc  add_line_func,
                                gpointer             user_data)
{
  const GskCurveCurve *self = &curve->curve;
  GskCurve left, right;
  float mid_progress;

  if (!gsk_curve_curve_too_curvy (self, tolerance) || end_progress - start_progress <= MIN_PROGRESS)
    return add_line_func (&self->points[0], &self->points[3], start_progress, end_progress, user_data);

  gsk_curve_curve_split ((const GskCurve *) self, 0.5, &left, &right);
  mid_progress = (start_progress + end_progress) / 2;

  return gsk_curce_curve_decompose_step (&left, start_progress, mid_progress, tolerance, add_line_func, user_data)
      && gsk_curce_curve_decompose_step (&right, mid_progress, end_progress, tolerance, add_line_func, user_data);
}

static gboolean
gsk_curve_curve_decompose (const GskCurve      *curve,
                           float                tolerance,
                           GskCurveAddLineFunc  add_line_func,
                           gpointer             user_data)
{
  return gsk_curce_curve_decompose_step (curve, 0.0, 1.0, tolerance, add_line_func, user_data);
}

static gskpathop
gsk_curve_curve_pathop (const GskCurve *curve)
{
  const GskCurveCurve *self = &curve->curve;

  return gsk_pathop_encode (self->op, self->points);
}

static const graphene_point_t *
gsk_curve_curve_get_start_point (const GskCurve *curve)
{
  const GskCurveCurve *self = &curve->curve;

  return &self->points[0];
}

static const graphene_point_t *
gsk_curve_curve_get_end_point (const GskCurve *curve)
{
  const GskCurveCurve *self = &curve->curve;

  return &self->points[3];
}

static const GskCurveClass GSK_CURVE_CURVE_CLASS = {
  gsk_curve_curve_init,
  gsk_curve_curve_eval,
  gsk_curve_curve_split,
  gsk_curve_curve_decompose,
  gsk_curve_curve_pathop,
  gsk_curve_curve_get_start_point,
  gsk_curve_curve_get_end_point,
};

/** CONIC **/

static void
gsk_conic_curve_init_from_points (GskConicCurve          *self,
                                  const graphene_point_t  pts[4])
{
  self->op = GSK_PATH_CONIC;
  self->has_coefficients = FALSE;

  memcpy (self->points, pts, sizeof (graphene_point_t) * 4);
}
                                  
static void
gsk_conic_curve_init (GskCurve  *curve,
                      gskpathop  op)
{
  GskConicCurve *self = &curve->conic;

  gsk_conic_curve_init_from_points (self, gsk_pathop_points (op));
}

static inline float
gsk_conic_curve_get_weight (const GskConicCurve *self)
{
  return self->points[2].x;
}

static void
gsk_conic_curve_ensure_coefficents (const GskConicCurve *curve)
{
  GskConicCurve *self = (GskConicCurve *) curve;
  float w = gsk_conic_curve_get_weight (self);
  const graphene_point_t *pts = self->points;
  graphene_point_t pw = GRAPHENE_POINT_INIT (w * pts[1].x, w * pts[1].y);

  if (self->has_coefficients)
    return;

  self->num[2] = pts[0];
  self->num[1] = GRAPHENE_POINT_INIT (2 * (pw.x - pts[0].x),
                                      2 * (pw.y - pts[0].y));
  self->num[0] = GRAPHENE_POINT_INIT (pts[3].x - 2 * pw.x + pts[0].x,
                                      pts[3].y - 2 * pw.y + pts[0].y);

  self->denom[2] = GRAPHENE_POINT_INIT (1, 1);
  self->denom[1] = GRAPHENE_POINT_INIT (2 * (w - 1), 2 * (w - 1));
  self->denom[0] = GRAPHENE_POINT_INIT (-self->denom[1].x, -self->denom[1].y);

  self->has_coefficients = TRUE;
}

static inline void
gsk_curve_eval_quad (const graphene_point_t quad[3],
                     float                  progress,
                     graphene_point_t      *result)
{
  *result = GRAPHENE_POINT_INIT ((quad[0].x * progress + quad[1].x) * progress + quad[2].x,
                                 (quad[0].y * progress + quad[1].y) * progress + quad[2].y);
}

static inline void
gsk_conic_curve_eval_point (const GskConicCurve *self,
                            float                progress,
                            graphene_point_t    *point)
{
  graphene_point_t num, denom;

  gsk_curve_eval_quad (self->num, progress, &num);
  gsk_curve_eval_quad (self->denom, progress, &denom);

  *point = GRAPHENE_POINT_INIT (num.x / denom.x, num.y / denom.y);
}
                       
static void
gsk_conic_curve_eval (const GskCurve   *curve,
                      float             progress,
                      graphene_point_t *pos,
                      graphene_vec2_t  *tangent)
{
  const GskConicCurve *self = &curve->conic;

  gsk_conic_curve_ensure_coefficents (self);

  if (pos)
    gsk_conic_curve_eval_point (self, progress, pos);

  if (tangent)
    {
      graphene_point_t tmp;
      float w = gsk_conic_curve_get_weight (self);
      const graphene_point_t *pts = self->points;

      /* The tangent will be 0 in these corner cases, just
       * treat it like a line here. */
      if ((progress <= 0.f && graphene_point_equal (&pts[0], &pts[1])) ||
          (progress >= 1.f && graphene_point_equal (&pts[1], &pts[3])))
        {
          graphene_vec2_init (tangent, pts[3].x - pts[0].x, pts[3].y - pts[0].y);
          return;
        }

      gsk_curve_eval_quad ((graphene_point_t[3]) {
                             GRAPHENE_POINT_INIT ((w - 1) * (pts[3].x - pts[0].x),
                                                  (w - 1) * (pts[3].y - pts[0].y)),
                             GRAPHENE_POINT_INIT (pts[3].x - pts[0].x - 2 * w * (pts[1].x - pts[0].x),
                                                  pts[3].y - pts[0].y - 2 * w * (pts[1].y - pts[0].y)),
                             GRAPHENE_POINT_INIT (w * (pts[1].x - pts[0].x),
                                                  w * (pts[1].y - pts[0].y))
                           },
                           progress,
                           &tmp);
      graphene_vec2_init (tangent, tmp.x, tmp.y);
      graphene_vec2_normalize (tangent, tangent);
    }
}

static void
split_bezier3d_recurse (const graphene_point3d_t *p,
                        int                       l,
                        float                     t,
                        graphene_point3d_t       *left,
                        graphene_point3d_t       *right,
                        int                      *lpos,
                        int                      *rpos)
{
  if (l == 1)
    {
      left[*lpos] = p[0];
      right[*rpos] = p[0];
    }
  else
    {
      graphene_point3d_t *np;
      int i;

      np = g_alloca (sizeof (graphene_point3d_t) * (l - 1));
      for (i = 0; i < l - 1; i++)
        {
          if (i == 0)
            {
              left[*lpos] = p[i];
              (*lpos)++;
            }
          if (i + 1 == l - 1)
            {
              right[*rpos] = p[i + 1];
              (*rpos)--;
            }
          graphene_point3d_interpolate (&p[i], &p[i + 1], t, &np[i]);
        }
      split_bezier3d_recurse (np, l - 1, t, left, right, lpos, rpos);
    }
}

static void
split_bezier3d (const graphene_point3d_t *p,
                int                       l,
                float                     t,
                graphene_point3d_t       *left,
                graphene_point3d_t       *right)
{
  int lpos = 0;
  int rpos = l - 1;
  split_bezier3d_recurse (p, l, t, left, right, &lpos, &rpos);
}

static void
gsk_conic_curve_split (const GskCurve   *curve,
                       float             progress,
                       GskCurve         *start,
                       GskCurve         *end)
{
  const GskConicCurve *self = &curve->conic;
  graphene_point3d_t p[3];
  graphene_point3d_t l[3], r[3];
  graphene_point_t left[4], right[4];
  float w;

  /* do de Casteljau in homogeneous coordinates... */
  w = self->points[2].x;
  p[0] = GRAPHENE_POINT3D_INIT (self->points[0].x, self->points[0].y, 1);
  p[1] = GRAPHENE_POINT3D_INIT (self->points[1].x * w, self->points[1].y * w, w);
  p[2] = GRAPHENE_POINT3D_INIT (self->points[3].x, self->points[3].y, 1);

  split_bezier3d (p, 3, progress, l, r);

  /* then project the control points down */
  left[0] = GRAPHENE_POINT_INIT (l[0].x / l[0].z, l[0].y / l[0].z);
  left[1] = GRAPHENE_POINT_INIT (l[1].x / l[1].z, l[1].y / l[1].z);
  left[3] = GRAPHENE_POINT_INIT (l[2].x / l[2].z, l[2].y / l[2].z);

  right[0] = GRAPHENE_POINT_INIT (r[0].x / r[0].z, r[0].y / r[0].z);
  right[1] = GRAPHENE_POINT_INIT (r[1].x / r[1].z, r[1].y / r[1].z);
  right[3] = GRAPHENE_POINT_INIT (r[2].x / r[2].z, r[2].y / r[2].z);

  /* normalize the outer weights to be 1 by using
   * the fact that weights w_i and c*w_i are equivalent
   * for any nonzero constant c
   */
  for (int i = 0; i < 3; i++)
    {
      l[i].z /= l[0].z;
      r[i].z /= r[2].z;
    }

  /* normalize the inner weight to be 1 by using
   * the fact that w_0*w_2/w_1^2 is a constant for
   * all equivalent weights.
   */
  left[2] = GRAPHENE_POINT_INIT (l[1].z / sqrt (l[2].z), 0);
  right[2] = GRAPHENE_POINT_INIT (r[1].z / sqrt (r[0].z), 0);

  if (start)
    gsk_curve_init (start, gsk_pathop_encode (GSK_PATH_CONIC, left));
  if (end)
    gsk_curve_init (end, gsk_pathop_encode (GSK_PATH_CONIC, right));
}

/* taken from Skia, including the very descriptive name */
static gboolean
gsk_conic_curve_too_curvy (const graphene_point_t *start,
                           const graphene_point_t *mid,
                           const graphene_point_t *end,
                           float                  tolerance)
{
  return fabs ((start->x + end->x) * 0.5 - mid->x) > tolerance
      || fabs ((start->y + end->y) * 0.5 - mid->y) > tolerance;
}

static gboolean
gsk_conic_curve_decompose_subdivide (const GskConicCurve    *self,
                                     float                   tolerance,
                                     const graphene_point_t *start,
                                     float                   start_progress,
                                     const graphene_point_t *end,
                                     float                   end_progress,
                                     GskCurveAddLineFunc     add_line_func,
                                     gpointer                user_data)
{
  graphene_point_t mid;
  float mid_progress;

  mid_progress = (start_progress + end_progress) / 2;
  gsk_conic_curve_eval_point (self, mid_progress, &mid);

  if (end_progress - start_progress <= MIN_PROGRESS ||
      !gsk_conic_curve_too_curvy (start, &mid, end, tolerance))
    {
      return add_line_func (start, end, start_progress, end_progress, user_data);
    }

  return gsk_conic_curve_decompose_subdivide (self, tolerance,
                                              start, start_progress, &mid, mid_progress,
                                              add_line_func, user_data)
      && gsk_conic_curve_decompose_subdivide (self, tolerance, 
                                              &mid, mid_progress, end, end_progress,
                                              add_line_func, user_data);
}

static gboolean
gsk_conic_curve_decompose (const GskCurve      *curve,
                           float                tolerance,
                           GskCurveAddLineFunc  add_line_func,
                           gpointer             user_data)
{
  const GskConicCurve *self = &curve->conic;

  gsk_conic_curve_ensure_coefficents (self);

  return gsk_conic_curve_decompose_subdivide (self,
                                              tolerance,
                                              &self->points[0],
                                              0.0f,
                                              &self->points[3],
                                              1.0f,
                                              add_line_func,
                                              user_data);
}

static gskpathop
gsk_conic_curve_pathop (const GskCurve *curve)
{
  const GskConicCurve *self = &curve->conic;

  return gsk_pathop_encode (self->op, self->points);
}

static const graphene_point_t *
gsk_conic_curve_get_start_point (const GskCurve *curve)
{
  const GskConicCurve *self = &curve->conic;

  return &self->points[0];
}

static const graphene_point_t *
gsk_conic_curve_get_end_point (const GskCurve *curve)
{
  const GskConicCurve *self = &curve->conic;

  return &self->points[3];
}

static const GskCurveClass GSK_CONIC_CURVE_CLASS = {
  gsk_conic_curve_init,
  gsk_conic_curve_eval,
  gsk_conic_curve_split,
  gsk_conic_curve_decompose,
  gsk_conic_curve_pathop,
  gsk_conic_curve_get_start_point,
  gsk_conic_curve_get_end_point,
};

/** API **/

static const GskCurveClass *
get_class (GskPathOperation op)
{
  const GskCurveClass *klasses[] = {
    [GSK_PATH_CLOSE] = &GSK_LINE_CURVE_CLASS,
    [GSK_PATH_LINE] = &GSK_LINE_CURVE_CLASS,
    [GSK_PATH_CURVE] = &GSK_CURVE_CURVE_CLASS,
    [GSK_PATH_CONIC] = &GSK_CONIC_CURVE_CLASS,
  };

  g_assert (op < G_N_ELEMENTS (klasses) && klasses[op] != NULL);

  return klasses[op];
}

void
gsk_curve_init (GskCurve  *curve,
                gskpathop  op)
{
  get_class (gsk_pathop_op (op))->init (curve, op);
}

void
gsk_curve_eval (const GskCurve   *curve,
                float             progress,
                graphene_point_t *pos,
                graphene_vec2_t  *tangent)
{
  get_class (curve->op)->eval (curve, progress, pos, tangent);
}

void
gsk_curve_split (const GskCurve *curve,
                 float           progress,
                 GskCurve       *start,
                 GskCurve       *end)
{
  get_class (curve->op)->split (curve, progress, start, end);
}

gboolean
gsk_curve_decompose (const GskCurve      *curve,
                     float                tolerance,
                     GskCurveAddLineFunc  add_line_func,
                     gpointer             user_data)
{
  return get_class (curve->op)->decompose (curve, tolerance, add_line_func, user_data);
}

gskpathop
gsk_curve_pathop (const GskCurve *curve)
{
  return get_class (curve->op)->pathop (curve);
}

const graphene_point_t *
gsk_curve_get_start_point (const GskCurve *curve)
{
  return get_class (curve->op)->get_start_point (curve);
}

const graphene_point_t *
gsk_curve_get_end_point (const GskCurve *curve)
{
  return get_class (curve->op)->get_end_point (curve);
}

