/* STRESS, Spatio Temporal Retinex Envelope with Stochastic Sampling
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
 * Copyright 2007 Øyvind Kolås     <oeyvindk@hig.no>
 *                Ivar Farup       <ivarf@hig.no>
 *                Allesandro Rizzi <rizzi@dti.unimi.it>
 */

#include "config.h"
#include <glib/gi18n-lib.h>


#ifdef GEGL_CHANT_PROPERTIES

gegl_chant_int (radius, _("Radius"), 2, 3000.0, 300,
                _("Neighbourhood taken into account, for enhancement ideal values are close to the longest side of the image, increasing this increases the runtime."))
gegl_chant_int (samples, _("Samples"), 0, 1000, 4,
                _("Number of samples to do per iteration looking for the range of colors."))
gegl_chant_int (iterations, _("Iterations"), 0, 1000, 10,
                _("Number of iterations, a higher number of iterations provides a less noisy rendering at computational cost."))


/*

gegl_chant_double (rgamma, _("Radial Gamma"), 0.0, 8.0, 2.0,
                _("Gamma applied to radial distribution"))

*/



#else

#define GEGL_CHANT_TYPE_AREA_FILTER
#define GEGL_CHANT_C_FILE       "stress.c"

#include "gegl-chant.h"

#define RGAMMA   2.0
#define GAMMA    1.0

#include <math.h>
#include <stdlib.h>
#include "envelopes.h"

static void stress (GeglBuffer          *src,
                    const GeglRectangle *src_rect,
                    GeglBuffer          *dst,
                    const GeglRectangle *dst_rect,
                    gint                 radius,
                    gint                 samples,
                    gint                 iterations,
                    gdouble              rgamma)
{
  gint x,y;
  gint    dst_offset=0;
  gfloat *src_buf;
  gfloat *dst_buf;
  gint    inw = src_rect->width;
  gint    inh = src_rect->height;
  gint   outw = dst_rect->width;

  /* this use of huge linear buffers should be avoided and
   * most probably would lead to great speed ups
   */

  src_buf = g_new0 (gfloat, src_rect->width * src_rect->height * 4);
  dst_buf = g_new0 (gfloat, dst_rect->width * dst_rect->height * 4);

  gegl_buffer_get (src, 1.0, src_rect, babl_format ("RGBA float"), src_buf, GEGL_AUTO_ROWSTRIDE);

  for (y=radius; y<dst_rect->height+radius; y++)
    {
      gint src_offset = (inw*y+radius)*4;
      for (x=radius; x<outw+radius; x++)
        {
          gfloat *center_pix= src_buf + src_offset;
          gfloat  min_envelope[4];
          gfloat  max_envelope[4];

          compute_envelopes (src_buf,
                             inw, inh,
                             x, y,
                             radius, samples,
                             iterations,
                             FALSE, /* same-spray */
                             rgamma,
                             min_envelope, max_envelope);
           {
              gint c;
              for (c=0;c<3;c++)
                {
                  gfloat delta = max_envelope[c]-min_envelope[c];
                  if (delta != 0)
                    {
                      dst_buf[dst_offset+c] =
                         (center_pix[c]-min_envelope[c])/delta;
                    }
                  else
                    {
                      dst_buf[dst_offset+c] = 0.5;
                    }
                }
           }
          dst_buf[dst_offset+3] = src_buf[src_offset+3];
          src_offset+=4;
          dst_offset+=4;
        }
    }
  gegl_buffer_set (dst, dst_rect, babl_format ("RGBA float"), dst_buf, GEGL_AUTO_ROWSTRIDE);
  g_free (src_buf);
  g_free (dst_buf);
}

static void prepare (GeglOperation *operation)
{
  GeglOperationAreaFilter *area = GEGL_OPERATION_AREA_FILTER (operation);
  area->left = area->right = area->top = area->bottom =
      ceil (GEGL_CHANT_PROPERTIES (operation)->radius);

  gegl_operation_set_format (operation, "output",
                             babl_format ("RaGaBaA float"));
}

static GeglRectangle
get_bounding_box (GeglOperation *operation)
{
  GeglRectangle  result = {0,0,0,0};
  GeglRectangle *in_rect = gegl_operation_source_get_bounding_box (operation,
                                                                     "input");
  if (!in_rect)
    return result;
  return *in_rect;
}

#include "opencl/gegl-cl.h"
#include "buffer/gegl-buffer-cl-iterator.h"

static const char* kernel_source =
"#define TRUE true                                                     \n"
"                                                                      \n"
"#define FALSE false                                                   \n"
"#define ANGLE_PRIME 95273                                             \n"
"#define RADIUS_PRIME 29537                                            \n"
"                                                                      \n"
"void sample_min_max(const __global   float4 *src_buf,                 \n"
"                                     int     src_width,               \n"
"                                     int     src_height,              \n"
"                    const __global   float  *radiuses,                \n"
"                    const __global   float  *lut_cos,                 \n"
"                    const __global   float  *lut_sin,                 \n"
"                                     int     x,                       \n"
"                                     int     y,                       \n"
"                                     int     radius,                  \n"
"                                     int     samples,                 \n"
"                                     float4 *min,                     \n"
"                                     float4 *max,                     \n"
"                                     int     j,                       \n"
"                                     int     iterations)              \n"
"{                                                                     \n"
"    float4 best_min;                                                  \n"
"    float4 best_max;                                                  \n"
"    float4 center_pix = *(src_buf + src_width * y + x);               \n"
"    int i;                                                            \n"
"                                                                      \n"
"    best_min = center_pix;                                            \n"
"    best_max = center_pix;                                            \n"
"                                                                      \n"
"    int angle_no  = (src_width * y + x) * (iterations) *              \n"
"                       samples + j * samples;                         \n"
"    int radius_no = angle_no;                                         \n"
"    angle_no  %= ANGLE_PRIME;                                         \n"
"    radius_no %= RADIUS_PRIME;                                        \n"
"    for(i=0; i<samples; i++)                                          \n"
"    {                                                                 \n"
"        int angle;                                                    \n"
"        float rmag;                                                   \n"
"        /* if we've sampled outside the valid image                   \n"
"           area, we grab another sample instead, this                 \n"
"           should potentially work better than mirroring              \n"
"           or extending the image */                                  \n"
"                                                                      \n"
"         angle = angle_no++;                                          \n"
"         rmag  = radiuses[radius_no++] * radius;                      \n"
"                                                                      \n"
"         if( angle_no  >= ANGLE_PRIME)                                \n"
"             angle_no   = 0;                                          \n"
"         if( radius_no >= RADIUS_PRIME)                               \n"
"             radius_no  = 0;                                          \n"
"                                                                      \n"
"         int u = x + rmag * lut_cos[angle];                           \n"
"         int v = y + rmag * lut_sin[angle];                           \n"
"                                                                      \n"
"         if(u>=src_width || u <0 || v>=src_height || v<0)             \n"
"         {                                                            \n"
"             //--i;                                                   \n"
"             continue;                                                \n"
"         }                                                            \n"
"         float4 pixel = *(src_buf + (src_width * v + u));             \n"
"         if(pixel.w<=0.0f)                                            \n"
"         {                                                            \n"
"             //--i;                                                   \n"
"             continue;                                                \n"
"         }                                                            \n"
"                                                                      \n"
"         best_min = pixel < best_min ? pixel : best_min;              \n"
"         best_max = pixel > best_max ? pixel : best_max;              \n"
"    }                                                                 \n"
"                                                                      \n"
"    (*min).xyz = best_min.xyz;                                        \n"
"    (*max).xyz = best_max.xyz;                                        \n"
"}                                                                     \n"
"                                                                      \n"
"void compute_envelopes_CL(const __global  float4 *src_buf,            \n"
"                                          int     src_width,          \n"
"                                          int     src_height,         \n"
"                          const __global  float  *radiuses,           \n"
"                          const __global  float  *lut_cos,            \n"
"                          const __global  float  *lut_sin,            \n"
"                                          int     x,                  \n"
"                                          int     y,                  \n"
"                                          int     radius,             \n"
"                                          int     samples,            \n"
"                                          int     iterations,         \n"
"                                          float4 *min_envelope,       \n"
"                                          float4 *max_envelope)       \n"
"{                                                                     \n"
"    float4 range_sum = 0;                                             \n"
"    float4 relative_brightness_sum = 0;                               \n"
"    float4 pixel = *(src_buf + src_width * y + x);                    \n"
"                                                                      \n"
"    int i;                                                            \n"
"    for(i =0; i<iterations; i++)                                      \n"
"    {                                                                 \n"
"        float4 min,max;                                               \n"
"        float4 range, relative_brightness;                            \n"
"                                                                      \n"
"        sample_min_max(src_buf, src_width, src_height,                \n"
"                        radiuses, lut_cos, lut_sin, x, y,             \n"
"                        radius,samples,&min,&max,i,iterations);       \n"
"        range = max - min;                                            \n"
"        relative_brightness = range <= 0.0f ?                         \n"
"                               0.5f : (pixel - min) / range;          \n"
"        relative_brightness_sum += relative_brightness;               \n"
"        range_sum += range;                                           \n"
"    }                                                                 \n"
"                                                                      \n"
"    float4 relative_brightness = relative_brightness_sum / iterations;\n"
"    float4 range = range_sum / iterations;                            \n"
"                                                                      \n"
"    if(max_envelope)                                                  \n"
"        *max_envelope = pixel + (1.0f - relative_brightness) * range; \n"
"                                                                      \n"
"    if(min_envelope)                                                  \n"
"        *min_envelope = pixel - relative_brightness * range;          \n"
"}                                                                     \n"
"                                                                      \n"
"__kernel void stress_CL(const __global float4 *src_buf,               \n"
"                                       int     src_width,             \n"
"                                       int     src_height,            \n"
"                              __global float  *radiuses,              \n"
"                              __global float  *lut_cos,               \n"
"                              __global float  *lut_sin,               \n"
"                              __global float4 *dst_buf,               \n"
"                                       int     radius,                \n"
"                                       int     samples,               \n"
"                                       int     iterations)            \n"
"{                                                                     \n"
"    int gidx = get_global_id(0);                                      \n"
"    int gidy = get_global_id(1);                                      \n"
"                                                                      \n"
"    int x = gidx + radius;                                            \n"
"    int y = gidy + radius;                                            \n"
"                                                                      \n"
"    int src_offset = src_width * y + x;                               \n"
"    int dst_offset = get_global_size(0) * gidy + gidx;                \n"
"                                                                      \n"
"    float4 center_pix = *(src_buf + src_offset);                      \n"
"                                                                      \n"
"    float4 min_envelope;                                              \n"
"    float4 max_envelope;                                              \n"
"                                                                      \n"
"    compute_envelopes_CL(src_buf,src_width,src_height,                \n"
"                        radiuses,lut_cos,lut_sin,x,y,                 \n"
"                        radius,samples,iterations,                    \n"
"                        &min_envelope,&max_envelope);                 \n"
"                                                                      \n"
"    float4 delta = max_envelope - min_envelope;                       \n"
"                                                                      \n"
"    //dst_buf[dst_offset] = (center_pix - min_envelope) / delta ;     \n"
"    dst_buf[dst_offset] = delta !=0 ?                                 \n"
"               (center_pix - min_envelope) / delta : 0.5f;            \n"
"    dst_buf[dst_offset].w = src_buf[src_offset].w;                    \n"
"}                                                                     \n";

static gegl_cl_run_data *cl_data = NULL;

static cl_int 
cl_stress (cl_mem                in_tex,
           cl_mem                out_tex,
           size_t                global_worksize,
           const GeglRectangle   *src_roi, 
           const GeglRectangle   *roi,
           gint                  radius,
           gint                  samples,
           gint                  iterations,
           gdouble               rgamma)
{
  cl_int cl_err = 0;
  if (!cl_data)
  {
    const char *kernel_name[] ={"stress_CL", NULL};
      cl_data = gegl_cl_compile_and_build(kernel_source, kernel_name);
  }
  if (!cl_data)  return 0;

  const size_t gbl_size[2] = {roi->width,roi->height};

  compute_luts(rgamma);

  cl_mem cl_lut_cos, cl_lut_sin, cl_radiuses;

  cl_lut_cos = gegl_clCreateBuffer( gegl_cl_get_context(),
									CL_MEM_READ_ONLY,
									ANGLE_PRIME * sizeof(cl_float), 
									NULL, 
									&cl_err);

  cl_err |= gegl_clEnqueueWriteBuffer(gegl_cl_get_command_queue(), 
									  cl_lut_cos,
									  CL_TRUE, 
									  NULL, 
									  ANGLE_PRIME * sizeof(cl_float), 
									  lut_cos, 
									  NULL, NULL, NULL);

  cl_lut_sin = gegl_clCreateBuffer( gegl_cl_get_context(),
									CL_MEM_READ_ONLY,
									ANGLE_PRIME * sizeof(cl_float), 
									NULL, 
									&cl_err);

  cl_err |= gegl_clEnqueueWriteBuffer(gegl_cl_get_command_queue(), 
									  cl_lut_sin,
									  CL_TRUE, NULL, 
									  ANGLE_PRIME * sizeof(cl_float), 
									  lut_sin, 
									  NULL, NULL, NULL);

  cl_radiuses = gegl_clCreateBuffer(gegl_cl_get_context(),
									CL_MEM_READ_ONLY,
									RADIUS_PRIME * sizeof(cl_float), 
									NULL, 
									&cl_err);

  cl_err |= gegl_clEnqueueWriteBuffer(gegl_cl_get_command_queue(), 
									  cl_radiuses,
									  CL_TRUE, NULL, 
									  RADIUS_PRIME * sizeof(cl_float), 
									  radiuses, 
									  NULL, NULL, NULL);

  if (CL_SUCCESS != cl_err)    return cl_err;

  cl_int cl_src_width  = src_roi->width;
  cl_int cl_src_height = src_roi->height;
  cl_int cl_radius     = radius;
  cl_int cl_samples    = samples;
  cl_int cl_iterations = iterations;

  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 0, sizeof(cl_mem), (void*)&in_tex);

  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 1, sizeof(cl_int), (void*)&cl_src_width);
  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 2, sizeof(cl_int), (void*)&cl_src_height);

  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 3, sizeof(cl_mem), (void*)&cl_radiuses);
  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 4, sizeof(cl_mem), (void*)&cl_lut_cos);
  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 5, sizeof(cl_mem), (void*)&cl_lut_sin);

  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 6, sizeof(cl_mem), (void*)&out_tex);

  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 7, sizeof(cl_int), (void*)&cl_radius);
  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 8, sizeof(cl_int), (void*)&cl_samples);
  cl_err |= gegl_clSetKernelArg(cl_data->kernel[0], 9, sizeof(cl_int), (void*)&cl_iterations);

  if (cl_err != CL_SUCCESS) return cl_err;

  cl_err = gegl_clEnqueueNDRangeKernel( gegl_cl_get_command_queue(), 
										cl_data->kernel[0],
										2, NULL,
										gbl_size, NULL,
										0, NULL, NULL);
  if (cl_err != CL_SUCCESS) return cl_err;

  cl_err = gegl_clEnqueueBarrier(gegl_cl_get_command_queue());
  if (CL_SUCCESS != cl_err)    return cl_err;

  gegl_clFinish(gegl_cl_get_command_queue ());

  gegl_clReleaseMemObject(cl_radiuses);
  gegl_clReleaseMemObject(cl_lut_cos);
  gegl_clReleaseMemObject(cl_lut_sin);
}

static gboolean
cl_process (GeglOperation       *operation,
            GeglBuffer          *input,
            GeglBuffer          *output,
            const GeglRectangle *result)
{
  const Babl *in_format  = babl_format("RGBA float");
  const Babl *out_format = babl_format("RGBA float");
  gint err;
  gint j;
  cl_int cl_err;

  GeglOperationAreaFilter *op_area = GEGL_OPERATION_AREA_FILTER (operation);
  GeglChantO *o = GEGL_CHANT_PROPERTIES (operation);
  
  GeglBufferClIterator *i = gegl_buffer_cl_iterator_new (output,result, out_format, GEGL_CL_BUFFER_WRITE);
                gint read = gegl_buffer_cl_iterator_add_2 (i, input, result, in_format, GEGL_CL_BUFFER_READ,op_area->left, op_area->right, op_area->top, op_area->bottom);

  while (gegl_buffer_cl_iterator_next (i, &err))
  {
    if (err) return FALSE;
    for (j=0; j < i->n; j++)
    {
      cl_err = cl_stress(i->tex[read][j], i->tex[0][j],i->size[0][j], &i->roi[read][j],&i->roi[0][j],
                         o->radius,o->samples,o->iterations,RGAMMA);
      if (cl_err != CL_SUCCESS)
      {
        g_warning("[OpenCL] Error in %s [GeglOperationFilter:Edge-sobel] Kernel\n");
        return FALSE;
      }
    }
  }
  return TRUE;
}

static gboolean
process (GeglOperation       *operation,
         GeglBuffer          *input,
         GeglBuffer          *output,
         const GeglRectangle *result)
{
  GeglChantO *o = GEGL_CHANT_PROPERTIES (operation);
  GeglRectangle compute;
  compute = gegl_operation_get_required_for_output (operation, "input",result);

  if (o->radius < 501 && cl_state.is_accelerated)
    if(cl_process(operation, input, output, result))
      return TRUE;

  stress (input, &compute, output, result,
          o->radius,
          o->samples,
          o->iterations,
          RGAMMA);
  return  TRUE;
}


static void
gegl_chant_class_init (GeglChantClass *klass)
{
  GeglOperationClass       *operation_class;
  GeglOperationFilterClass *filter_class;

  operation_class = GEGL_OPERATION_CLASS (klass);
  filter_class    = GEGL_OPERATION_FILTER_CLASS (klass);

  filter_class->process = process;
  operation_class->prepare  = prepare;
  /* we override get_bounding_box to avoid growing the size of what is defined
   * by the filter. This also allows the tricks used to treat alpha==0 pixels
   * in the image as source data not to be skipped by the stochastic sampling
   * yielding correct edge behavior.
   */
  operation_class->get_bounding_box = get_bounding_box;

  operation_class->name        = "gegl:stress";
  operation_class->opencl_support = TRUE;
  operation_class->categories  = "enhance";
  operation_class->description =
        _("Spatio Temporal Retinex-like Envelope with Stochastic Sampling.");
}

#endif
