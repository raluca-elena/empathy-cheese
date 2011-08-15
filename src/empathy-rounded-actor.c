/*
 * empathy-rounded-actor.c - Source for EmpathyRoundedActor
 * Copyright (C) 2011 Collabora Ltd.
 * @author Emilio Pozuelo Monfort <emilio.pozuelo@collabora.co.uk>
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
 */


#include <clutter/clutter.h>
#include <clutter-gtk/clutter-gtk.h>

#include "empathy-rounded-actor.h"

G_DEFINE_TYPE(EmpathyRoundedActor, empathy_rounded_actor, GTK_CLUTTER_TYPE_ACTOR)

static void
empathy_rounded_actor_paint (ClutterActor *actor)
{
  ClutterActorBox allocation = { 0, };
  gfloat width, height;

  clutter_actor_get_allocation_box (actor, &allocation);
  clutter_actor_box_get_size (&allocation, &width, &height);

  cogl_path_new ();

  /* create and store a path describing a rounded rectangle */
  cogl_path_round_rectangle (0, 0, width, height, height / 2, 0.1);

  cogl_clip_push_from_path ();

  CLUTTER_ACTOR_CLASS (empathy_rounded_actor_parent_class)->paint (actor);

  cogl_clip_pop ();
}

static void
empathy_rounded_actor_init (EmpathyRoundedActor *self)
{
}

static void
empathy_rounded_actor_class_init (EmpathyRoundedActorClass *klass)
{
  ClutterActorClass *actor_class = CLUTTER_ACTOR_CLASS (klass);

  actor_class->paint = empathy_rounded_actor_paint;
}

ClutterActor *
empathy_rounded_actor_new (void)
{
  return CLUTTER_ACTOR (
    g_object_new (EMPATHY_TYPE_ROUNDED_ACTOR, NULL));
}
