/* This file is part of GEGL
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
 * Copyright 2006 Øyvind Kolås
 */

#include "config.h"
#if 0
#include "../gegl-buffer/clog.h"
#endif

#include <glib-object.h>

#include "gegl-types.h"

#include "gegl-debug-rect-visitor.h"
#include "operation/gegl-operation.h"
#include "graph/gegl-node.h"
#include "graph/gegl-node-dynamic.h"
#include "graph/gegl-pad.h"
#include "graph/gegl-visitable.h"


static void gegl_debug_rect_visitor_class_init (GeglDebugRectVisitorClass *klass);
static void visit_node (GeglVisitor *self,
                        GeglNode    *node);


G_DEFINE_TYPE (GeglDebugRectVisitor, gegl_debug_rect_visitor, GEGL_TYPE_VISITOR)


static void
gegl_debug_rect_visitor_class_init (GeglDebugRectVisitorClass *klass)
{
  GeglVisitorClass *visitor_class = GEGL_VISITOR_CLASS (klass);

  visitor_class->visit_node = visit_node;
}

static void
gegl_debug_rect_visitor_init (GeglDebugRectVisitor *self)
{
}

static void
visit_node (GeglVisitor *self,
            GeglNode    *node)
{
  GeglNodeDynamic *dynamic = gegl_node_get_dynamic (node, self->context_id);

  GEGL_VISITOR_CLASS (gegl_debug_rect_visitor_parent_class)->visit_node (self, node);

  g_warning (
    "%s\n"
    "\thave: %ix%i %i,%i\n"
    "\tneed: %ix%i %i,%i\n"
    "\tresult: %ix%i %i,%i\n"
    "\trefs: %i",
    gegl_node_get_debug_name (node),
    node->have_rect.width, node->have_rect.height,
    node->have_rect.x, node->have_rect.y,
    dynamic->need_rect.width, dynamic->need_rect.height,
    dynamic->need_rect.x, dynamic->need_rect.y,
    dynamic->result_rect.width, dynamic->result_rect.height,
    dynamic->result_rect.x, dynamic->result_rect.y,
    dynamic->refs);
}