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

#include <glib-object.h>

#include "gegl-types.h"

#include "gegl-need-visitor.h"
#include "graph/gegl-node.h"
#include "graph/gegl-pad.h"
#include "graph/gegl-visitable.h"
#include "operation/gegl-operation.h"


static void gegl_need_visitor_class_init (GeglNeedVisitorClass *klass);
static void visit_node (GeglVisitor *self,
                        GeglNode    *node);


G_DEFINE_TYPE (GeglNeedVisitor, gegl_need_visitor, GEGL_TYPE_VISITOR)


static void
gegl_need_visitor_class_init (GeglNeedVisitorClass *klass)
{
  GeglVisitorClass *visitor_class = GEGL_VISITOR_CLASS (klass);

  visitor_class->visit_node = visit_node;
}

static void
gegl_need_visitor_init (GeglNeedVisitor *self)
{
}

static void
visit_node (GeglVisitor *self,
            GeglNode    *node)
{
  GeglOperation *operation  = node->operation;
  gpointer       context_id = self->context_id;

  GEGL_VISITOR_CLASS (gegl_need_visitor_parent_class)->visit_node (self, node);
  gegl_operation_calc_source_regions (operation, context_id);
}