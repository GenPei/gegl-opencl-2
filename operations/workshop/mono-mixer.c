/* This file is an image processing operation for GEGL
 *
 * GEGL is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * GEGL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GEGL; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Copyright 2006 Mark Probst <mark.probst@gmail.com>
 */

#if GEGL_CHANT_PROPERTIES

 gegl_chant_double (red, -10.0, 10.0, 0.5, "Amount of red")
 gegl_chant_double (green, -10.0, 10.0, 0.25, "Amount of green")
 gegl_chant_double (blue, -10.0, 10.0, 0.25, "Amount of blue")

#else

#define GEGL_CHANT_FILTER

#define GEGL_CHANT_NAME         mono_mixer
#define GEGL_CHANT_SELF         "mono-mixer.c"

#define GEGL_CHANT_DESCRIPTION  "Monochrome channel mixer"

#define GEGL_CHANT_CATEGORIES   "color"

#include "gegl-chant.h"

static gboolean
process (GeglOperation *operation,
        gpointer       context_id)
{
 GeglChantOperation *self;
 GeglBuffer         *input;
 GeglBuffer         *output;
 gfloat             *in_buf;
 gfloat             *out_buf;
 GeglRectangle      *result_rect = gegl_operation_result_rect
(operation, context_id);
 gfloat              red, green, blue;

 self = GEGL_CHANT_OPERATION (operation);
 input = GEGL_BUFFER (gegl_operation_get_data (operation, context_id,
"input"));

 red = self->red;
 green = self->green;
 blue = self->blue;

 output = g_object_new (GEGL_TYPE_BUFFER,
                        "format", babl_format ("YA float"),
                        "x", result_rect->x,
                        "y", result_rect->y,
                        "width", result_rect->width,
                        "height", result_rect->height,
                        NULL);

 if ((result_rect->width > 0) && (result_rect->height > 0))
 {
     gint num_pixels = result_rect->width * result_rect->height;
     gint i;
     gfloat *in_pixel, *out_pixel;

     in_buf = g_malloc (4 * sizeof (gfloat) * num_pixels);
     out_buf = g_malloc (2 * sizeof(gfloat) * num_pixels);

     gegl_buffer_get (input, result_rect, 1.0, babl_format ("RGBA float"), in_buf);

     in_pixel = in_buf;
     out_pixel = out_buf;
     for (i = 0; i < num_pixels; ++i)
     {
         out_pixel[0] = in_pixel[0] * red + in_pixel[1] * green + in_pixel[2] * blue;
         out_pixel[1] = in_pixel[3];

         in_pixel += 4;
         out_pixel += 2;
     }

     gegl_buffer_set (output, result_rect, output->format, out_buf);

     g_free (in_buf);
     g_free (out_buf);
 }

 gegl_operation_set_data (operation, context_id, "output", G_OBJECT (output));

 return TRUE;
}

#endif