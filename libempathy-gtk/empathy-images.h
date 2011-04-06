/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#ifndef __EMPATHY_IMAGES_H__
#define __EMPATHY_IMAGES_H__

G_BEGIN_DECLS

#define EMPATHY_IMAGE_OFFLINE             "user-offline"
/* user-invisible is not (yet?) in the naming spec but already implemented by
 * some theme */
#define EMPATHY_IMAGE_HIDDEN              "user-invisible"
#define EMPATHY_IMAGE_AVAILABLE           "user-available"
#define EMPATHY_IMAGE_BUSY                "user-busy"
#define EMPATHY_IMAGE_AWAY                "user-away"
#define EMPATHY_IMAGE_EXT_AWAY            "user-extended-away"
#define EMPATHY_IMAGE_IDLE                "user-idle"
#define EMPATHY_IMAGE_PENDING             "empathy-pending"

#define EMPATHY_IMAGE_MESSAGE             "im-message"
#define EMPATHY_IMAGE_NEW_MESSAGE         "im-message-new"
#define EMPATHY_IMAGE_TYPING              "user-typing"
#define EMPATHY_IMAGE_CONTACT_INFORMATION "gtk-info"
#define EMPATHY_IMAGE_GROUP_MESSAGE       "system-users"
#define EMPATHY_IMAGE_SMS                 "stock_cell-phone"
#define EMPATHY_IMAGE_VOIP                "audio-input-microphone"
#define EMPATHY_IMAGE_VIDEO_CALL          "camera-web"
#define EMPATHY_IMAGE_LOG                 "document-open-recent"
#define EMPATHY_IMAGE_DOCUMENT_SEND       "document-send"
#define EMPATHY_IMAGE_AVATAR_DEFAULT      "avatar-default"

G_END_DECLS

#endif /* __EMPATHY_IMAGES_ICONS_H__ */
