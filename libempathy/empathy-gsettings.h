/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_GSETTINGS_H__
#define __EMPATHY_GSETTINGS_H__

#include <gio/gio.h>

G_BEGIN_DECLS

#define EMPATHY_PREFS_SCHEMA "org.gnome.Empathy"
#define EMPATHY_PREFS_USE_CONN                     "use-conn"
#define EMPATHY_PREFS_AUTOCONNECT                  "autoconnect"
#define EMPATHY_PREFS_AUTOAWAY                     "autoaway"
#define EMPATHY_PREFS_FILE_TRANSFER_DEFAULT_FOLDER "file-transfer-default-folder"

#define EMPATHY_PREFS_NOTIFICATIONS_SCHEMA EMPATHY_PREFS_SCHEMA ".notifications"
#define EMPATHY_PREFS_NOTIFICATIONS_ENABLED        "notifications-enabled"
#define EMPATHY_PREFS_NOTIFICATIONS_DISABLED_AWAY  "notifications-disabled-away"
#define EMPATHY_PREFS_NOTIFICATIONS_FOCUS          "notifications-focus"
#define EMPATHY_PREFS_NOTIFICATIONS_CONTACT_SIGNIN "notifications-contact-signin"
#define EMPATHY_PREFS_NOTIFICATIONS_CONTACT_SIGNOUT "notifications-contact-signout"
#define EMPATHY_PREFS_NOTIFICATIONS_POPUPS_WHEN_AVAILABLE "popups-when-available"

#define EMPATHY_PREFS_SOUNDS_SCHEMA EMPATHY_PREFS_SCHEMA ".sounds"
#define EMPATHY_PREFS_SOUNDS_ENABLED               "sounds-enabled"
#define EMPATHY_PREFS_SOUNDS_DISABLED_AWAY         "sounds-disabled-away"
#define EMPATHY_PREFS_SOUNDS_INCOMING_MESSAGE      "sounds-incoming-message"
#define EMPATHY_PREFS_SOUNDS_OUTGOING_MESSAGE      "sounds-outgoing-message"
#define EMPATHY_PREFS_SOUNDS_NEW_CONVERSATION      "sounds-new-conversation"
#define EMPATHY_PREFS_SOUNDS_SERVICE_LOGIN         "sounds-service-login"
#define EMPATHY_PREFS_SOUNDS_SERVICE_LOGOUT        "sounds-service-logout"
#define EMPATHY_PREFS_SOUNDS_CONTACT_LOGIN         "sounds-contact-login"
#define EMPATHY_PREFS_SOUNDS_CONTACT_LOGOUT        "sounds-contact-logout"

#define EMPATHY_PREFS_CALL_SCHEMA EMPATHY_PREFS_SCHEMA ".call"
#define EMPATHY_PREFS_CALL_CAMERA_DEVICE           "camera-device"
#define EMPATHY_PREFS_CALL_ECHO_CANCELLATION       "echo-cancellation"

#define EMPATHY_PREFS_CHAT_SCHEMA EMPATHY_PREFS_SCHEMA ".conversation"
#define EMPATHY_PREFS_CHAT_SHOW_SMILEYS            "graphical-smileys"
#define EMPATHY_PREFS_CHAT_SHOW_CONTACTS_IN_ROOMS  "show-contacts-in-rooms"
#define EMPATHY_PREFS_CHAT_THEME                   "theme"
#define EMPATHY_PREFS_CHAT_THEME_VARIANT           "theme-variant"
#define EMPATHY_PREFS_CHAT_ADIUM_PATH              "adium-path"
#define EMPATHY_PREFS_CHAT_SPELL_CHECKER_LANGUAGES "spell-checker-languages"
#define EMPATHY_PREFS_CHAT_SPELL_CHECKER_ENABLED   "spell-checker-enabled"
#define EMPATHY_PREFS_CHAT_NICK_COMPLETION_CHAR    "nick-completion-char"
#define EMPATHY_PREFS_CHAT_AVATAR_IN_ICON          "avatar-in-icon"
#define EMPATHY_PREFS_CHAT_WEBKIT_DEVELOPER_TOOLS  "enable-webkit-developer-tools"
#define EMPATHY_PREFS_CHAT_ROOM_LAST_ACCOUNT       "room-last-account"

#define EMPATHY_PREFS_UI_SCHEMA EMPATHY_PREFS_SCHEMA ".ui"
#define EMPATHY_PREFS_UI_SEPARATE_CHAT_WINDOWS     "separate-chat-windows"
#define EMPATHY_PREFS_UI_EVENTS_NOTIFY_AREA        "events-notify-area"
#define EMPATHY_PREFS_UI_MAIN_WINDOW_HIDDEN        "main-window-hidden"
#define EMPATHY_PREFS_UI_AVATAR_DIRECTORY          "avatar-directory"
#define EMPATHY_PREFS_UI_SHOW_AVATARS              "show-avatars"
#define EMPATHY_PREFS_UI_SHOW_PROTOCOLS            "show-protocols"
#define EMPATHY_PREFS_UI_COMPACT_CONTACT_LIST      "compact-contact-list"
#define EMPATHY_PREFS_UI_CHAT_WINDOW_PANED_POS     "chat-window-paned-pos"
#define EMPATHY_PREFS_UI_SHOW_OFFLINE              "show-offline"

#define EMPATHY_PREFS_CONTACTS_SCHEMA EMPATHY_PREFS_SCHEMA ".contacts"
#define EMPATHY_PREFS_CONTACTS_SORT_CRITERIUM      "sort-criterium"

#define EMPATHY_PREFS_HINTS_SCHEMA EMPATHY_PREFS_SCHEMA ".hints"
#define EMPATHY_PREFS_HINTS_CLOSE_MAIN_WINDOW      "close-main-window"

#define EMPATHY_PREFS_LOCATION_SCHEMA EMPATHY_PREFS_SCHEMA ".location"
#define EMPATHY_PREFS_LOCATION_PUBLISH             "publish"
#define EMPATHY_PREFS_LOCATION_RESOURCE_NETWORK    "resource-network"
#define EMPATHY_PREFS_LOCATION_RESOURCE_CELL       "resource-cell"
#define EMPATHY_PREFS_LOCATION_RESOURCE_GPS        "resource-gps"
#define EMPATHY_PREFS_LOCATION_REDUCE_ACCURACY     "reduce-accuracy"

#define EMPATHY_PREFS_LOGGER_SCHEMA "org.freedesktop.Telepathy.Logger"
#define EMPATHY_PREFS_LOGGER_ENABLED "enabled"

#define EMPATHY_PREFS_DESKTOP_INTERFACE_SCHEMA "org.gnome.desktop.interface"
#define EMPATHY_PREFS_DESKTOP_INTERFACE_DOCUMENT_FONT_NAME "document-font-name"
#define EMPATHY_PREFS_DESKTOP_INTERFACE_FONT_NAME "font-name"

G_END_DECLS

#endif /* __EMPATHY_GSETTINGS_H__ */

