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

#include <gtk/gtk.h>

#include "gsk/gskcurveprivate.h"

static gboolean
measure_segment (const graphene_point_t *from,
                 const graphene_point_t *to,
                 float                   from_t,
                 float                   to_t,
                 gpointer                data)
{
  float *length = data;

  *length += graphene_point_distance (from, to, NULL, NULL);

  return TRUE;
}

static float
measure_length (const GskCurve *curve)
{
  float result = 0;

  gsk_curve_decompose (curve, 0.5, measure_segment, &result);

  return result;
}

/* This is a pretty nasty conic that makes it obvious that split()
 * does not respect the progress values, so split() twice  with
 * scaled factor won't work.
 */
static void
test_conic_segment (void)
{
  GskCurve c, s, e, m;
  graphene_point_t pts[4] = {
    GRAPHENE_POINT_INIT (-1856.131591796875, 46.217609405517578125),
    GRAPHENE_POINT_INIT (-1555.9866943359375, 966.0810546875),
    GRAPHENE_POINT_INIT (98.94945526123046875, 0),
    GRAPHENE_POINT_INIT (-1471.33154296875, 526.701171875)
  };
  float start = 0.02222645096480846405029296875;
  float end = 0.982032716274261474609375;

  gsk_curve_init (&c, gsk_pathop_encode (GSK_PATH_CONIC, pts));

  gsk_curve_split (&c, start, &s, NULL);
  gsk_curve_segment (&c, start, end, &m);
  gsk_curve_split (&c, end, NULL, &e);

  g_assert_cmpfloat_with_epsilon (measure_length (&c), measure_length (&s) + measure_length (&m) + measure_length (&e), 0.03125);
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/curve/special/conic-segment", test_conic_segment);

  return g_test_run ();
}
