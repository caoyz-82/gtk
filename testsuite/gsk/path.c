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

static GskPath *
create_random_path (void)
{
  GskPathBuilder *builder;
  guint i, n;

  builder = gsk_path_builder_new ();
  n = g_test_rand_int_range (0, 20);

  for (i = 0; i < n; i++)
    {
      switch (g_test_rand_int_range (0, 11))
      {
        case 0:
          gsk_path_builder_move_to (builder,
                                    g_test_rand_double_range (-1000, 1000),
                                    g_test_rand_double_range (-1000, 1000));
          break;

        case 1:
          gsk_path_builder_close (builder);
          break;

        case 2:
        case 3:
        case 4:
        case 5:
          gsk_path_builder_line_to (builder,
                                    g_test_rand_double_range (-1000, 1000),
                                    g_test_rand_double_range (-1000, 1000));
          break;

        case 6:
        case 7:
        case 8:
        case 9:
          gsk_path_builder_curve_to (builder,
                                     g_test_rand_double_range (-1000, 1000),
                                     g_test_rand_double_range (-1000, 1000),
                                     g_test_rand_double_range (-1000, 1000),
                                     g_test_rand_double_range (-1000, 1000),
                                     g_test_rand_double_range (-1000, 1000),
                                     g_test_rand_double_range (-1000, 1000));
          break;

        case 10:
          gsk_path_builder_add_rect (builder,
                                     &GRAPHENE_RECT_INIT (g_test_rand_double_range (-1000, 1000),
                                                          g_test_rand_double_range (-1000, 1000),
                                                          g_test_rand_double_range (-1000, 1000),
                                                          g_test_rand_double_range (-1000, 1000)));
          break;

        default:
          g_assert_not_reached();
          break;
      }
    }

  return gsk_path_builder_free_to_path (builder);
}

static void
test_create (void)
{
  GskPath *path1, *path2, *built;
  GskPathBuilder *builder;
  guint i;
  char *s;
  GString *str;

  for (i = 0; i < 1000; i++)
    {
      builder = gsk_path_builder_new ();
      path1 = create_random_path ();
      gsk_path_builder_add_path (builder, path1);
      path2 = create_random_path ();
      gsk_path_builder_add_path (builder, path2);
      built = gsk_path_builder_free_to_path (builder);

      str = g_string_new (NULL);
      gsk_path_print (path1, str);
      if (!gsk_path_is_empty (path1) && !gsk_path_is_empty (path2))
        g_string_append_c (str, ' ');
      gsk_path_print (path2, str);

      s = gsk_path_to_string (built);

      g_assert_cmpstr (s, ==, str->str);
      g_string_free (str, TRUE);
      g_free (s);

      gsk_path_unref (built);
      gsk_path_unref (path2);
      gsk_path_unref (path1);
    }
}

static void
test_segment_start (void)
{
  GskPath *path, *path1;
  GskPathMeasure *measure, *measure1;
  GskPathBuilder *builder;
  float epsilon, length;
  guint i;

  path = create_random_path ();
  measure = gsk_path_measure_new (path);
  length = gsk_path_measure_get_length (measure);
  epsilon = MAX (length / 1024, G_MINFLOAT);

  for (i = 0; i < 100; i++)
    {
      float seg_length = length * i / 100.0f;

      builder = gsk_path_builder_new ();
      gsk_path_measure_add_segment (measure, builder, 0, seg_length);
      path1 = gsk_path_builder_free_to_path (builder);
      measure1 = gsk_path_measure_new (path1);

      g_assert_cmpfloat_with_epsilon (seg_length, gsk_path_measure_get_length (measure1), epsilon);

      gsk_path_measure_unref (measure1);
      gsk_path_unref (path1);
    }

  gsk_path_measure_unref (measure);
  gsk_path_unref (path);
}

static void
test_segment_end (void)
{
  GskPath *path, *path1;
  GskPathMeasure *measure, *measure1;
  GskPathBuilder *builder;
  float epsilon, length;
  guint i;

  path = create_random_path ();
  measure = gsk_path_measure_new (path);
  length = gsk_path_measure_get_length (measure);
  epsilon = MAX (length / 1024, G_MINFLOAT);

  for (i = 0; i < 100; i++)
    {
      float seg_length = length * i / 100.0f;

      builder = gsk_path_builder_new ();
      gsk_path_measure_add_segment (measure, builder, length - seg_length, length);
      path1 = gsk_path_builder_free_to_path (builder);
      measure1 = gsk_path_measure_new (path1);

      g_assert_cmpfloat_with_epsilon (seg_length, gsk_path_measure_get_length (measure1), epsilon);

      gsk_path_measure_unref (measure1);
      gsk_path_unref (path1);
    }

  gsk_path_measure_unref (measure);
  gsk_path_unref (path);
}

static void
test_segment_chunk (void)
{
  GskPath *path, *path1;
  GskPathMeasure *measure, *measure1;
  GskPathBuilder *builder;
  float epsilon, length;
  guint i;

  path = create_random_path ();
  measure = gsk_path_measure_new (path);
  length = gsk_path_measure_get_length (measure);
  epsilon = MAX (length / 1024, G_MINFLOAT);

  for (i = 0; i <= 100; i++)
    {
      float seg_start = length * i / 200.0f;

      builder = gsk_path_builder_new ();
      gsk_path_measure_add_segment (measure, builder, seg_start, seg_start + length / 2);
      path1 = gsk_path_builder_free_to_path (builder);
      measure1 = gsk_path_measure_new (path1);

      g_assert_cmpfloat_with_epsilon (length / 2, gsk_path_measure_get_length (measure1), epsilon);

      gsk_path_measure_unref (measure1);
      gsk_path_unref (path1);
    }

  gsk_path_measure_unref (measure);
  gsk_path_unref (path);
}

static void
test_segment (void)
{
  GskPath *path, *path1, *path2, *path3;
  GskPathMeasure *measure, *measure1, *measure2, *measure3;
  GskPathBuilder *builder;
  guint i;
  float split1, split2, epsilon, length;

  for (i = 0; i < 1000; i++)
    {
      path = create_random_path ();
      measure = gsk_path_measure_new (path);
      length = gsk_path_measure_get_length (measure);
      /* chosen high enough to stop the testsuite from failing */
      epsilon = MAX (length / 256, 1.f / 1024);

      split1 = g_test_rand_double_range (0, length);
      split2 = g_test_rand_double_range (split1, length);

      builder = gsk_path_builder_new ();
      gsk_path_measure_add_segment (measure, builder, 0, split1);
      path1 = gsk_path_builder_free_to_path (builder);
      measure1 = gsk_path_measure_new (path1);

      builder = gsk_path_builder_new ();
      gsk_path_measure_add_segment (measure, builder, split1, split2);
      path2 = gsk_path_builder_free_to_path (builder);
      measure2 = gsk_path_measure_new (path2);

      builder = gsk_path_builder_new ();
      gsk_path_measure_add_segment (measure, builder, split2, length);
      path3 = gsk_path_builder_free_to_path (builder);
      measure3 = gsk_path_measure_new (path3);

      g_assert_cmpfloat_with_epsilon (split1, gsk_path_measure_get_length (measure1), epsilon);
      g_assert_cmpfloat_with_epsilon (split2 - split1, gsk_path_measure_get_length (measure2), epsilon);
      g_assert_cmpfloat_with_epsilon (length - split2, gsk_path_measure_get_length (measure3), epsilon);

      gsk_path_measure_unref (measure2);
      gsk_path_measure_unref (measure1);
      gsk_path_measure_unref (measure);
      gsk_path_unref (path2);
      gsk_path_unref (path1);
      gsk_path_unref (path);
    }
}

static void
test_closest_point (void)
{
  static const float tolerance = 0.5;
  GskPath *path, *path1, *path2;
  GskPathMeasure *measure, *measure1, *measure2;
  GskPathBuilder *builder;
  guint i, j;

  for (i = 0; i < 10; i++)
    {
      path1 = create_random_path ();
      measure1 = gsk_path_measure_new_with_tolerance (path1, tolerance);
      path2 = create_random_path ();
      measure2 = gsk_path_measure_new_with_tolerance (path2, tolerance);

      builder = gsk_path_builder_new ();
      gsk_path_builder_add_path (builder, path1);
      gsk_path_builder_add_path (builder, path2);
      path = gsk_path_builder_free_to_path (builder);
      measure = gsk_path_measure_new_with_tolerance (path, tolerance);

      for (j = 0; j < 100; j++)
        {
          graphene_point_t test = GRAPHENE_POINT_INIT (g_test_rand_double_range (-1000, 1000),
                                                       g_test_rand_double_range (-1000, 1000));
          graphene_point_t p1, p2, p;
          graphene_vec2_t t1, t2, t;
          float offset1, offset2, offset;
          float distance1, distance2, distance;
          gboolean found1, found2, found;

          found1 = gsk_path_measure_get_closest_point_full (measure1,
                                                            &test,
                                                            INFINITY,
                                                            &distance1,
                                                            &p1,
                                                            &offset1,
                                                            &t1);
          found2 = gsk_path_measure_get_closest_point_full (measure2,
                                                            &test,
                                                            INFINITY,
                                                            &distance2,
                                                            &p2,
                                                            &offset2,
                                                            &t2);
          found = gsk_path_measure_get_closest_point_full (measure,
                                                           &test,
                                                           INFINITY,
                                                           &distance,
                                                           &p,
                                                           &offset,
                                                           &t);

          if (found1 && (!found2 || distance1 < distance2 + tolerance))
            {
              g_assert_cmpfloat (distance1, ==, distance);
              g_assert_cmpfloat (p1.x, ==, p.x);
              g_assert_cmpfloat (p1.y, ==, p.y);
              g_assert_cmpfloat (offset1, ==, offset);
              g_assert_true (graphene_vec2_equal (&t1, &t));
            }
          else if (found2)
            {
              g_assert_cmpfloat (distance2, ==, distance);
              g_assert_cmpfloat (p2.x, ==, p.x);
              g_assert_cmpfloat (p2.y, ==, p.y);
              g_assert_cmpfloat_with_epsilon (offset2 + gsk_path_measure_get_length (measure1), offset, MAX (G_MINFLOAT, offset / 1024));
              g_assert_true (graphene_vec2_equal (&t2, &t));
            }
          else
            {
              g_assert (!found);
            }
        }

      gsk_path_measure_unref (measure2);
      gsk_path_measure_unref (measure1);
      gsk_path_measure_unref (measure);
      gsk_path_unref (path2);
      gsk_path_unref (path1);
      gsk_path_unref (path);
    }
}

int
main (int   argc,
      char *argv[])
{
  gtk_test_init (&argc, &argv, NULL);

  g_test_add_func ("/path/create", test_create);
  g_test_add_func ("/path/segment_start", test_segment_start);
  g_test_add_func ("/path/segment_end", test_segment_end);
  g_test_add_func ("/path/segment_chunk", test_segment_chunk);
  g_test_add_func ("/path/segment", test_segment);
  g_test_add_func ("/path/closest_point", test_closest_point);

  return g_test_run ();
}
