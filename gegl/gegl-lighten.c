#include "gegl-lighten.h"
#include "gegl-scanline-processor.h"
#include "gegl-image-iterator.h"
#include "gegl-utils.h"

static void class_init (GeglLightenClass * klass);
static void init (GeglLighten * self, GeglLightenClass * klass);

static GeglScanlineFunc get_scanline_func(GeglComp * comp, GeglColorSpaceType space, GeglChannelSpaceType type);

static void fg_lighten_bg_float (GeglFilter * filter, GeglImageIterator ** iters, gint width);

static gpointer parent_class = NULL;

GType
gegl_lighten_get_type (void)
{
  static GType type = 0;

  if (!type)
    {
      static const GTypeInfo typeInfo =
      {
        sizeof (GeglLightenClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) class_init,
        (GClassFinalizeFunc) NULL,
        NULL,
        sizeof (GeglLighten),
        0,
        (GInstanceInitFunc) init,
      };

      type = g_type_register_static (GEGL_TYPE_BLEND, 
                                     "GeglLighten", 
                                     &typeInfo, 
                                     0);
    }
    return type;
}

static void 
class_init (GeglLightenClass * klass)
{
  GeglCompClass *comp_class = GEGL_COMP_CLASS(klass);
  parent_class = g_type_class_peek_parent(klass);
  comp_class->get_scanline_func = get_scanline_func;
}

static void 
init (GeglLighten * self, 
      GeglLightenClass * klass)
{
}

/* scanline_funcs[data type] */
static GeglScanlineFunc scanline_funcs[] = 
{ 
  NULL, 
  NULL, 
  fg_lighten_bg_float, 
  NULL 
};

static GeglScanlineFunc
get_scanline_func(GeglComp * comp,
                  GeglColorSpaceType space,
                  GeglChannelSpaceType type)
{
  return scanline_funcs[type];
}


static void                                                            
fg_lighten_bg_float (GeglFilter * filter,              
                     GeglImageIterator ** iters,        
                     gint width)                       
{                                                                       
  gfloat **d = (gfloat**)gegl_image_iterator_color_channels(iters[0]);
  gfloat *da = (gfloat*)gegl_image_iterator_alpha_channel(iters[0]);
  gint d_color_chans = gegl_image_iterator_get_num_colors(iters[0]);

  gfloat **b = (gfloat**)gegl_image_iterator_color_channels(iters[1]);
  gfloat *ba = (gfloat*)gegl_image_iterator_alpha_channel(iters[1]);
  gint b_color_chans = gegl_image_iterator_get_num_colors(iters[1]);

  gfloat **f = (gfloat**)gegl_image_iterator_color_channels(iters[2]);
  gfloat * fa = (gfloat*)gegl_image_iterator_alpha_channel(iters[2]);
  gint f_color_chans = gegl_image_iterator_get_num_colors(iters[2]);

  gint alpha_mask = 0x0;

  if(ba) 
    alpha_mask |= GEGL_BG_ALPHA; 
  if(fa)
    alpha_mask |= GEGL_FG_ALPHA; 

  {
    gfloat *d0 = (d_color_chans > 0) ? d[0]: NULL;   
    gfloat *d1 = (d_color_chans > 1) ? d[1]: NULL;
    gfloat *d2 = (d_color_chans > 2) ? d[2]: NULL;

    gfloat *b0 = (b_color_chans > 0) ? b[0]: NULL;   
    gfloat *b1 = (b_color_chans > 1) ? b[1]: NULL;
    gfloat *b2 = (b_color_chans > 2) ? b[2]: NULL;

    gfloat *f0 = (f_color_chans > 0) ? f[0]: NULL;   
    gfloat *f1 = (f_color_chans > 1) ? f[1]: NULL;
    gfloat *f2 = (f_color_chans > 2) ? f[2]: NULL;

    switch(alpha_mask)
      {
      case GEGL_NO_ALPHA:
        while(width--)                                                        
          {                                                                   
            switch(d_color_chans)
              {
                case 3: *d2++ = MAX(*f2, *b2); 
                        f2++; 
                        b2++;
                case 2: *d1++ = MAX(*f1, *b1); 
                        f1++; 
                        b1++;
                case 1: *d0++ = MAX(*f0, *b0); 
                        f0++; 
                        b0++;
              }
          }
        break;
      case GEGL_FG_ALPHA:
        g_warning("Case not implemented yet\n");
        break;
      case GEGL_BG_ALPHA:
        g_warning("Case not implemented yet\n");
        break;
      case GEGL_FG_BG_ALPHA:
          {
            gfloat a;                                              
            gfloat b;                                               
            while(width--)                                                        
              {                                                                   
                a = 1.0 - *fa;                                              
                b = 1.0 - *ba;                                               
                switch(d_color_chans)
                  {
                    case 3: *d2++ = MAX(a * *b2 + *f2, b * *f2 + *b2); 
                            f2++; 
                            b2++;
                    case 2: *d1++ = MAX(a * *b1 + *f1, b * *f1 + *b1); 
                            f1++; 
                            b1++;
                    case 1: *d0++ = MAX(a * *b0 + *f0, b * *f0 + *b0); 
                            f0++; 
                            b0++;
                  }

                *da++ = *fa + *ba - *ba * *fa; 
                 fa++;
                 ba++;
              }
          }
        break;
      }
  }

  g_free(d);
  g_free(b);
  g_free(f);
}                                                                       
