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
 * Copyright 2005 Øyvind Kolås <pippin@gimp.org>,
 *           2007 Øyvind Kolås <oeyvindk@hig.no>
 */

#if GEGL_CHANT_PROPERTIES 
#define MAX_SAMPLES 20000 /* adapted to max level of radius */

gegl_chant_double (blur_radius, 0.0, 70.0, 4.0,
  "Radius of square pixel region, (width and height will be radius*2+1.")
gegl_chant_double (edge_preservation, 0.0, 70.0, 8.0, "Amount of edge preservation")

#else

#define GEGL_CHANT_FILTER
#define GEGL_CHANT_NAME            bilateral_filter
#define GEGL_CHANT_DESCRIPTION     "An edge preserving blur filter that can be used for noise reduction. It is a gaussian blur where the contribution of neighbourhood pixels are weighted by the color difference from the center pixel."
#define GEGL_CHANT_SELF            "bilateral-filter.c"
#define GEGL_CHANT_CATEGORIES      "misc"
#define GEGL_CHANT_CLASS_INIT
#include "gegl-chant.h"
#include <math.h>

static void
bilateral_filter (GeglBuffer *src,
                  GeglBuffer *dst,
                  gdouble     radius,
                  gdouble     preserve);

static GeglRectangle get_source_rect (GeglOperation *self,
                                      gpointer       context_id);

#include <stdio.h>

static gboolean
process (GeglOperation *operation,
         gpointer       context_id)
{
  GeglOperationFilter *filter;
  GeglChantOperation  *self;
  GeglBuffer          *input;
  GeglBuffer          *output;

  filter = GEGL_OPERATION_FILTER (operation);
  self   = GEGL_CHANT_OPERATION (operation);


  input = GEGL_BUFFER (gegl_operation_get_data (operation, context_id, "input"));
    {
      GeglRectangle   *result = gegl_operation_result_rect (operation, context_id);
      GeglRectangle    need   = *result;
      GeglBuffer      *temp_in;

      need.x-=self->blur_radius;
      need.y-=self->blur_radius;
      need.width +=self->blur_radius*2;
      need.height +=self->blur_radius*2;

      if (result->width == 0 ||
          result->height== 0 ||
          self->blur_radius < 1.0)
        {
          output = g_object_ref (input);
        }
      else
        {
          temp_in = g_object_new (GEGL_TYPE_BUFFER,
                                 "source", input,
                                 "x",      need.x,
                                 "y",      need.y,
                                 "width",  need.width,
                                 "height", need.height,
                                 NULL);

          output = g_object_new (GEGL_TYPE_BUFFER,
                                 "format", babl_format ("RGBA float"),
                                 "x",      need.x,
                                 "y",      need.y,
                                 "width",  need.width,
                                 "height", need.height,
                                 NULL);

          bilateral_filter (temp_in, output, self->blur_radius, self->edge_preservation);
          g_object_unref (temp_in);
        }

      {
        GeglBuffer *cropped = g_object_new (GEGL_TYPE_BUFFER,
                                              "source", output,
                                              "x",      result->x,
                                              "y",      result->y,
                                              "width",  result->width ,
                                              "height", result->height,
                                              NULL);
        gegl_operation_set_data (operation, context_id, "output", G_OBJECT (cropped));
        g_object_unref (output);
      }
    }
  return  TRUE;
}

static void
bilateral_filter (GeglBuffer *src,
                  GeglBuffer *dst,
                  gdouble     radius,
                  gdouble     preserve)
{
  gfloat gauss[((int)radius*2+1)][((int)radius*2+1)];
  gint x,y;
  gint offset;
  gfloat *src_buf;
  gfloat *dst_buf;

  src_buf = g_malloc0 (src->width * src->height * 4 * 4);
  dst_buf = g_malloc0 (dst->width * dst->height * 4 * 4);

  gegl_buffer_get (src, NULL, 1.0, babl_format ("RGBA float"), src_buf);

  offset = 0;

#define POW2(a) ((a)*(a))
  for (y=-radius;y<=radius;y++)
    for (x=-radius;x<=radius;x++)
      {
        gauss[x+(int)radius][y+(int)radius] = exp(- 0.5*(POW2(x)+POW2(y))/radius   );
      }

  for (y=0; y<dst->height; y++)
    for (x=0; x<dst->width; x++)
      {
        gint u,v;
        gfloat *center_pix = src_buf + (x+(y * src->width)) * 4;
        gfloat  accumulated[4]={0,0,0,0};
        gfloat  count=0.0;
        
        for (v=-radius;v<=radius;v++)
          for (u=-radius;u<=radius;u++)
            {
              if (x+u >= 0 && x+u < dst->width &&
                  y+v >= 0 && y+v < dst->height)
                {
                  gint c;
                  
                  gfloat *src_pix = src_buf + ((x+u)+((y+v) * src->width)) * 4;

                  gfloat diff_map   = exp (- (POW2(center_pix[0] - src_pix[0])+
                                              POW2(center_pix[1] - src_pix[1])+
                                              POW2(center_pix[2] - src_pix[2])) * preserve 
                                          );
                  gfloat gaussian_weight;
                  gfloat weight;
                 
                  gaussian_weight = gauss[u+(int)radius][v+(int)radius];

                  weight = diff_map * gaussian_weight;

                  for (c=0;c<4;c++)
                    {
                      accumulated[c] += src_pix[c] * weight;
                    }  
                  count += weight;
                }
            }

        for (u=0; u<4;u++)
          dst_buf[offset*4+u] = accumulated[u]/count;
        offset++;
      }
  gegl_buffer_set (dst, NULL, babl_format ("RGBA float"), dst_buf);
  g_free (src_buf);
  g_free (dst_buf);
}

#include <math.h>
static GeglRectangle
get_defined_region (GeglOperation *operation)
{
  GeglRectangle  result = {0,0,0,0};
  GeglRectangle *in_rect = gegl_operation_source_get_defined_region (operation,
                                                                     "input");

  GeglChantOperation *blur = GEGL_CHANT_OPERATION (operation);
  gint       radius = ceil(blur->blur_radius);

  if (!in_rect)
    return result;

  result = *in_rect;
  if (result.width  != 0 &&
      result.height  != 0)
    {
      result.x-=radius;
      result.y-=radius;
      result.width +=radius*2;
      result.height +=radius*2;
    }
  
  return result;
}

static GeglRectangle get_source_rect (GeglOperation *self,
                                      gpointer       context_id)
{
  GeglChantOperation *blur   = GEGL_CHANT_OPERATION (self);
  GeglRectangle            rect;
  gint                radius;
 
  radius = ceil(blur->blur_radius);

  rect  = *gegl_operation_get_requested_region (self, context_id);
  if (rect.width  != 0 &&
      rect.height  != 0)
    {
      rect.x -= radius;
      rect.y -= radius;
      rect.width  += radius*2;
      rect.height  += radius*2;
    }

  return rect;
}

static gboolean
calc_source_regions (GeglOperation *self,
                     gpointer       context_id)
{
  GeglRectangle need = get_source_rect (self, context_id);

  gegl_operation_set_source_region (self, context_id, "input", &need);

  return TRUE;
}

static GeglRectangle
get_affected_region (GeglOperation *self,
                     const gchar   *input_pad,
                     GeglRectangle  region)
{
  GeglChantOperation *blur   = GEGL_CHANT_OPERATION (self);
  gint                radius;
 
  radius = ceil(blur->blur_radius);

  region.x -= radius;
  region.y -= radius;
  region.width  += radius*2;
  region.height  += radius*2;
  return region;
}

static void class_init (GeglOperationClass *operation_class)
{
  operation_class->get_defined_region  = get_defined_region;
  operation_class->get_affected_region = get_affected_region;
  operation_class->calc_source_regions = calc_source_regions;
}

#endif