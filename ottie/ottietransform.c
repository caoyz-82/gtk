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

#include "ottietransformprivate.h"

#include "ottiedoublevalueprivate.h"
#include "ottieparserprivate.h"
#include "ottiepointvalueprivate.h"
#include "ottieshapeprivate.h"

#include <glib/gi18n-lib.h>
#include <gsk/gsk.h>

struct _OttieTransform
{
  OttieShape parent;

  OttieDoubleValue opacity;
  OttieDoubleValue rotation;
  OttiePointValue anchor;
  OttiePointValue position;
  OttiePointValue scale;
};

struct _OttieTransformClass
{
  OttieShapeClass parent_class;
};

G_DEFINE_TYPE (OttieTransform, ottie_transform, OTTIE_TYPE_SHAPE)

static void
ottie_transform_snapshot (OttieShape         *shape,
                          GtkSnapshot        *snapshot,
                          OttieShapeSnapshot *snapshot_data,
                          double              timestamp)
{
}

static void
ottie_transform_dispose (GObject *object)
{
  OttieTransform *self = OTTIE_TRANSFORM (object);

  ottie_double_value_clear (&self->opacity);
  ottie_double_value_clear (&self->rotation);
  ottie_point_value_clear (&self->anchor);
  ottie_point_value_clear (&self->position);
  ottie_point_value_clear (&self->scale);

  G_OBJECT_CLASS (ottie_transform_parent_class)->dispose (object);
}

static void
ottie_transform_finalize (GObject *object)
{
  //OttieTransform *self = OTTIE_TRANSFORM (object);

  G_OBJECT_CLASS (ottie_transform_parent_class)->finalize (object);
}

static void
ottie_transform_class_init (OttieTransformClass *klass)
{
  OttieShapeClass *shape_class = OTTIE_SHAPE_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  shape_class->snapshot = ottie_transform_snapshot;

  gobject_class->finalize = ottie_transform_finalize;
  gobject_class->dispose = ottie_transform_dispose;
}

static void
ottie_transform_init (OttieTransform *self)
{
  ottie_double_value_init (&self->opacity, 100);
  ottie_double_value_init (&self->rotation, 0);
  ottie_point_value_init (&self->anchor, &GRAPHENE_POINT3D_INIT (0, 0, 0));
  ottie_point_value_init (&self->position, &GRAPHENE_POINT3D_INIT (0, 0, 0));
  ottie_point_value_init (&self->scale, &GRAPHENE_POINT3D_INIT (100, 100, 100));
}

static gboolean
ottie_transform_value_parse_point (JsonReader *reader,
                                   gsize       offset,
                                   gpointer    data)
{
  return ottie_point_value_parse (reader, 0, offset, data);
}

static gboolean
ottie_transform_value_parse_scale (JsonReader *reader,
                                   gsize       offset,
                                   gpointer    data)
{
  return ottie_point_value_parse (reader, 100, offset, data);
}


OttieShape *
ottie_transform_parse (JsonReader *reader)
{
  OttieParserOption options[] = {
    { "nm", ottie_parser_option_string, G_STRUCT_OFFSET (OttieShape, name) },
    { "mn", ottie_parser_option_string, G_STRUCT_OFFSET (OttieShape, match_name) },
    { "hd", ottie_parser_option_boolean, G_STRUCT_OFFSET (OttieShape, hidden) },
    { "o", ottie_double_value_parse, G_STRUCT_OFFSET (OttieTransform, opacity) },
    { "r", ottie_double_value_parse, G_STRUCT_OFFSET (OttieTransform, rotation) },
    { "a", ottie_transform_value_parse_point, G_STRUCT_OFFSET (OttieTransform, anchor) },
    { "p", ottie_transform_value_parse_point, G_STRUCT_OFFSET (OttieTransform, position) },
    { "s", ottie_transform_value_parse_scale, G_STRUCT_OFFSET (OttieTransform, scale) },
    { "ty", ottie_parser_option_skip, 0 },
  };
  OttieTransform *self;

  self = g_object_new (OTTIE_TYPE_TRANSFORM, NULL);

  if (!ottie_parser_parse_object (reader, "transform", options, G_N_ELEMENTS (options), self))
    {
      g_object_unref (self);
      return NULL;
    }

  return OTTIE_SHAPE (self);
}

GskTransform *
ottie_transform_get_transform (OttieTransform *self,
                               double          timestamp)
{
  graphene_point3d_t anchor, position, scale;
  GskTransform *transform;

  ottie_point_value_get (&self->anchor, timestamp, &anchor);
  ottie_point_value_get (&self->position, timestamp, &position);
  ottie_point_value_get (&self->scale, timestamp, &scale);

  transform = NULL;
  transform = gsk_transform_translate_3d (transform, &position);
  transform = gsk_transform_rotate (transform, ottie_double_value_get (&self->rotation, timestamp));
  transform = gsk_transform_scale_3d (transform, scale.x / 100, scale.y / 100, scale.z / 100);
  graphene_point3d_scale (&anchor, -1, &anchor);
  transform = gsk_transform_translate_3d (transform, &anchor);

  return transform;
}

