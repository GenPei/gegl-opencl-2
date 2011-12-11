/* This file is part of GEGL
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
 * Copyright 2006 Øyvind Kolås
 */


#include "config.h"

#include <glib-object.h>

#include "gegl.h"
#include "gegl-types-internal.h"
#include "gegl-operation-point-filter.h"
#include "graph/gegl-pad.h"
#include "graph/gegl-node.h"
#include "gegl-utils.h"
#include <string.h>

#include "gegl-buffer-private.h"
#include "gegl-tile-storage.h"

#include "opencl/gegl-cl.h"

static gboolean gegl_operation_point_filter_process
                              (GeglOperation       *operation,
                               GeglBuffer          *input,
                               GeglBuffer          *output,
                               const GeglRectangle *result);

static gboolean gegl_operation_point_filter_op_process
                              (GeglOperation       *operation,
                               GeglOperationContext *context,
                               const gchar          *output_pad,
                               const GeglRectangle  *roi);

G_DEFINE_TYPE (GeglOperationPointFilter, gegl_operation_point_filter, GEGL_TYPE_OPERATION_FILTER)

static void prepare (GeglOperation *operation)
{
  gegl_operation_set_format (operation, "input", babl_format ("RGBA float"));
  gegl_operation_set_format (operation, "output", babl_format ("RGBA float"));
}

static void
gegl_operation_point_filter_class_init (GeglOperationPointFilterClass *klass)
{
  GeglOperationClass *operation_class = GEGL_OPERATION_CLASS (klass);

  operation_class->process = gegl_operation_point_filter_op_process;
  operation_class->prepare = prepare;
  operation_class->no_cache = TRUE;

  klass->process = NULL;
  klass->cl_process = NULL;
  klass->cl_kernel_source     = NULL;
  klass->cl_kernel_parameters = NULL;
}

static void
gegl_operation_point_filter_init (GeglOperationPointFilter *self)
{
}

struct buf_tex
{
  GeglBuffer *buf;
  GeglRectangle *region;
  cl_mem *tex;
};

#define CL_ERROR {g_assert(0);}
//#define CL_ERROR {goto error;}

#include "opencl/gegl-cl-color-kernel.h"
#include "opencl/gegl-cl-color.h"

static cl_kernel
cl_generate_kernel (const gchar *kernel_source, const gchar *kernel_parameters,
                    const Babl *op_in_format, const Babl *buffer_in_format,
                    const Babl *op_out_format, const Babl *buffer_out_format,
                    gboolean *need_babl_in, gboolean *need_babl_out)
{
  static const gchar *kernel_template =
  "                                                       \n"
  " %s                                                    \n" /* header */
  "                                                       \n"
  "__kernel void kernel_point (read_only  __global float4 *in,  \n"
  "                            write_only __global float4 *out, \n"
  "                            %s                               \n" /* custom kernel parameters */
  "                           )                                 \n"
  "{                                                      \n"
  "  int gid = get_global_id(0);                          \n"
  "  float4 in_v = in[gid];                               \n"
  "  float4 out_v;                                        \n"
  "                                                       \n"
  "  %s                                                   \n" /* color conversion */
  "                                                       \n"
  "  %s                                                   \n" /* custom processing code */
  "                                                       \n"
  "  %s                                                   \n" /* color conversion */
  "                                                       \n"
  "  out[gid] = out_v;                                    \n"
  "}                                                      \n";

  static const gchar *kernel_name[] = {"kernel_point", NULL};
  gegl_cl_run_data *cl_data = NULL;

  GString *g_kernel_source_full = g_string_new ("");
  GString *g_compiler_options   = g_string_new ("");

  gint conv_in [2];
  gint conv_out[2];

  g_string_printf(g_kernel_source_full, kernel_template, kernel_color_header,
                                        kernel_parameters, kernel_color_conv_in,
                                        kernel_source, kernel_color_conv_out);

  *need_babl_in  = !gegl_cl_color_conv (buffer_in_format, op_in_format,      conv_in );
  *need_babl_out = !gegl_cl_color_conv (op_out_format,    buffer_out_format, conv_out);

  g_string_printf(g_compiler_options,
                  "-DCONV_IN_1=%d -DCONV_IN_2=%d -DCONV_OUT_1=%d -DCONV_OUT_2=%d",
                  conv_in[0], conv_in[1], conv_out[0], conv_out[1]);

  /* g_printf("[OpenCL] Kernel:\n%s\n", g_kernel_source_full->str); */

  cl_data = gegl_cl_compile_and_build (g_kernel_source_full->str, kernel_name, g_compiler_options->str);
  return cl_data->kernel[0];
}

static gboolean
gegl_operation_point_filter_cl_process_full (GeglOperation       *operation,
                                             GeglBuffer          *input,
                                             GeglBuffer          *output,
                                             const GeglRectangle *result)
{
  const Babl *in_format  = gegl_operation_get_format (operation, "input");
  const Babl *out_format = gegl_operation_get_format (operation, "output");

  GeglOperationPointFilterClass *point_filter_class = GEGL_OPERATION_POINT_FILTER_GET_CLASS (operation);

  int y, x, i;
  int errcode;

  gfloat** in_data  = NULL;
  gfloat** out_data = NULL;

  int ntex = 0;
  struct buf_tex input_tex;
  struct buf_tex output_tex;

  gboolean need_babl_in  = TRUE;
  gboolean need_babl_out = TRUE;

  /* generate kernel */
  cl_kernel kernel = cl_generate_kernel (point_filter_class->cl_kernel_source, point_filter_class->cl_kernel_parameters,
                                         gegl_buffer_get_format(input),  in_format,
                                         gegl_buffer_get_format(output), out_format,
                                         &need_babl_in, &need_babl_out);

  g_printf("[OpenCL] BABL formats: (%s,%s:%d) (%s,%s:%d)\n", babl_get_name(gegl_buffer_get_format(input)),  babl_get_name(in_format),
                                                             gegl_cl_color_supported (gegl_buffer_get_format(input), in_format),
                                                             babl_get_name(out_format), babl_get_name(gegl_buffer_get_format(output)),
                                                             gegl_cl_color_supported (out_format, gegl_buffer_get_format(output)));

  for (y=0; y < result->height; y += cl_state.max_image_height)
   for (x=0; x < result->width;  x += cl_state.max_image_width)
     ntex++;

  input_tex.region  = (GeglRectangle *) gegl_malloc(ntex * sizeof(GeglRectangle));
  output_tex.region = (GeglRectangle *) gegl_malloc(ntex * sizeof(GeglRectangle));
  input_tex.tex     = (cl_mem *)        gegl_malloc(ntex * sizeof(cl_mem));
  output_tex.tex    = (cl_mem *)        gegl_malloc(ntex * sizeof(cl_mem));

  if (input_tex.region == NULL || output_tex.region == NULL || input_tex.tex == NULL || output_tex.tex == NULL)
    CL_ERROR;

  in_data  = (gfloat**) gegl_malloc(ntex * sizeof(gfloat *));
  out_data = (gfloat**) gegl_malloc(ntex * sizeof(gfloat *));

  if (in_data == NULL || out_data == NULL) CL_ERROR;

  i = 0;
  for (y=0; y < result->height; y += cl_state.max_image_height)
    for (x=0; x < result->width;  x += cl_state.max_image_width)
      {
        const size_t region[3] = {MIN(cl_state.max_image_width,  result->width -x),
                                  MIN(cl_state.max_image_height, result->height-y),
                                  1};
        const size_t mem_size = region[0] * region[1]*sizeof(cl_float4);

        GeglRectangle r = {x, y, region[0], region[1]};
        input_tex.region[i] = output_tex.region[i] = r;

        input_tex.tex[i]  = gegl_clCreateBuffer (gegl_cl_get_context(),
                                                 CL_MEM_ALLOC_HOST_PTR | CL_MEM_READ_ONLY, mem_size,
                                                 NULL, &errcode);
        if (errcode != CL_SUCCESS) CL_ERROR;

        output_tex.tex[i] = gegl_clCreateBuffer (gegl_cl_get_context(),
                                                 CL_MEM_WRITE_ONLY, mem_size,
                                                 NULL, &errcode);
        if (errcode != CL_SUCCESS) CL_ERROR;

        i++;
      }

  for (i=0; i < ntex; i++)
    {
      const size_t region[3] = {input_tex.region[i].width, input_tex.region[i].height, 1};
      const size_t mem_size = region[0] * region[1]*sizeof(cl_float4);

      /* pre-pinned memory */
      in_data[i]  = gegl_clEnqueueMapBuffer(gegl_cl_get_command_queue(), input_tex.tex[i], CL_TRUE,
                                            CL_MAP_WRITE,
                                            0, mem_size,
                                            0, NULL, NULL, &errcode);
      if (errcode != CL_SUCCESS) CL_ERROR;

      /* un-tile */
      if (!need_babl_in)
        gegl_buffer_get (input, 1.0, &input_tex.region[i], gegl_buffer_get_format(input), in_data[i], GEGL_AUTO_ROWSTRIDE);
      else                                                 /* color conversion using BABL */
        gegl_buffer_get (input, 1.0, &input_tex.region[i], in_format, in_data[i], GEGL_AUTO_ROWSTRIDE);
    }

  /* CPU -> GPU */
  for (i=0; i < ntex; i++)
    {
      errcode = gegl_clEnqueueUnmapMemObject (gegl_cl_get_command_queue(), input_tex.tex[i], in_data[i],
                                              0, NULL, NULL);
      if (errcode != CL_SUCCESS) CL_ERROR;
    }

  errcode = gegl_clEnqueueBarrier(gegl_cl_get_command_queue());
  if (errcode != CL_SUCCESS) CL_ERROR;

  /* Process */
  for (i=0; i < ntex; i++)
    {
      const size_t region[3] = {input_tex.region[i].width, input_tex.region[i].height, 1};
      const size_t global_worksize = region[0] * region[1];

      errcode = point_filter_class->cl_process(operation, input_tex.tex[i], output_tex.tex[i], kernel, global_worksize, &input_tex.region[i]);
      if (errcode != CL_SUCCESS) CL_ERROR;
    }

  /* Wait Processing */
  errcode = gegl_clEnqueueBarrier(gegl_cl_get_command_queue());
  if (errcode != CL_SUCCESS) CL_ERROR;

  /* GPU -> CPU */
  for (i=0; i < ntex; i++)
    {
      const size_t region[3] = {output_tex.region[i].width, output_tex.region[i].height, 1};
      const size_t mem_size = region[0] * region[1] * sizeof(cl_float4);

      out_data[i]  = gegl_clEnqueueMapBuffer(gegl_cl_get_command_queue(), output_tex.tex[i], CL_FALSE,
                                             CL_MAP_READ,
                                             0, mem_size,
                                             0, NULL, NULL, &errcode);
      if (errcode != CL_SUCCESS) CL_ERROR;
    }

  /* Wait */
  errcode = gegl_clEnqueueBarrier(gegl_cl_get_command_queue());
  if (errcode != CL_SUCCESS) CL_ERROR;

  /* Run! */
  errcode = gegl_clFinish(gegl_cl_get_command_queue());
  if (errcode != CL_SUCCESS) CL_ERROR;

  for (i=0; i < ntex; i++)
    {
      /* tile-ize */
      if (need_babl_out)
        gegl_buffer_set (output, &output_tex.region[i], gegl_buffer_get_format(output), out_data[i], GEGL_AUTO_ROWSTRIDE);
      else                                                 /* color conversion using BABL */
        gegl_buffer_set (output, &output_tex.region[i], out_format, out_data[i], GEGL_AUTO_ROWSTRIDE);
    }

  for (i=0; i < ntex; i++)
    {
      gegl_clReleaseMemObject (input_tex.tex[i]);
      gegl_clReleaseMemObject (output_tex.tex[i]);
    }

  gegl_free(input_tex.tex);
  gegl_free(output_tex.tex);
  gegl_free(input_tex.region);
  gegl_free(output_tex.region);

  return TRUE;

error:
  g_warning("[OpenCL] Error: %s", gegl_cl_errstring(errcode));

    for (i=0; i < ntex; i++)
      {
        if (input_tex.tex[i])  gegl_clReleaseMemObject (input_tex.tex[i]);
        if (output_tex.tex[i]) gegl_clReleaseMemObject (output_tex.tex[i]);
      }

  if (input_tex.tex)     gegl_free(input_tex.tex);
  if (output_tex.tex)    gegl_free(output_tex.tex);
  if (input_tex.region)  gegl_free(input_tex.region);
  if (output_tex.region) gegl_free(output_tex.region);

  return FALSE;
}

#undef CL_ERROR

static gboolean
gegl_operation_point_filter_process (GeglOperation       *operation,
                                     GeglBuffer          *input,
                                     GeglBuffer          *output,
                                     const GeglRectangle *result)
{
  const Babl *in_format  = gegl_operation_get_format (operation, "input");
  const Babl *out_format = gegl_operation_get_format (operation, "output");
  GeglOperationPointFilterClass *point_filter_class;

  point_filter_class = GEGL_OPERATION_POINT_FILTER_GET_CLASS (operation);

  if ((result->width > 0) && (result->height > 0))
    {
      if (cl_state.is_accelerated && point_filter_class->cl_process)
        {
          if (gegl_operation_point_filter_cl_process_full (operation, input, output, result))
            return TRUE;
        }

      {
        GeglBufferIterator *i = gegl_buffer_iterator_new (output, result, out_format, GEGL_BUFFER_WRITE);
        gint read = /*output == input ? 0 :*/ gegl_buffer_iterator_add (i, input,  result, in_format, GEGL_BUFFER_READ);
        /* using separate read and write iterators for in-place ideally a single
         * readwrite indice would be sufficient
         */
          while (gegl_buffer_iterator_next (i))
            point_filter_class->process (operation, i->data[read], i->data[0], i->length, &i->roi[0]);
      }
    }
  return TRUE;
}

gboolean gegl_can_do_inplace_processing (GeglOperation       *operation,
                                         GeglBuffer          *input,
                                         const GeglRectangle *result);

gboolean gegl_can_do_inplace_processing (GeglOperation       *operation,
                                         GeglBuffer          *input,
                                         const GeglRectangle *result)
{
  if (!input ||
      GEGL_IS_CACHE (input))
    return FALSE;
  if (gegl_object_get_has_forked (input))
    return FALSE;

  if (input->format == gegl_operation_get_format (operation, "output") &&
      gegl_rectangle_contains (gegl_buffer_get_extent (input), result))
    return TRUE;
  return FALSE;
}


static gboolean gegl_operation_point_filter_op_process
                              (GeglOperation       *operation,
                               GeglOperationContext *context,
                               const gchar          *output_pad,
                               const GeglRectangle  *roi)
{
  GeglBuffer               *input;
  GeglBuffer               *output;
  gboolean                  success = FALSE;

  input = gegl_operation_context_get_source (context, "input");

  if (gegl_can_do_inplace_processing (operation, input, roi))
    {
      output = g_object_ref (input);
      gegl_operation_context_take_object (context, "output", G_OBJECT (output));
    }
  else
    {
      output = gegl_operation_context_get_target (context, "output");
    }

  success = gegl_operation_point_filter_process (operation, input, output, roi);
  if (output == GEGL_BUFFER (operation->node->cache))
    gegl_cache_computed (operation->node->cache, roi);

  if (input != NULL)
    g_object_unref (input);
  return success;
}
