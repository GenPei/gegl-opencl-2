/* This file is an image processing operation for GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright 2006 Øyvind Kolås <pippin@gimp.org>
 */


#include "config.h"
#include <glib/gi18n-lib.h>


#ifdef GEGL_CHANT_PROPERTIES

gegl_chant_path   (path,   _("Vector"),
                             _("A GeglVector representing the path of the stroke"))
gegl_chant_color  (color,    _("Color"),      "rgba(0.1,0.2,0.3,0.1)",
                             _("Color of paint to use"))
gegl_chant_double (linewidth,_("Linewidth"),  0.0, 100.0, 3.0,
                             _("width of stroke"))
gegl_chant_double (hardness, _("Hardness"),   0.0, 1.0, 0.7,
                             _("hardness of brush, 0.0 for soft brush 1.0 for hard brush."))

#else

#define GEGL_CHANT_TYPE_SOURCE
#define GEGL_CHANT_C_FILE "stroke.c"

#include "gegl-plugin.h"

/* the path api isn't public yet */
#include "property-types/gegl-path.h"
static void path_changed (GeglPath *path,
                          const GeglRectangle *roi,
                          gpointer userdata);

#include "gegl-chant.h"
#include <cairo/cairo.h>


static void path_changed (GeglPath *path,
                          const GeglRectangle *roi,
                          gpointer userdata)
{
  GeglRectangle rect = *roi;
  GeglChantO    *o   = GEGL_CHANT_PROPERTIES (userdata);
  /* invalidate the incoming rectangle */

  rect.x -= o->linewidth/2;
  rect.y -= o->linewidth/2;
  rect.width += o->linewidth;
  rect.height += o->linewidth;

  gegl_operation_invalidate (userdata, &rect, FALSE);
};

static void
prepare (GeglOperation *operation)
{
  gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  GeglChantO    *o       = GEGL_CHANT_PROPERTIES (operation);
  GeglRectangle  defined = { 0, 0, 512, 512 };
  gdouble        x0, x1, y0, y1;

  gegl_path_get_bounds (o->path, &x0, &x1, &y0, &y1);
  defined.x      = x0 - o->linewidth;
  defined.y      = y0 - o->linewidth;
  defined.width  = x1 - x0 + o->linewidth * 2;
  defined.height = y1 - y0 + o->linewidth * 2;

  return defined;
}

static GeglRectangle
get_cached_region (GeglOperation *operation)
{
  return get_bounding_box (operation);
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *output,
         const GeglRectangle *result)
{
  GeglChantO *o = GEGL_CHANT_PROPERTIES (operation);
  GeglRectangle box = get_bounding_box (operation);

  gegl_buffer_clear (output, &box);
  g_object_set_data (G_OBJECT (operation), "path-radius", GINT_TO_POINTER((gint)(o->linewidth+1)/2));
  gegl_path_stroke (output, o->path, o->color, o->linewidth, o->hardness);

  return  TRUE;
}


static void foreach_cairo (const GeglPathItem *knot,
                           gpointer              cr)
{
  switch (knot->type)
    {
      case 'M':
        cairo_move_to (cr, knot->point[0].x, knot->point[0].y);
        break;
      case 'L':
        cairo_line_to (cr, knot->point[0].x, knot->point[0].y);
        break;
      case 'C':
        cairo_curve_to (cr, knot->point[0].x, knot->point[0].y,
                            knot->point[1].x, knot->point[1].y,
                            knot->point[2].x, knot->point[2].y);
        break;
      case 'z':
        cairo_close_path (cr);
        break;
      default:
        g_print ("%s uh?:%c\n", G_STRLOC, knot->type);
    }
}

static void gegl_path_cairo_play (GeglPath *path,
                                    cairo_t *cr)
{
  gegl_path_foreach_flat (path, foreach_cairo, cr);
}

static GeglNode *detect (GeglOperation *operation,
                         gint           x,
                         gint           y)
{
  GeglChantO *o = GEGL_CHANT_PROPERTIES (operation);
  cairo_t *cr;
  cairo_surface_t *surface;
  gchar *data = "     ";
  gboolean result;

  surface = cairo_image_surface_create_for_data ((guchar*)data,
                                                 CAIRO_FORMAT_ARGB32,
                                                 1,1,4);
  cr = cairo_create (surface);
  gegl_path_cairo_play (o->path, cr);
  cairo_set_line_width (cr, o->linewidth);
  result = cairo_in_stroke (cr, x, y);
  cairo_destroy (cr);

  if (result)
    return operation->node;

  return NULL;
}

static void
gegl_chant_class_init (GeglChantClass *klass)
{
  GeglOperationClass       *operation_class;
  GeglOperationSourceClass *source_class;

  operation_class = GEGL_OPERATION_CLASS (klass);
  source_class    = GEGL_OPERATION_SOURCE_CLASS (klass);

  source_class->process = process;
  operation_class->get_bounding_box = get_bounding_box;
  operation_class->prepare = prepare;
  operation_class->detect = detect;

  operation_class->name        = "gegl:stroke";
  operation_class->categories  = "render";
  operation_class->description = _("Renders a brush stroke");
  operation_class->get_cached_region = (void*)get_cached_region;
}


#endif
