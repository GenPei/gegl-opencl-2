#include "gegl.h"
#include "gegl-cl-types.h"
#include "gegl-cl-init.h"
#include "gegl-cl-color.h"
#include "gegl-cl-texture.h"

GeglClTexture *
gegl_cl_texture_new (gint width, gint height, const Babl *format,
                     gint px_pitch, gpointer data)
{
  cl_int errcode;
  size_t px_size, pitch;

  GeglClTexture *texture = g_new (GeglClTexture, 1);
  texture->width = width;
  texture->height = height;
  texture->babl_format = format;

  g_assert((px_pitch > 0 && data == NULL) == FALSE);

  if (!gegl_cl_babl_to_cl_image_format (format, &texture->tex_format, &px_size))
    return NULL;

  if (px_pitch == 0)
    pitch = 0;
  else
    pitch = px_pitch * px_size;

  texture->data  = gegl_clCreateImage2D (gegl_cl_get_context(),
                                         CL_MEM_READ_WRITE,
                                         &texture->tex_format,
                                         texture->width,
                                         texture->height,
                                         pitch, data, &errcode);
  if (errcode != CL_SUCCESS)
    {
      g_free(texture);
      return NULL;
    }

  return texture;
}

void
gegl_cl_texture_free (GeglClTexture *texture)
{
  gegl_clReleaseMemObject (texture->data);
  g_free (texture);
}

void
gegl_cl_texture_get (const GeglClTexture *texture,
                     gpointer             dst)
{
  const size_t origin[3] = {0,0,0};
  const size_t region[3] = {texture->width,
                            texture->height,
                            1};
  gegl_clEnqueueReadImage(gegl_cl_get_command_queue(),
                          texture->data, CL_FALSE, origin, region, 0, 0, dst,
                          0, NULL, NULL);
}

void
gegl_cl_texture_set (GeglClTexture  *texture,
                     const gpointer  src)
{
  const size_t origin[3] = {0,0,0};
  const size_t region[3] = {texture->width,
                            texture->height,
                            1};
  gegl_clEnqueueWriteImage(gegl_cl_get_command_queue(),
                          texture->data, CL_TRUE, origin, region, 0, 0, src,
                          0, NULL, NULL);
}

GeglClTexture *
gegl_cl_texture_dup (const GeglClTexture *texture)
{
  const size_t origin[3] = {0,0,0};
  const size_t region[3] = {texture->width,
                            texture->height,
                            1};

  GeglClTexture *new_texture = gegl_cl_texture_new (texture->width,
                                                    texture->height,
                                                    texture->babl_format,
                                                    0, NULL);

  if (new_texture)
    gegl_clEnqueueCopyImage(gegl_cl_get_command_queue(),
                            texture->data, new_texture->data,
                            origin, origin, region,
                            0, NULL, NULL);
  return new_texture;
}

void
gegl_cl_texture_copy (const GeglClTexture  *src,
                      GeglRectangle        *src_rect,
                      GeglClTexture        *dst,
                      gint                  dst_x,
                      gint                  dst_y)
{
  const size_t src_origin[3] = {src_rect->x, src_rect->y, 0};
  const size_t dst_origin[3] = {dst_x, dst_y, 0};
  const size_t region[3] = {src_rect->width, src_rect->height, 1};

  gegl_clEnqueueCopyImage (gegl_cl_get_command_queue(),
                           src->data, dst->data,
                           src_origin, dst_origin, region,
                           0, NULL, NULL);
}