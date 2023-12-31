/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann
 * Copyright (C) 2014 Michal Ratajsky <michal.ratajsky@gmail.com>
 * Copyright (C) 2019 Victor Kareh <vkareh@vkareh.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <ctk/ctk.h>
#include <cdk/cdkkeysyms.h>

#include <libcafemixer/cafemixer.h>
#include <cafe-panel-applet.h>

#define CAFE_DESKTOP_USE_UNSTABLE_API
#include <libcafe-desktop/cafe-desktop-utils.h>

#include "gvc-channel-bar.h"
#include "gvc-stream-applet-icon.h"

struct _GvcStreamAppletIconPrivate
{
        gchar                 **icon_names;
        CtkImage               *image;
        CtkWidget              *dock;
        CtkWidget              *bar;
        guint                   current_icon;
        gchar                  *display_name;
        CafeMixerStreamControl *control;
        CafePanelAppletOrient   orient;
        guint                   size;
};

enum
{
        PROP_0,
        PROP_CONTROL,
        PROP_DISPLAY_NAME,
        PROP_ICON_NAMES,
        N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

static void gvc_stream_applet_icon_finalize   (GObject *object);

G_DEFINE_TYPE_WITH_PRIVATE (GvcStreamAppletIcon, gvc_stream_applet_icon, CTK_TYPE_EVENT_BOX)

static gboolean
popup_dock (GvcStreamAppletIcon *icon, guint time)
{
        CtkAllocation  allocation;
        CdkDisplay    *display;
        CdkScreen     *screen;
        int            x, y;
        CdkMonitor    *monitor_num;
        CdkRectangle   monitor;
        CtkRequisition dock_req;

        screen = ctk_widget_get_screen (CTK_WIDGET (icon));
        ctk_widget_get_allocation (CTK_WIDGET (icon), &allocation);
        cdk_window_get_origin (ctk_widget_get_window (CTK_WIDGET (icon)), &allocation.x, &allocation.y);

        /* position roughly */
        ctk_window_set_screen (CTK_WINDOW (icon->priv->dock), screen);
        gvc_channel_bar_set_orientation (GVC_CHANNEL_BAR (icon->priv->bar), icon->priv->orient);

        monitor_num = cdk_display_get_monitor_at_point (cdk_screen_get_display (screen), allocation.x, allocation.y);
        cdk_monitor_get_geometry (monitor_num, &monitor);

        ctk_container_foreach (CTK_CONTAINER (icon->priv->dock), (CtkCallback) ctk_widget_show_all, NULL);
        ctk_widget_get_preferred_size (icon->priv->dock, &dock_req, NULL);

        if (icon->priv->orient == CAFE_PANEL_APPLET_ORIENT_LEFT || icon->priv->orient == CAFE_PANEL_APPLET_ORIENT_RIGHT) {
                if (allocation.x + allocation.width + dock_req.width <= monitor.x + monitor.width)
                        x = allocation.x + allocation.width;
                else
                        x = allocation.x - dock_req.width;

                if (allocation.y + dock_req.height <= monitor.y + monitor.height)
                        y = allocation.y;
                else
                        y = monitor.y + monitor.height - dock_req.height;
        } else {
                if (allocation.y + allocation.height + dock_req.height <= monitor.y + monitor.height)
                        y = allocation.y + allocation.height;
                else
                        y = allocation.y - dock_req.height;

                if (allocation.x + dock_req.width <= monitor.x + monitor.width)
                        x = allocation.x;
                else
                        x = monitor.x + monitor.width - dock_req.width;
        }

        ctk_window_move (CTK_WINDOW (icon->priv->dock), x, y);

        /* Without this, the popup window appears as a square after changing
         * the orientation */
        ctk_window_resize (CTK_WINDOW (icon->priv->dock), 1, 1);

        ctk_widget_show_all (icon->priv->dock);

        /* Grab focus */
        ctk_grab_add (icon->priv->dock);

        display = ctk_widget_get_display (icon->priv->dock);

        do {
                CdkSeat *seat = cdk_display_get_default_seat (display);
                CdkWindow *window = ctk_widget_get_window (icon->priv->dock);

                if (cdk_seat_grab (seat,
                                   window,
                                   CDK_SEAT_CAPABILITY_ALL,
                                   TRUE,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL) != CDK_GRAB_SUCCESS) {
                        ctk_grab_remove (icon->priv->dock);
                        ctk_widget_hide (icon->priv->dock);
                        break;
                }
        } while (0);

        ctk_widget_grab_focus (icon->priv->dock);

        return TRUE;
}

static gboolean
on_applet_icon_button_press (CtkWidget           *applet_icon,
                             CdkEventButton      *event,
                             GvcStreamAppletIcon *icon)
{
        if (event->button == 1) {
                popup_dock (icon, CDK_CURRENT_TIME);
                return TRUE;
        }

        /* Middle click acts as mute/unmute */
        if (event->button == 2) {
                gboolean is_muted = cafe_mixer_stream_control_get_mute (icon->priv->control);

                cafe_mixer_stream_control_set_mute (icon->priv->control, !is_muted);
                return TRUE;
        }
        return FALSE;
}

void
gvc_stream_applet_icon_set_mute (GvcStreamAppletIcon *icon, gboolean mute)
{
        cafe_mixer_stream_control_set_mute (icon->priv->control, mute);
}

gboolean
gvc_stream_applet_icon_get_mute (GvcStreamAppletIcon *icon)
{
        return cafe_mixer_stream_control_get_mute (icon->priv->control);
}

void
gvc_stream_applet_icon_volume_control (GvcStreamAppletIcon *icon)
{
        GError *error = NULL;

        cafe_cdk_spawn_command_line_on_screen (ctk_widget_get_screen (icon->priv->dock),
                                               "cafe-volume-control",
                                               &error);

        if (error != NULL) {
                CtkWidget *dialog;

                dialog = ctk_message_dialog_new (NULL,
                                                 0,
                                                 CTK_MESSAGE_ERROR,
                                                 CTK_BUTTONS_CLOSE,
                                                 _("Failed to start Sound Preferences: %s"),
                                                 error->message);
                g_signal_connect (G_OBJECT (dialog),
                                  "response",
                                  G_CALLBACK (ctk_widget_destroy),
                                  NULL);
                ctk_widget_show (dialog);
                g_error_free (error);
        }
}

static gboolean
on_applet_icon_scroll_event (CtkWidget           *event_box,
                             CdkEventScroll      *event,
                             GvcStreamAppletIcon *icon)
{
        return gvc_channel_bar_scroll (GVC_CHANNEL_BAR (icon->priv->bar), event->direction);
}

static void
gvc_icon_release_grab (GvcStreamAppletIcon *icon, CdkEventButton *event)
{
        CdkDisplay *display = ctk_widget_get_display (icon->priv->dock);
        CdkSeat *seat = cdk_display_get_default_seat (display);
        cdk_seat_ungrab (seat);
        ctk_grab_remove (icon->priv->dock);

        /* Hide again */
        ctk_widget_hide (icon->priv->dock);
}

static gboolean
on_dock_button_press (CtkWidget           *widget,
                      CdkEventButton      *event,
                      GvcStreamAppletIcon *icon)
{
        if (event->type == CDK_BUTTON_PRESS) {
                gvc_icon_release_grab (icon, event);
                return TRUE;
        }

        return FALSE;
}

static void
popdown_dock (GvcStreamAppletIcon *icon)
{
        CdkDisplay *display;

        display = ctk_widget_get_display (icon->priv->dock);

        CdkSeat *seat = cdk_display_get_default_seat (display);
        cdk_seat_ungrab (seat);

        /* Hide again */
        ctk_widget_hide (icon->priv->dock);
}

/* This is called when the grab is broken for either the dock, or the scale */
static void
gvc_icon_grab_notify (GvcStreamAppletIcon *icon, gboolean was_grabbed)
{
        if (was_grabbed != FALSE)
                return;

        if (ctk_widget_has_grab (icon->priv->dock) == FALSE)
                return;

        if (ctk_widget_is_ancestor (ctk_grab_get_current (), icon->priv->dock))
                return;

        popdown_dock (icon);
}

static void
on_dock_grab_notify (CtkWidget           *widget,
                     gboolean             was_grabbed,
                     GvcStreamAppletIcon *icon)
{
        gvc_icon_grab_notify (icon, was_grabbed);
}

static gboolean
on_dock_grab_broken_event (CtkWidget           *widget,
                           gboolean             was_grabbed,
                           GvcStreamAppletIcon *icon)
{
        gvc_icon_grab_notify (icon, FALSE);
        return FALSE;
}

static gboolean
on_dock_key_release (CtkWidget           *widget,
                     CdkEventKey         *event,
                     GvcStreamAppletIcon *icon)
{
        if (event->keyval == CDK_KEY_Escape) {
                popdown_dock (icon);
                return TRUE;
        }
        return TRUE;
}

static gboolean
on_dock_scroll_event (CtkWidget           *widget,
                      CdkEventScroll      *event,
                      GvcStreamAppletIcon *icon)
{
        /* Forward event to the applet icon */
        on_applet_icon_scroll_event (NULL, event, icon);
        return TRUE;
}

static void
gvc_stream_applet_icon_set_icon_from_name (GvcStreamAppletIcon *icon,
                                           const gchar *icon_name)
{
        CtkIconTheme *icon_theme = ctk_icon_theme_get_default ();
        gint icon_scale = ctk_widget_get_scale_factor (CTK_WIDGET (icon));

        cairo_surface_t* surface = ctk_icon_theme_load_surface (icon_theme, icon_name,
                                                                icon->priv->size,
                                                                icon_scale, NULL,
                                                                CTK_ICON_LOOKUP_FORCE_SIZE,
                                                                NULL);

        ctk_image_set_from_surface (CTK_IMAGE (icon->priv->image), surface);
        cairo_surface_destroy (surface);
}

static void
update_icon (GvcStreamAppletIcon *icon)
{
        guint                       volume = 0;
        gdouble                     decibel = 0;
        guint                       normal = 0;
        gboolean                    muted = FALSE;
        guint                       n = 0;
        gchar                      *markup;
        const gchar                *description;
        CafeMixerStreamControlFlags flags;

        if (icon->priv->control == NULL) {
                /* Do not bother creating a tooltip for an unusable icon as it
                 * has no practical use */
                ctk_widget_set_has_tooltip (CTK_WIDGET (icon), FALSE);
                return;
        } else
                ctk_widget_set_has_tooltip (CTK_WIDGET (icon), TRUE);

        flags = cafe_mixer_stream_control_get_flags (icon->priv->control);

        if (flags & CAFE_MIXER_STREAM_CONTROL_MUTE_READABLE)
                muted = cafe_mixer_stream_control_get_mute (icon->priv->control);

        if (flags & CAFE_MIXER_STREAM_CONTROL_VOLUME_READABLE) {
                volume = cafe_mixer_stream_control_get_volume (icon->priv->control);
                normal = cafe_mixer_stream_control_get_normal_volume (icon->priv->control);

                /* Select an icon, they are expected to be sorted, the lowest index being
                 * the mute icon and the rest being volume increments */
                if (volume <= 0 || muted)
                        n = 0;
                else
                        n = CLAMP (3 * volume / normal + 1, 1, 3);
        }
        if (flags & CAFE_MIXER_STREAM_CONTROL_HAS_DECIBEL)
                decibel = cafe_mixer_stream_control_get_decibel (icon->priv->control);

        /* Apparently applet icon will reset icon even if it doesn't change */
        if (icon->priv->current_icon != n) {
                gvc_stream_applet_icon_set_icon_from_name (icon, icon->priv->icon_names[n]);
                icon->priv->current_icon = n;
        }

        description = cafe_mixer_stream_control_get_label (icon->priv->control);

        guint volume_percent = (guint) round (100.0 * volume / normal);
        if (muted) {
                markup = g_strdup_printf ("<b>%s: %s %u%%</b>\n<small>%s</small>",
                                          icon->priv->display_name,
                                          _("Muted at"),
                                          volume_percent,
                                          description);
        } else if (flags & CAFE_MIXER_STREAM_CONTROL_VOLUME_READABLE) {
                if (flags & CAFE_MIXER_STREAM_CONTROL_HAS_DECIBEL) {
                        if (decibel > -CAFE_MIXER_INFINITY) {
                                markup = g_strdup_printf ("<b>%s: %u%%</b>\n"
                                                          "<small>%0.2f dB\n%s</small>",
                                                          icon->priv->display_name,
                                                          volume_percent,
                                                          decibel,
                                                          description);
                        } else {
                                markup = g_strdup_printf ("<b>%s: %u%%</b>\n"
                                                          "<small>-&#8734; dB\n%s</small>",
                                                          icon->priv->display_name,
                                                          volume_percent,
                                                          description);
                        }
                } else {
                        markup = g_strdup_printf ("<b>%s: %u%%</b>\n<small>%s</small>",
                                                  icon->priv->display_name,
                                                  volume_percent,
                                                  description);
                }
        } else {
                markup = g_strdup_printf ("<b>%s</b>\n<small>%s</small>",
                                          icon->priv->display_name,
                                          description);
        }

        ctk_widget_set_tooltip_markup (CTK_WIDGET (icon), markup);

        g_free (markup);
}

void
gvc_stream_applet_icon_set_size (GvcStreamAppletIcon *icon,
                                 guint                size)
{

        /*Iterate through the icon sizes so they can be kept sharp*/
        if (size < 22)
                size = 16;
        else if (size < 24)
                size = 22;
        else if (size < 32)
                size = 24;
        else if (size < 48)
                size = 32;

        icon->priv->size = size;
        gvc_stream_applet_icon_set_icon_from_name (icon, icon->priv->icon_names[icon->priv->current_icon]);
}

void
gvc_stream_applet_icon_set_orient (GvcStreamAppletIcon  *icon,
                                   CafePanelAppletOrient orient)
{
        /* Sometimes orient does not get properly defined especially on a bottom panel.
         * Use the applet orientation if it is valid, otherwise set a vertical slider,
         * otherwise bottom panels get a horizontal slider.
         */
        if (orient)
                icon->priv->orient = orient;
        else
                icon->priv->orient = CAFE_PANEL_APPLET_ORIENT_DOWN;
}

void
gvc_stream_applet_icon_set_icon_names (GvcStreamAppletIcon  *icon,
                                       const gchar         **names)
{
        g_return_if_fail (GVC_IS_STREAM_APPLET_ICON (icon));
        g_return_if_fail (names != NULL && *names != NULL);

        if (G_UNLIKELY (g_strv_length ((gchar **) names) != 4)) {
                g_warn_if_reached ();
                return;
        }

        g_strfreev (icon->priv->icon_names);

        icon->priv->icon_names = g_strdupv ((gchar **) names);

        /* Set the first icon as the initial one, the icon may be immediately
         * updated or not depending on whether a stream is available */
        gvc_stream_applet_icon_set_icon_from_name (icon, names[0]);
        update_icon (icon);

        g_object_notify_by_pspec (G_OBJECT (icon), properties[PROP_ICON_NAMES]);
}

static void
on_stream_control_volume_notify (CafeMixerStreamControl *control,
                                 GParamSpec             *pspec,
                                 GvcStreamAppletIcon    *icon)
{
        update_icon (icon);
}

static void
on_stream_control_mute_notify (CafeMixerStreamControl *control,
                               GParamSpec             *pspec,
                               GvcStreamAppletIcon    *icon)
{
        update_icon (icon);
}

void
gvc_stream_applet_icon_set_display_name (GvcStreamAppletIcon *icon,
                                         const gchar         *name)
{
        g_return_if_fail (GVC_STREAM_APPLET_ICON (icon));

        g_free (icon->priv->display_name);

        icon->priv->display_name = g_strdup (name);
        update_icon (icon);

        g_object_notify_by_pspec (G_OBJECT (icon), properties[PROP_DISPLAY_NAME]);
}

void
gvc_stream_applet_icon_set_control (GvcStreamAppletIcon    *icon,
                                    CafeMixerStreamControl *control)
{
        g_return_if_fail (GVC_STREAM_APPLET_ICON (icon));

        if (icon->priv->control == control)
                return;

        if (control != NULL)
                g_object_ref (control);

        if (icon->priv->control != NULL) {
                g_signal_handlers_disconnect_by_func (G_OBJECT (icon->priv->control),
                                                      G_CALLBACK (on_stream_control_volume_notify),
                                                      icon);
                g_signal_handlers_disconnect_by_func (G_OBJECT (icon->priv->control),
                                                      G_CALLBACK (on_stream_control_mute_notify),
                                                      icon);

                g_object_unref (icon->priv->control);
        }

        icon->priv->control = control;

        if (icon->priv->control != NULL) {
                g_signal_connect (G_OBJECT (icon->priv->control),
                                  "notify::volume",
                                  G_CALLBACK (on_stream_control_volume_notify),
                                  icon);
                g_signal_connect (G_OBJECT (icon->priv->control),
                                  "notify::mute",
                                  G_CALLBACK (on_stream_control_mute_notify),
                                  icon);

                // XXX when no stream set some default icon and "unset" dock
                update_icon (icon);
        }

        gvc_channel_bar_set_control (GVC_CHANNEL_BAR (icon->priv->bar), icon->priv->control);

        g_object_notify_by_pspec (G_OBJECT (icon), properties[PROP_CONTROL]);
}

static void
gvc_stream_applet_icon_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
        GvcStreamAppletIcon *self = GVC_STREAM_APPLET_ICON (object);

        switch (prop_id) {
        case PROP_CONTROL:
                gvc_stream_applet_icon_set_control (self, g_value_get_object (value));
                break;
        case PROP_DISPLAY_NAME:
                gvc_stream_applet_icon_set_display_name (self, g_value_get_string (value));
                break;
        case PROP_ICON_NAMES:
                gvc_stream_applet_icon_set_icon_names (self, g_value_get_boxed (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_stream_applet_icon_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
        GvcStreamAppletIcon *self = GVC_STREAM_APPLET_ICON (object);

        switch (prop_id) {
        case PROP_CONTROL:
                g_value_set_object (value, self->priv->control);
                break;
        case PROP_DISPLAY_NAME:
                g_value_set_string (value, self->priv->display_name);
                break;
        case PROP_ICON_NAMES:
                g_value_set_boxed (value, self->priv->icon_names);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_stream_applet_icon_dispose (GObject *object)
{
        GvcStreamAppletIcon *icon = GVC_STREAM_APPLET_ICON (object);

        if (icon->priv->dock != NULL) {
                ctk_widget_destroy (icon->priv->dock);
                icon->priv->dock = NULL;
        }

        g_clear_object (&icon->priv->control);

        G_OBJECT_CLASS (gvc_stream_applet_icon_parent_class)->dispose (object);
}

static void
gvc_stream_applet_icon_class_init (GvcStreamAppletIconClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        CtkWidgetClass *widget_class = (CtkWidgetClass *) klass;

        object_class->finalize     = gvc_stream_applet_icon_finalize;
        object_class->dispose      = gvc_stream_applet_icon_dispose;
        object_class->set_property = gvc_stream_applet_icon_set_property;
        object_class->get_property = gvc_stream_applet_icon_get_property;

        properties[PROP_CONTROL] =
                g_param_spec_object ("control",
                                     "Control",
                                     "CafeMixer stream control",
                                     CAFE_MIXER_TYPE_STREAM_CONTROL,
                                     G_PARAM_READWRITE |
                                     G_PARAM_CONSTRUCT |
                                     G_PARAM_STATIC_STRINGS);

        properties[PROP_DISPLAY_NAME] =
                g_param_spec_string ("display-name",
                                     "Display name",
                                     "Name to display for this stream",
                                     NULL,
                                     G_PARAM_READWRITE |
                                     G_PARAM_CONSTRUCT |
                                     G_PARAM_STATIC_STRINGS);

        properties[PROP_ICON_NAMES] =
                g_param_spec_boxed ("icon-names",
                                    "Icon names",
                                    "Name of icon to display for this stream",
                                    G_TYPE_STRV,
                                    G_PARAM_READWRITE |
                                    G_PARAM_CONSTRUCT |
                                    G_PARAM_STATIC_STRINGS);

        ctk_widget_class_set_css_name (widget_class, "volume-applet");

        g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

static void
on_applet_icon_visible_notify (GvcStreamAppletIcon *icon)
{
        if (ctk_widget_get_visible (CTK_WIDGET (icon)) == FALSE)
                ctk_widget_hide (icon->priv->dock);
}

static void
on_icon_theme_change (CtkSettings         *settings,
                      GParamSpec          *pspec,
                      GvcStreamAppletIcon *icon)
{
        gvc_stream_applet_icon_set_icon_from_name (icon, icon->priv->icon_names[icon->priv->current_icon]);
}

static void
gvc_stream_applet_icon_init (GvcStreamAppletIcon *icon)
{
        CtkWidget *frame;
        CtkWidget *box;

        icon->priv = gvc_stream_applet_icon_get_instance_private (icon);

        icon->priv->image = CTK_IMAGE (ctk_image_new ());
        ctk_container_add (CTK_CONTAINER (icon), CTK_WIDGET (icon->priv->image));

        g_signal_connect (CTK_WIDGET (icon),
                          "button-press-event",
                          G_CALLBACK (on_applet_icon_button_press),
                          icon);
        g_signal_connect (CTK_WIDGET (icon),
                          "scroll-event",
                          G_CALLBACK (on_applet_icon_scroll_event),
                          icon);
        g_signal_connect (CTK_WIDGET (icon),
                          "notify::visible",
                          G_CALLBACK (on_applet_icon_visible_notify),
                          NULL);

        /* Create the dock window */
        icon->priv->dock = ctk_window_new (CTK_WINDOW_POPUP);

        ctk_window_set_decorated (CTK_WINDOW (icon->priv->dock), FALSE);

        g_signal_connect (G_OBJECT (icon->priv->dock),
                          "button-press-event",
                          G_CALLBACK (on_dock_button_press),
                          icon);
        g_signal_connect (G_OBJECT (icon->priv->dock),
                          "key-release-event",
                          G_CALLBACK (on_dock_key_release),
                          icon);
        g_signal_connect (G_OBJECT (icon->priv->dock),
                          "scroll-event",
                          G_CALLBACK (on_dock_scroll_event),
                          icon);
        g_signal_connect (G_OBJECT (icon->priv->dock),
                          "grab-notify",
                          G_CALLBACK (on_dock_grab_notify),
                          icon);
        g_signal_connect (G_OBJECT (icon->priv->dock),
                          "grab-broken-event",
                          G_CALLBACK (on_dock_grab_broken_event),
                          icon);

        frame = ctk_frame_new (NULL);
        ctk_frame_set_shadow_type (CTK_FRAME (frame), CTK_SHADOW_OUT);
        ctk_container_add (CTK_CONTAINER (icon->priv->dock), frame);

        icon->priv->bar = gvc_channel_bar_new (NULL);

        gvc_channel_bar_set_orientation (GVC_CHANNEL_BAR (icon->priv->bar),
                                         CTK_ORIENTATION_VERTICAL);

        /* Set volume control frame, slider and toplevel window to follow panel theme */
        CtkWidget *toplevel = ctk_widget_get_toplevel (icon->priv->dock);
        CtkStyleContext *context;
        context = ctk_widget_get_style_context (CTK_WIDGET(toplevel));
        ctk_style_context_add_class(context,"cafe-panel-applet-slider");

        /* Make transparency possible in ctk3 theme */
        CdkScreen *screen = ctk_widget_get_screen(CTK_WIDGET(toplevel));
        CdkVisual *visual = cdk_screen_get_rgba_visual(screen);
        ctk_widget_set_visual(CTK_WIDGET(toplevel), visual);

        box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 6);

        ctk_container_set_border_width (CTK_CONTAINER (box), 2);
        ctk_container_add (CTK_CONTAINER (frame), box);

        ctk_box_pack_start (CTK_BOX (box), icon->priv->bar, TRUE, FALSE, 0);

        g_signal_connect (ctk_settings_get_default (),
                          "notify::ctk-icon-theme-name",
                          G_CALLBACK (on_icon_theme_change),
                          icon);
}

static void
gvc_stream_applet_icon_finalize (GObject *object)
{
        GvcStreamAppletIcon *icon;

        icon = GVC_STREAM_APPLET_ICON (object);

        g_strfreev (icon->priv->icon_names);

        g_signal_handlers_disconnect_by_func (ctk_settings_get_default (),
                                              on_icon_theme_change,
                                              icon);

        G_OBJECT_CLASS (gvc_stream_applet_icon_parent_class)->finalize (object);
}

GvcStreamAppletIcon *
gvc_stream_applet_icon_new (CafeMixerStreamControl *control,
                            const gchar           **icon_names)
{
        return g_object_new (GVC_TYPE_STREAM_APPLET_ICON,
                             "control", control,
                             "icon-names", icon_names,
                             NULL);
}
