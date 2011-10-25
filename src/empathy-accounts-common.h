/*
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __EMPATHY_ACCOUNTS_COMMON_H__
#define __EMPATHY_ACCOUNTS_COMMON_H__

gboolean empathy_accounts_has_non_salut_accounts (TpAccountManager *manager);

gboolean empathy_accounts_has_accounts (TpAccountManager *manager);

void empathy_accounts_show_accounts_ui (TpAccountManager *manager,
    TpAccount *account,
    gboolean assistant,
    GApplication *app);

#endif /* __EMPATHY_ACCOUNTS_COMMON_H__ */
