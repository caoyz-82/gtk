/*
 * Copyright © 2018 Benjamin Otte
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

#include "gtktim4sortmodel.h"

#include "gtkintl.h"
#include "gtkprivate.h"
#include "gtksorterprivate.h"
#include "gtktimsortprivate.h"

/**
 * SECTION:gtkstim4listmodel
 * @title: GtkTim4SortModel
 * @short_description: A list model that sorts its items
 * @see_also: #GListModel, #GtkSorter
 *
 * #GtkTim4SortModel is a list model that takes a list model and
 * sorts its elements according to a #GtkSorter.
 *
 * #GtkTim4SortModel is a generic model and because of that it
 * cannot take advantage of any external knowledge when sorting.
 * If you run into performance issues with #GtkTim4SortModel, it
 * is strongly recommended that you write your own sorting list
 * model.
 */

enum {
  PROP_0,
  PROP_INCREMENTAL,
  PROP_MODEL,
  PROP_SORTER,
  NUM_PROPERTIES
};

struct _GtkTim4SortModel
{
  GObject parent_instance;

  GListModel *model;
  GtkSorter *sorter;
  gboolean incremental;

  GtkTimSort sort; /* ongoing sort operation */
  guint sort_cb; /* 0 or current ongoing sort callback */

  guint n_items;
  GtkSortKeys *sort_keys;
  gsize key_size;
  gpointer keys;
  gpointer *positions;
};

struct _GtkTim4SortModelClass
{
  GObjectClass parent_class;
};

static GParamSpec *properties[NUM_PROPERTIES] = { NULL, };

static guint
pos_from_key (GtkTim4SortModel *self,
              gpointer          key)
{
  guint pos = ((char *) key - (char *) self->keys) / self->key_size;

  g_assert (pos < self->n_items);

  return pos;
}

static gpointer
key_from_pos (GtkTim4SortModel *self,
              guint             pos)
{
  return (char *) self->keys + self->key_size * pos;
}

static GType
gtk_tim4_sort_model_get_item_type (GListModel *list)
{
  return G_TYPE_OBJECT;
}

static guint
gtk_tim4_sort_model_get_n_items (GListModel *list)
{
  GtkTim4SortModel *self = GTK_TIM4_SORT_MODEL (list);

  if (self->model == NULL)
    return 0;

  return g_list_model_get_n_items (self->model);
}

static gpointer
gtk_tim4_sort_model_get_item (GListModel *list,
                              guint       position)
{
  GtkTim4SortModel *self = GTK_TIM4_SORT_MODEL (list);

  if (self->model == NULL)
    return NULL;

  if (self->positions)
    position = pos_from_key (self, self->positions[position]);

  return g_list_model_get_item (self->model, position);
}

static void
gtk_tim4_sort_model_model_init (GListModelInterface *iface)
{
  iface->get_item_type = gtk_tim4_sort_model_get_item_type;
  iface->get_n_items = gtk_tim4_sort_model_get_n_items;
  iface->get_item = gtk_tim4_sort_model_get_item;
}

G_DEFINE_TYPE_WITH_CODE (GtkTim4SortModel, gtk_tim4_sort_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, gtk_tim4_sort_model_model_init))

static gboolean
gtk_tim4_sort_model_is_sorting (GtkTim4SortModel *self)
{
  return self->sort_cb != 0;
}

static void
gtk_tim4_sort_model_stop_sorting (GtkTim4SortModel *self,
                                  gsize            *runs)
{
  if (self->sort_cb == 0)
    {
      if (runs)
        {
          runs[0] = self->n_items;
          runs[1] = 0;
        }
      return;
    }

  if (runs)
    gtk_tim_sort_get_runs (&self->sort, runs);
  gtk_tim_sort_finish (&self->sort);
  g_clear_handle_id (&self->sort_cb, g_source_remove);
}

static gboolean
gtk_tim4_sort_model_sort_step (GtkTim4SortModel *self,
                               gboolean          finish,
                               guint            *out_position,
                               guint            *out_n_items)
{
  gint64 end_time = g_get_monotonic_time ();
  gboolean result = FALSE;
  GtkTimSortRun change;
  gpointer *start_change, *end_change;

  /* 1 millisecond */
  end_time += 1000;
  end_change = self->positions;
  start_change = self->positions + self->n_items;

  while (gtk_tim_sort_step (&self->sort, &change))
    {
      result = TRUE;
      if (change.len)
        {
          start_change = MIN (start_change, (gpointer *) change.base);
          end_change = MAX (end_change, ((gpointer *) change.base) + change.len);
        }
     
      if (g_get_monotonic_time () >= end_time && !finish)
        break;
    }

  if (start_change < end_change)
    {
      *out_position = start_change - self->positions;
      *out_n_items = end_change - start_change;
    }
  else
    {
      *out_position = 0;
      *out_n_items = 0;
    }

  return result;
}

static gboolean
gtk_tim4_sort_model_sort_cb (gpointer data)
{
  GtkTim4SortModel *self = data;
  guint pos, n_items;

  if (gtk_tim4_sort_model_sort_step (self, FALSE, &pos, &n_items))
    {
      if (n_items)
        g_list_model_items_changed (G_LIST_MODEL (self), pos, n_items, n_items);
      return G_SOURCE_CONTINUE;
    }

  gtk_tim4_sort_model_stop_sorting (self, NULL);
  return G_SOURCE_REMOVE;
}

static int
sort_func (gconstpointer a,
           gconstpointer b,
           gpointer      data)
{
  gpointer *sa = (gpointer *) a;
  gpointer *sb = (gpointer *) b;
  int result;

  result = gtk_sort_keys_compare (data, *sa, *sb);
  if (result)
    return result;

  return *sa < *sb ? -1 : 1;
}

static gboolean
gtk_tim4_sort_model_start_sorting (GtkTim4SortModel *self,
                                   gsize            *runs)
{
  g_assert (self->sort_cb == 0);

  gtk_tim_sort_init (&self->sort,
                     self->positions,
                     self->n_items,
                     sizeof (gpointer),
                     sort_func,
                     self->sort_keys);
  if (runs)
    gtk_tim_sort_set_runs (&self->sort, runs);
  if (self->incremental)
    gtk_tim_sort_set_max_merge_size (&self->sort, 1024);

  if (!self->incremental)
    return FALSE;

  self->sort_cb = g_idle_add (gtk_tim4_sort_model_sort_cb, self);
  return TRUE;
}

static void
gtk_tim4_sort_model_finish_sorting (GtkTim4SortModel *self,
                                    guint            *pos,
                                    guint            *n_items)
{
  gtk_tim_sort_set_max_merge_size (&self->sort, 0);

  gtk_tim4_sort_model_sort_step (self, TRUE, pos, n_items);
  gtk_tim_sort_finish (&self->sort);

  gtk_tim4_sort_model_stop_sorting (self, NULL);
}

static void
gtk_tim4_sort_model_clear_keys (GtkTim4SortModel *self)
{

  if (gtk_sort_keys_needs_clear_key (self->sort_keys))
    {
      guint i;

      for (i = 0; i < self->n_items; i++)
        gtk_sort_keys_clear_key (self->sort_keys, key_from_pos (self, i));
    }

  g_clear_pointer (&self->keys, g_free);
  g_clear_pointer (&self->sort_keys, gtk_sort_keys_unref);
  self->key_size = 0;
}

static void
gtk_tim4_sort_model_clear_items (GtkTim4SortModel *self,
                                 guint            *pos,
                                 guint            *n_items)
{
  gtk_tim4_sort_model_stop_sorting (self, NULL);

  if (self->sort_keys == NULL)
    {
      if (pos || n_items)
        *pos = *n_items = 0;
      return;
    }

  if (pos || n_items)
    {
      guint start, end;

      for (start = 0; start < self->n_items; start++)
        {
          if (pos_from_key (self, self->positions[start]) != + start)
            break;
        }
      for (end = self->n_items; end > start; end--)
        {
          if (pos_from_key (self, self->positions[end - 1]) != end - 1)
            break;
        }

      *n_items = end - start;
      if (*n_items == 0)
        *pos = 0;
      else
        *pos = start;
    }

  g_clear_pointer (&self->positions, g_free);

  gtk_tim4_sort_model_clear_keys (self);
} 

static gboolean
gtk_tim4_sort_model_should_sort (GtkTim4SortModel *self)
{
  return self->sorter != NULL &&
         self->model != NULL &&
         gtk_sorter_get_order (self->sorter) != GTK_SORTER_ORDER_NONE;
}

static void
gtk_tim4_sort_model_create_keys (GtkTim4SortModel *self)
{
  guint i;

  g_assert (self->keys == NULL);
  g_assert (self->sort_keys == NULL);
  g_assert (self->key_size == 0);

  self->sort_keys = gtk_sorter_get_keys (self->sorter);
  self->key_size = self->sort_keys->key_size;
  self->keys = g_malloc_n (self->n_items, self->key_size);
  for (i = 0; i < self->n_items; i++)
    {
      gpointer item = g_list_model_get_item (self->model, i);
      gtk_sort_keys_init_key (self->sort_keys, item, key_from_pos (self, i));
      g_object_unref (item);
    }
}

static void
gtk_tim4_sort_model_create_items (GtkTim4SortModel *self)
{
  guint i;

  if (!gtk_tim4_sort_model_should_sort (self))
    return;

  g_assert (self->sort_keys == NULL);

  self->positions = g_new (gpointer, self->n_items);

  gtk_tim4_sort_model_create_keys (self);

  for (i = 0; i < self->n_items; i++)
    self->positions[i] = key_from_pos (self, i);
}

/* This realloc()s the arrays but does not set the added values. */
static void
gtk_tim4_sort_model_update_items (GtkTim4SortModel *self,
                                  gsize             runs[GTK_TIM_SORT_MAX_PENDING + 1],
                                  guint             position,
                                  guint             removed,
                                  guint             added,
                                  guint            *unmodified_start,
                                  guint            *unmodified_end)
{
  guint i, n_items, valid;
  guint start, end;
  gpointer *old_keys;

  n_items = self->n_items;
  start = n_items;
  end = n_items;
  
  /* first, move the keys over */
  old_keys = self->keys;
  for (i = position; i < position + removed; i++)
    {
      gtk_sort_keys_clear_key (self->sort_keys, key_from_pos (self, i));
    }

  if (removed > added)
    {
      memmove (key_from_pos (self, position + added),
               key_from_pos (self, position + removed),
               self->key_size * (n_items - position - removed));
      self->keys = g_realloc_n (self->keys, n_items - removed + added, self->key_size);
    }
  else if (removed < added)
    {
      self->keys = g_realloc_n (self->keys, n_items - removed + added, self->key_size);
      memmove (key_from_pos (self, position + added),
               key_from_pos (self, position + removed),
               self->key_size * (n_items - position - removed));
    }

  /* then, update the positions */
  valid = 0;
  for (i = 0; i < n_items; i++)
    {
      guint pos = ((char *) self->positions[i] - (char *) old_keys) / self->key_size;

      if (pos >= position + removed)
        pos = pos - removed + added;
      else if (pos >= position)
        { 
          start = MIN (start, valid);
          end = n_items - i - 1;
          continue;
        }

      self->positions[valid] = key_from_pos (self, pos);
      valid++;
    }
  self->positions = g_renew (gpointer, self->positions, n_items - removed + added);

  /* FIXME */
  runs[0] = 0;

  g_assert (valid == n_items - removed);

  self->n_items = n_items - removed + added;

  for (i = 0; i < added; i++)
    {
      gpointer item = g_list_model_get_item (self->model, position + i);
      gtk_sort_keys_init_key (self->sort_keys, item, key_from_pos (self, position + i));
      g_object_unref (item);
      self->positions[self->n_items - added + i] = key_from_pos (self, position + i);
    }

  *unmodified_start = start;
  *unmodified_end = end;
}

static void
gtk_tim4_sort_model_items_changed_cb (GListModel       *model,
                                      guint             position,
                                      guint             removed,
                                      guint             added,
                                      GtkTim4SortModel *self)
{
  gsize runs[GTK_TIM_SORT_MAX_PENDING + 1];
  guint i, n_items, start, end;
  gboolean was_sorting;

  if (removed == 0 && added == 0)
    return;

  if (!gtk_tim4_sort_model_should_sort (self))
    {
      self->n_items = self->n_items - removed + added;
      g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
      return;
    }

  was_sorting = gtk_tim4_sort_model_is_sorting (self);
  gtk_tim4_sort_model_stop_sorting (self, runs);

  gtk_tim4_sort_model_update_items (self, runs, position, removed, added, &start, &end);

  if (added > 0)
    {
      if (gtk_tim4_sort_model_start_sorting (self, runs))
        {
          end = 0;
        }
      else
        {
          guint pos, n;
          gtk_tim4_sort_model_finish_sorting (self, &pos, &n);
          if (n)
            start = MIN (start, pos);
          /* find first item that was added */
          for (i = 0; i < end; i++)
            {
              pos = pos_from_key (self, self->positions[self->n_items - i - 1]);
              if (pos >= position && pos < position + added)
                {
                  end = i;
                  break;
                }
            }
        }
    }
  else
    {
      if (was_sorting)
        gtk_tim4_sort_model_start_sorting (self, runs);
    }

  n_items = self->n_items - start - end;
  g_list_model_items_changed (G_LIST_MODEL (self), start, n_items - added + removed, n_items);
}

static void
gtk_tim4_sort_model_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  GtkTim4SortModel *self = GTK_TIM4_SORT_MODEL (object);

  switch (prop_id)
    {
    case PROP_INCREMENTAL:
      gtk_tim4_sort_model_set_incremental (self, g_value_get_boolean (value));
      break;

    case PROP_MODEL:
      gtk_tim4_sort_model_set_model (self, g_value_get_object (value));
      break;

    case PROP_SORTER:
      gtk_tim4_sort_model_set_sorter (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void 
gtk_tim4_sort_model_get_property (GObject     *object,
                                  guint        prop_id,
                                  GValue      *value,
                                  GParamSpec  *pspec)
{
  GtkTim4SortModel *self = GTK_TIM4_SORT_MODEL (object);

  switch (prop_id)
    {
    case PROP_INCREMENTAL:
      g_value_set_boolean (value, self->incremental);
      break;

    case PROP_MODEL:
      g_value_set_object (value, self->model);
      break;

    case PROP_SORTER:
      g_value_set_object (value, self->sorter);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_tim4_sort_model_sorter_changed_cb (GtkSorter        *sorter,
                                       int               change,
                                       GtkTim4SortModel *self)
{
  guint pos, n_items;

  if (gtk_tim4_sort_model_should_sort (self))
    {
      gtk_tim4_sort_model_stop_sorting (self, NULL);

      if (self->sort_keys == NULL)
        {
          gtk_tim4_sort_model_create_items (self);
        }
      else
        {
          GtkSortKeys *new_keys = gtk_sorter_get_keys (sorter);

          if (!gtk_sort_keys_is_compatible (new_keys, self->sort_keys))
            {
              char *old_keys = self->keys;
              gsize old_key_size = self->key_size;
              guint i;

              gtk_tim4_sort_model_clear_keys (self);
              gtk_tim4_sort_model_create_keys (self);

              for (i = 0; i < self->n_items; i++)
                self->positions[i] = key_from_pos (self, ((char *) self->positions[i] - old_keys) / old_key_size);

              gtk_sort_keys_unref (new_keys);
            }
          else
            {
              gtk_sort_keys_unref (self->sort_keys);
              self->sort_keys = new_keys;
            }
        }

      if (gtk_tim4_sort_model_start_sorting (self, NULL))
        pos = n_items = 0;
      else
        gtk_tim4_sort_model_finish_sorting (self, &pos, &n_items);
    }
  else
    {
      gtk_tim4_sort_model_clear_items (self, &pos, &n_items);
    }

  if (n_items > 0)
    g_list_model_items_changed (G_LIST_MODEL (self), pos, n_items, n_items);
}

static void
gtk_tim4_sort_model_clear_model (GtkTim4SortModel *self)
{
  if (self->model == NULL)
    return;

  g_signal_handlers_disconnect_by_func (self->model, gtk_tim4_sort_model_items_changed_cb, self);
  g_clear_object (&self->model);
  gtk_tim4_sort_model_clear_items (self, NULL, NULL);
  self->n_items = 0;
}

static void
gtk_tim4_sort_model_clear_sorter (GtkTim4SortModel *self)
{
  if (self->sorter == NULL)
    return;

  g_signal_handlers_disconnect_by_func (self->sorter, gtk_tim4_sort_model_sorter_changed_cb, self);
  g_clear_object (&self->sorter);
}

static void
gtk_tim4_sort_model_dispose (GObject *object)
{
  GtkTim4SortModel *self = GTK_TIM4_SORT_MODEL (object);

  gtk_tim4_sort_model_clear_model (self);
  gtk_tim4_sort_model_clear_sorter (self);

  G_OBJECT_CLASS (gtk_tim4_sort_model_parent_class)->dispose (object);
};

static void
gtk_tim4_sort_model_class_init (GtkTim4SortModelClass *class)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (class);

  gobject_class->set_property = gtk_tim4_sort_model_set_property;
  gobject_class->get_property = gtk_tim4_sort_model_get_property;
  gobject_class->dispose = gtk_tim4_sort_model_dispose;

  /**
   * GtkTim4SortModel:incremental:
   *
   * If the model should sort items incrementally
   */
  properties[PROP_INCREMENTAL] =
      g_param_spec_boolean ("incremental",
                            P_("Incremental"),
                            P_("Sort items incrementally"),
                            FALSE,
                            GTK_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtkTim4SortModel:model:
   *
   * The model being sorted
   */
  properties[PROP_MODEL] =
      g_param_spec_object ("model",
                           P_("Model"),
                           P_("The model being sorted"),
                           G_TYPE_LIST_MODEL,
                           GTK_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GtkTim4SortModel:sorter:
   *
   * The sorter for this model
   */
  properties[PROP_SORTER] =
      g_param_spec_object ("sorter",
                            P_("Sorter"),
                            P_("The sorter for this model"),
                            GTK_TYPE_SORTER,
                            GTK_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  g_object_class_install_properties (gobject_class, NUM_PROPERTIES, properties);
}

static void
gtk_tim4_sort_model_init (GtkTim4SortModel *self)
{
}

/**
 * gtk_tim4_sort_model_new:
 * @model: (allow-none): the model to sort
 * @sorter: (allow-none): the #GtkSorter to sort @model with
 *
 * Creates a new sort list model that uses the @sorter to sort @model.
 *
 * Returns: a new #GtkTim4SortModel
 **/
GtkTim4SortModel *
gtk_tim4_sort_model_new (GListModel *model,
                         GtkSorter  *sorter)
{
  GtkTim4SortModel *result;

  g_return_val_if_fail (model == NULL || G_IS_LIST_MODEL (model), NULL);
  g_return_val_if_fail (sorter == NULL || GTK_IS_SORTER (sorter), NULL);

  result = g_object_new (GTK_TYPE_TIM4_SORT_MODEL,
                         "model", model,
                         "sorter", sorter,
                         NULL);

  return result;
}

/**
 * gtk_tim4_sort_model_set_model:
 * @self: a #GtkTim4SortModel
 * @model: (allow-none): The model to be sorted
 *
 * Sets the model to be sorted. The @model's item type must conform to
 * the item type of @self.
 **/
void
gtk_tim4_sort_model_set_model (GtkTim4SortModel *self,
                               GListModel       *model)
{
  guint removed;

  g_return_if_fail (GTK_IS_TIM4_SORT_MODEL (self));
  g_return_if_fail (model == NULL || G_IS_LIST_MODEL (model));

  if (self->model == model)
    return;

  removed = g_list_model_get_n_items (G_LIST_MODEL (self));
  gtk_tim4_sort_model_clear_model (self);

  if (model)
    {
      guint ignore1, ignore2;

      self->model = g_object_ref (model);
      self->n_items = g_list_model_get_n_items (model);
      g_signal_connect (model, "items-changed", G_CALLBACK (gtk_tim4_sort_model_items_changed_cb), self);

      if (gtk_tim4_sort_model_should_sort (self))
        {
          gtk_tim4_sort_model_create_items (self);
          if (!gtk_tim4_sort_model_start_sorting (self, NULL))
            gtk_tim4_sort_model_finish_sorting (self, &ignore1, &ignore2);
        }
    }
  
  if (removed > 0 || self->n_items > 0)
    g_list_model_items_changed (G_LIST_MODEL (self), 0, removed, self->n_items);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODEL]);
}

/**
 * gtk_tim4_sort_model_get_model:
 * @self: a #GtkTim4SortModel
 *
 * Gets the model currently sorted or %NULL if none.
 *
 * Returns: (nullable) (transfer none): The model that gets sorted
 **/
GListModel *
gtk_tim4_sort_model_get_model (GtkTim4SortModel *self)
{
  g_return_val_if_fail (GTK_IS_TIM4_SORT_MODEL (self), NULL);

  return self->model;
}

/**
 * gtk_tim4_sort_model_set_sorter:
 * @self: a #GtkTim4SortModel
 * @sorter: (allow-none): the #GtkSorter to sort @model with
 *
 * Sets a new sorter on @self.
 */
void
gtk_tim4_sort_model_set_sorter (GtkTim4SortModel *self,
                                GtkSorter        *sorter)
{
  g_return_if_fail (GTK_IS_TIM4_SORT_MODEL (self));
  g_return_if_fail (sorter == NULL || GTK_IS_SORTER (sorter));

  gtk_tim4_sort_model_clear_sorter (self);

  if (sorter)
    {
      self->sorter = g_object_ref (sorter);
      g_signal_connect (sorter, "changed", G_CALLBACK (gtk_tim4_sort_model_sorter_changed_cb), self);
    }

  gtk_tim4_sort_model_sorter_changed_cb (sorter, GTK_SORTER_CHANGE_DIFFERENT, self);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SORTER]);
}

/**
 * gtk_tim4_sort_model_get_sorter:
 * @self: a #GtkTim4SortModel
 *
 * Gets the sorter that is used to sort @self.
 *
 * Returns: (nullable) (transfer none): the sorter of #self
 */
GtkSorter *
gtk_tim4_sort_model_get_sorter (GtkTim4SortModel *self)
{
  g_return_val_if_fail (GTK_IS_TIM4_SORT_MODEL (self), NULL);

  return self->sorter;
}

/**
 * gtk_tim4_sort_model_set_incremental:
 * @self: a #GtkTim4SortModel
 * @incremental: %TRUE to sort incrementally
 *
 * Sets the sort model to do an incremental sort.
 *
 * When incremental sorting is enabled, the sortlistmodel will not do
 * a complete sort immediately, but will instead queue an idle handler that
 * incrementally sorts the items towards their correct position. This of
 * course means that items do not instantly appear in the right place. It
 * also means that the total sorting time is a lot slower.
 *
 * When your filter blocks the UI while sorting, you might consider
 * turning this on. Depending on your model and sorters, this may become
 * interesting around 10,000 to 100,000 items.
 *
 * By default, incremental sortinging is disabled.
 */
void
gtk_tim4_sort_model_set_incremental (GtkTim4SortModel *self,
                                     gboolean          incremental)
{
  g_return_if_fail (GTK_IS_TIM4_SORT_MODEL (self));

  if (self->incremental == incremental)
    return;

  self->incremental = incremental;

  if (!incremental && gtk_tim4_sort_model_is_sorting (self))
    {
      guint pos, n_items;

      gtk_tim4_sort_model_finish_sorting (self, &pos, &n_items);
      if (n_items)
        g_list_model_items_changed (G_LIST_MODEL (self), pos, n_items, n_items);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INCREMENTAL]);
}

/**
 * gtk_tim4_sort_model_get_incremental:
 * @self: a #GtkModel
 *
 * Returns whether incremental sorting was enabled via
 * gtk_sort_list_model_set_incremental().
 *
 * Returns: %TRUE if incremental sorting is enabled
 */
gboolean
gtk_tim4_sort_model_get_incremental (GtkTim4SortModel *self)
{
  g_return_val_if_fail (GTK_IS_TIM4_SORT_MODEL (self), FALSE);

  return self->incremental;
}