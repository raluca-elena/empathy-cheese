/*
 * mcp-account-manager-goa.h
 *
 * McpAccountManagerGoa - a Mission Control plugin to expose GNOME Online
 * Accounts with chat capabilities (e.g. Facebook) to Mission Control
 *
 * Copyright (C) 2010-2011 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *    Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <mission-control-plugins/mission-control-plugins.h>

#ifndef __MCP_ACCOUNT_MANAGER_GOA_H__
#define __MCP_ACCOUNT_MANAGER_GOA_H__

G_BEGIN_DECLS

#define MCP_TYPE_ACCOUNT_MANAGER_GOA	(mcp_account_manager_goa_get_type ())
#define MCP_ACCOUNT_MANAGER_GOA(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), MCP_TYPE_ACCOUNT_MANAGER_GOA, McpAccountManagerGoa))
#define MCP_ACCOUNT_MANAGER_GOA_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), MCP_TYPE_ACCOUNT_MANAGER_GOA, McpAccountManagerGoaClass))
#define MCP_IS_ACCOUNT_MANAGER_GOA(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MCP_TYPE_ACCOUNT_MANAGER_GOA))
#define MCP_IS_ACCOUNT_MANAGER_GOA_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), MCP_TYPE_ACCOUNT_MANAGER_GOA))
#define MCP_ACCOUNT_MANAGER_GOA_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), MCP_TYPE_ACCOUNT_MANAGER_GOA, McpAccountManagerGoaClass))

typedef struct _McpAccountManagerGoa McpAccountManagerGoa;
typedef struct _McpAccountManagerGoaClass McpAccountManagerGoaClass;
typedef struct _McpAccountManagerGoaPrivate McpAccountManagerGoaPrivate;

struct _McpAccountManagerGoa
{
  GObject parent;

  McpAccountManagerGoaPrivate *priv;
};

struct _McpAccountManagerGoaClass
{
  GObjectClass parent_class;
};

GType mcp_account_manager_goa_get_type (void);

G_END_DECLS

#endif
