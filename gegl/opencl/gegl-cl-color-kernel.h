static const char* kernel_color_source =
"/* This is almost a copy-paste from babl/base conversion functions in RGBA space */      \n"
"                                                                                         \n"
"/* Alpha threshold used in the reference implementation for                              \n"
" * un-pre-multiplication of color data:                                                  \n"
" *                                                                                       \n"
" * 0.01 / (2^16 - 1)                                                                     \n"
" */                                                                                      \n"
"#define BABL_ALPHA_THRESHOLD 0.000000152590219                                           \n"
"                                                                                         \n"
"float linear_to_gamma_2_2 (float value)                                                  \n"
"{                                                                                        \n"
"  if (value > 0.0030402477f)                                                             \n"
"    return 1.055f * native_powr (value, (1.0f/2.4f)) - 0.055f;                           \n"
"  return 12.92f * value;                                                                 \n"
"}                                                                                        \n"
"                                                                                         \n"
"float gamma_2_2_to_linear (float value)                                                  \n"
"{                                                                                        \n"
"  if (value > 0.03928f)                                                                  \n"
"    return native_powr ((value + 0.055f) / 1.055f, 2.4f);                                \n"
"  return value / 12.92f;                                                                 \n"
"}                                                                                        \n"
"                                                                                         \n"
"__constant sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE |                             \n"
"                               CLK_ADDRESS_NONE            |                             \n"
"                               CLK_FILTER_NEAREST;                                       \n"
"                                                                                         \n"
"/* RGBA float -> RaGaBaA float */                                                        \n"
"__kernel void non_premultiplied_to_premultiplied (__read_only  image2d_t in,             \n"
"                                                  __write_only image2d_t out)            \n"
"{                                                                                        \n"
"  int2 gid = (int2)(get_global_id(0), get_global_id(1));                                 \n"
"  float4 in_v  = read_imagef(in, sampler, gid);                                          \n"
"  float4 out_v;                                                                          \n"
"  out_v   = in_v * in_v.w;                                                               \n"
"  out_v.w = in_v.w;                                                                      \n"
"  write_imagef(out, gid, out_v);                                                         \n"
"}                                                                                        \n"
"                                                                                         \n"
"/* RaGaBaA float -> RGBA float */                                                        \n"
"__kernel void premultiplied_to_non_premultiplied (__read_only  image2d_t in,             \n"
"                                                  __write_only image2d_t out)            \n"
"{                                                                                        \n"
"  int2 gid = (int2)(get_global_id(0), get_global_id(1));                                 \n"
"  float4 in_v  = read_imagef(in, sampler, gid);                                          \n"
"  float4 out_v;                                                                          \n"
"  out_v = (in_v.w > BABL_ALPHA_THRESHOLD)? in_v / in_v.w : (float4)(0.0f);               \n"
"  out_v.w = in_v.w;                                                                      \n"
"  write_imagef(out, gid, out_v);                                                         \n"
"}                                                                                        \n"
"                                                                                         \n"
"/* RGBA float -> R'G'B'A float */                                                        \n"
"__kernel void rgba2rgba_gamma_2_2 (__read_only  image2d_t in,                            \n"
"                                   __write_only image2d_t out)                           \n"
"{                                                                                        \n"
"  int2 gid = (int2)(get_global_id(0), get_global_id(1));                                 \n"
"  float4 in_v  = read_imagef(in, sampler, gid);                                          \n"
"  float4 out_v;                                                                          \n"
"  out_v = (float4)(linear_to_gamma_2_2(in_v.x),                                          \n"
"                   linear_to_gamma_2_2(in_v.y),                                          \n"
"                   linear_to_gamma_2_2(in_v.z),                                          \n"
"                   in_v.w);                                                              \n"
"  write_imagef(out, gid, out_v);                                                         \n"
"}                                                                                        \n"
"                                                                                         \n"
"/* R'G'B'A float -> RGBA float */                                                        \n"
"__kernel void rgba_gamma_2_22rgba (__read_only  image2d_t in,                            \n"
"                                   __write_only image2d_t out)                           \n"
"{                                                                                        \n"
"  int2 gid = (int2)(get_global_id(0), get_global_id(1));                                 \n"
"  float4 in_v  = read_imagef(in, sampler, gid);                                          \n"
"  float4 out_v;                                                                          \n"
"  out_v = (float4)(gamma_2_2_to_linear(in_v.x),                                          \n"
"                   gamma_2_2_to_linear(in_v.y),                                          \n"
"                   gamma_2_2_to_linear(in_v.z),                                          \n"
"                   in_v.w);                                                              \n"
"  write_imagef(out, gid, out_v);                                                         \n"
"}                                                                                        \n"
"                                                                                         \n"
"/* RGBA float -> R'aG'aB'aA float */                                                     \n"
"__kernel void rgba2rgba_gamma_2_2_premultiplied (__read_only  image2d_t in,              \n"
"                                                 __write_only image2d_t out)             \n"
"{                                                                                        \n"
"  int2 gid = (int2)(get_global_id(0), get_global_id(1));                                 \n"
"  float4 in_v  = read_imagef(in, sampler, gid);                                          \n"
"  float4 out_v;                                                                          \n"
"  out_v = (float4)(linear_to_gamma_2_2(in_v.x) * in_v.w,                                 \n"
"                   linear_to_gamma_2_2(in_v.y) * in_v.w,                                 \n"
"                   linear_to_gamma_2_2(in_v.z) * in_v.w,                                 \n"
"                   in_v.w);                                                              \n"
"  write_imagef(out, gid, out_v);                                                         \n"
"}                                                                                        \n"
"                                                                                         \n"
"/* R'aG'aB'aA float -> RGBA float */                                                     \n"
"__kernel void rgba_gamma_2_2_premultiplied2rgba (__read_only  image2d_t in,              \n"
"                                                 __write_only image2d_t out)             \n"
"{                                                                                        \n"
"  int2 gid = (int2)(get_global_id(0), get_global_id(1));                                 \n"
"  float4 in_v  = read_imagef(in, sampler, gid);                                          \n"
"  float4 out_v;                                                                          \n"
"  out_v = (in_v.w > BABL_ALPHA_THRESHOLD)? (float4)(linear_to_gamma_2_2(in_v.x) / in_v.w,\n"
"                                                    linear_to_gamma_2_2(in_v.y) / in_v.w,\n"
"                                                    linear_to_gamma_2_2(in_v.z) / in_v.w,\n"
"                                                    in_v.w) :                            \n"
"                                           (float4)(0.0f);                               \n"
"  write_imagef(out, gid, out_v);                                                         \n"
"}                                                                                        \n"
"                                                                                         \n"
"/* RGBA float -> RGBA u8 */                                                              \n"
"__kernel void rgbaf_to_rgbau8 (__read_only  image2d_t in,                                \n"
"                               __write_only image2d_t out)                               \n"
"{                                                                                        \n"
"  int2 gid = (int2)(get_global_id(0), get_global_id(1));                                 \n"
"  float4 in_v  = read_imagef(in, sampler, gid);                                          \n"
"  float4 out_v;                                                                          \n"
"  write_imagef(out, gid, out_v);                                                         \n"
"}                                                                                        \n"
"                                                                                         \n"
"/* RGBAu8 -> RGBA float */                                                               \n"
"__kernel void rgbau8_to_rgbaf (__read_only  image2d_t in,                                \n"
"                               __write_only image2d_t out)                               \n"
"{                                                                                        \n"
"  int2 gid = (int2)(get_global_id(0), get_global_id(1));                                 \n"
"  float4 in_v  = read_imagef(in, sampler, gid);                                          \n"
"  float4 out_v;                                                                          \n"
"  write_imagef(out, gid, out_v);                                                         \n"
"}                                                                                        \n";
