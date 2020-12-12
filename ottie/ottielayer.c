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

#include "ottielayerprivate.h"

#include <glib/gi18n-lib.h>
#include <json-glib/json-glib.h>

G_DEFINE_TYPE (OttieLayer, ottie_layer, G_TYPE_OBJECT)

static void
ottie_layer_default_update (OttieLayer *self,
                            GHashTable *compositions)
{
}

static void
ottie_layer_default_render (OttieLayer  *self,
                            OttieRender *render,
                            double       timestamp)
{
}

static void
ottie_layer_dispose (GObject *object)
{
  OttieLayer *self = OTTIE_LAYER (object);

  g_clear_object (&self->transform);

  G_OBJECT_CLASS (ottie_layer_parent_class)->dispose (object);
}

static void
ottie_layer_class_init (OttieLayerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  klass->update = ottie_layer_default_update;
  klass->render = ottie_layer_default_render;

  gobject_class->dispose = ottie_layer_dispose;
}

static void
ottie_layer_init (OttieLayer *self)
{
  self->stretch = 1;
  self->blend_mode = GSK_BLEND_MODE_DEFAULT;
}

void
ottie_layer_update (OttieLayer *self,
                    GHashTable *compositions)
{
  OTTIE_LAYER_GET_CLASS (self)->update (self, compositions);
}

void
ottie_layer_render (OttieLayer  *self,
                    OttieRender *render,
                    double       timestamp)
{
  OTTIE_LAYER_GET_CLASS (self)->render (self, render, timestamp);

  if (self->transform)
    ottie_shape_render (OTTIE_SHAPE (self->transform), render, timestamp);
}

