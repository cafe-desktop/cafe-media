/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
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

#include "config.h"

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <ctk/ctk.h>

#include <libcafemixer/cafemixer.h>
#include <cafe-panel-applet.h>

#include "gvc-applet.h"
#include "gvc-stream-applet-icon.h"

static const gchar *icon_names_output[] = {
        "audio-volume-muted",
        "audio-volume-low",
        "audio-volume-medium",
        "audio-volume-high",
        NULL
};

static const gchar *icon_names_input[] = {
        "audio-input-microphone-muted",
        "audio-input-microphone-low",
        "audio-input-microphone-medium",
        "audio-input-microphone-high",
        NULL
};

static void menu_output_mute (CtkAction *action, GvcApplet *applet);
static void menu_activate_open_volume_control (CtkAction *action, GvcApplet *applet);
static const CtkActionEntry applet_menu_actions [] = {
        { "Preferences", APPLET_ICON, N_("_Sound Preferences"), NULL, NULL, G_CALLBACK(menu_activate_open_volume_control) },
        { "MuteOutput", "audio-volume-muted", N_("Mute Output"), NULL, NULL, G_CALLBACK (menu_output_mute) }
};

static char *ui = "<menuitem name='Preferences' action='Preferences' />"
                  "<menuitem name='MuteOutput' action='MuteOutput' />";

struct _GvcAppletPrivate
{
        GvcStreamAppletIcon *icon_input;
        GvcStreamAppletIcon *icon_output;
        gboolean             running;
        CafeMixerContext    *context;
        CafeMixerStream     *output;
        CafeMixerStream     *input;

        CafePanelApplet     *applet;
        CtkBox              *box;
        CtkActionGroup      *action_group;
};

G_DEFINE_TYPE_WITH_PRIVATE (GvcApplet, gvc_applet, G_TYPE_OBJECT)

static void
update_icon_input (GvcApplet *applet)
{
        CafeMixerStreamControl *control = NULL;
        gboolean                show = FALSE;

        /* Enable the input icon in case there is an input stream present and there
         * is a non-mixer application using the input */
        if (applet->priv->input != NULL) {
                const gchar *app_id;
                const GList *inputs = cafe_mixer_stream_list_controls (applet->priv->input);

                control = cafe_mixer_stream_get_default_control (applet->priv->input);

                const gchar *stream_name = cafe_mixer_stream_get_name (applet->priv->input);
                g_debug ("Got stream name %s", stream_name);
                if (g_str_has_suffix (stream_name, ".monitor")) {
                        inputs = NULL;
                        g_debug ("Stream is a monitor, ignoring");
                }

                while (inputs != NULL) {
                        CafeMixerStreamControl *input = CAFE_MIXER_STREAM_CONTROL (inputs->data);
                        CafeMixerStreamControlRole role = cafe_mixer_stream_control_get_role (input);

                        if (role == CAFE_MIXER_STREAM_CONTROL_ROLE_APPLICATION) {
                                CafeMixerAppInfo *app_info = cafe_mixer_stream_control_get_app_info (input);

                                app_id = cafe_mixer_app_info_get_id (app_info);
                                if (app_id == NULL) {
                                        /* A recording application which has no
                                         * identifier set */
                                        g_debug ("Found a recording application control %s",
                                                 cafe_mixer_stream_control_get_label (input));

                                        if (G_UNLIKELY (control == NULL)) {
                                                /* In the unlikely case when there is no
                                                 * default input control, use the application
                                                 * control for the icon */
                                                control = input;
                                        }
                                        show = TRUE;
                                        break;
                                }

                                if (strcmp (app_id, "org.cafe.VolumeControl") != 0 &&
                                    strcmp (app_id, "org.gnome.VolumeControl") != 0 &&
                                    strcmp (app_id, "org.PulseAudio.pavucontrol") != 0) {
                                        g_debug ("Found a recording application %s", app_id);

                                        if (G_UNLIKELY (control == NULL))
                                                control = input;

                                        show = TRUE;
                                        break;
                                }
                        }
                        inputs = inputs->next;
                }

                if (show == TRUE)
                        g_debug ("Input icon enabled");
                else
                        g_debug ("There is no recording application, input icon disabled");
        }

        gvc_stream_applet_icon_set_control (applet->priv->icon_input, control);

        ctk_widget_set_visible (CTK_WIDGET (applet->priv->icon_input), show);
}

static void
update_icon_output (GvcApplet *applet)
{
        CafeMixerStream        *stream;
        CafeMixerStreamControl *control = NULL;

        stream = cafe_mixer_context_get_default_output_stream (applet->priv->context);
        if (stream != NULL)
                control = cafe_mixer_stream_get_default_control (stream);

        gvc_stream_applet_icon_set_control (applet->priv->icon_output, control);

        if (control != NULL) {
                g_debug ("Output icon enabled");
                ctk_widget_set_visible (CTK_WIDGET (applet->priv->icon_output), TRUE);
        }
        else {
                g_debug ("There is no output stream/control, output icon disabled");
                ctk_widget_set_visible (CTK_WIDGET (applet->priv->icon_output), FALSE);
        }
}

static void
on_output_stream_control_added (CafeMixerStream *stream,
                                const gchar     *name,
                                GvcApplet       *applet)
{
        CafeMixerStreamControl *control;

        control = cafe_mixer_stream_get_control (stream, name);
        if (G_LIKELY (control != NULL)) {
                CafeMixerStreamControlRole role = cafe_mixer_stream_control_get_role (control);

                /* Non-application output control doesn't affect the icon */
                if (role != CAFE_MIXER_STREAM_CONTROL_ROLE_APPLICATION)
                        return;
        }

        /* Either an application control has been added or we couldn't
         * read the control, this shouldn't happen but let's revalidate the
         * icon to be sure if it does */
        update_icon_output (applet);
}

static void
on_input_stream_control_added (CafeMixerStream *stream,
                               const gchar     *name,
                               GvcApplet       *applet)
{
        CafeMixerStreamControl *control;

        control = cafe_mixer_stream_get_control (stream, name);
        if (G_LIKELY (control != NULL)) {
                CafeMixerStreamControlRole role = cafe_mixer_stream_control_get_role (control);

                /* Non-application input control doesn't affect the icon */
                if (role != CAFE_MIXER_STREAM_CONTROL_ROLE_APPLICATION)
                        return;
        }

        /* Either an application control has been added or we couldn't
         * read the control, this shouldn't happen but let's revalidate the
         * icon to be sure if it does */
        update_icon_input (applet);
}

static void
on_output_stream_control_removed (CafeMixerStream *stream,
                                  const gchar     *name,
                                  GvcApplet       *applet)
{
        /* The removed stream could be an application output, which may cause
         * the output applet icon to disappear */
        update_icon_output (applet);
}

static void
on_input_stream_control_removed (CafeMixerStream *stream,
                                 const gchar     *name,
                                 GvcApplet       *applet)
{
        /* The removed stream could be an application input, which may cause
         * the input applet icon to disappear */
        update_icon_input (applet);
}

static gboolean
update_default_output_stream (GvcApplet *applet)
{
        CafeMixerStream *stream;

        stream = cafe_mixer_context_get_default_output_stream (applet->priv->context);
        if (stream == applet->priv->output)
                return FALSE;

        /* The output stream has changed */
        if (applet->priv->output != NULL) {
                g_signal_handlers_disconnect_by_data (G_OBJECT (applet->priv->output), applet);
                g_object_unref (applet->priv->output);
        }

        applet->priv->output = (stream == NULL) ? NULL : g_object_ref (stream);
        if (applet->priv->output != NULL) {
                g_signal_connect (G_OBJECT (applet->priv->output),
                                  "control-added",
                                  G_CALLBACK (on_output_stream_control_added),
                                  applet);
                g_signal_connect (G_OBJECT (applet->priv->output),
                                  "control-removed",
                                  G_CALLBACK (on_output_stream_control_removed),
                                  applet);
        }

        /* Return TRUE if the default output stream has changed */
        return TRUE;
}

static gboolean
update_default_input_stream (GvcApplet *applet)
{
        CafeMixerStream *stream;

        stream = cafe_mixer_context_get_default_input_stream (applet->priv->context);
        if (stream == applet->priv->input)
                return FALSE;

        /* The input stream has changed */
        if (applet->priv->input != NULL) {
                g_signal_handlers_disconnect_by_data (G_OBJECT (applet->priv->input), applet);
                g_object_unref (applet->priv->input);
        }

        applet->priv->input = (stream == NULL) ? NULL : g_object_ref (stream);
        if (applet->priv->input != NULL) {
                g_signal_connect (G_OBJECT (applet->priv->input),
                                  "control-added",
                                  G_CALLBACK (on_input_stream_control_added),
                                  applet);
                g_signal_connect (G_OBJECT (applet->priv->input),
                                  "control-removed",
                                  G_CALLBACK (on_input_stream_control_removed),
                                  applet);
        }

        /* Return TRUE if the default input stream has changed */
        return TRUE;
}

static void
on_context_state_notify (CafeMixerContext *context,
                         GParamSpec       *pspec,
                         GvcApplet        *applet)
{
        CafeMixerState state = cafe_mixer_context_get_state (context);

        switch (state) {
        case CAFE_MIXER_STATE_FAILED:
                g_warning ("Failed to connect to a sound system");
                break;

        case CAFE_MIXER_STATE_READY:
                update_default_output_stream (applet);
                update_default_input_stream (applet);

                /* Each applet change may affect the visibility of the icons */
                update_icon_output (applet);
                update_icon_input (applet);
                break;
        default:
                break;
        }
}

static void
on_context_default_input_stream_notify (CafeMixerContext *context,
                                        GParamSpec       *pspec,
                                        GvcApplet        *applet)
{
        if (update_default_input_stream (applet) == FALSE)
                return;

        update_icon_input (applet);
}

static void
on_context_default_output_stream_notify (CafeMixerContext *control,
                                         GParamSpec       *pspec,
                                         GvcApplet        *applet)
{
        if (update_default_output_stream (applet) == FALSE)
                return;

        update_icon_output (applet);
}

void
gvc_applet_start (GvcApplet *applet)
{
        g_return_if_fail (GVC_IS_APPLET (applet));

        if (G_UNLIKELY (applet->priv->running == TRUE))
                return;

        if (G_UNLIKELY (cafe_mixer_context_open (applet->priv->context) == FALSE)) {
                /* Normally this should never happen, in the worst case we
                 * should end up with the Null module */
                g_warning ("Failed to connect to a sound system");
        }

        g_debug ("Applet has been started");

        applet->priv->running = TRUE;
}

static void
gvc_applet_dispose (GObject *object)
{
        GvcApplet *applet = GVC_APPLET (object);

        if (applet->priv->output != NULL) {
                g_signal_handlers_disconnect_by_data (G_OBJECT (applet->priv->output), applet);
                g_clear_object (&applet->priv->output);
        }
        if (applet->priv->input != NULL) {
                g_signal_handlers_disconnect_by_data (G_OBJECT (applet->priv->input), applet);
                g_clear_object (&applet->priv->input);
        }

        g_clear_object (&applet->priv->context);
        g_clear_object (&applet->priv->icon_input);
        g_clear_object (&applet->priv->icon_output);

        G_OBJECT_CLASS (gvc_applet_parent_class)->dispose (object);
}

static void
gvc_applet_class_init (GvcAppletClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->dispose = gvc_applet_dispose;
}

static void
gvc_applet_init (GvcApplet *applet)
{
        applet->priv = gvc_applet_get_instance_private (applet);

        applet->priv->icon_input  = gvc_stream_applet_icon_new (NULL, icon_names_input);
        applet->priv->icon_output = gvc_stream_applet_icon_new (NULL, icon_names_output);

        gvc_stream_applet_icon_set_display_name (applet->priv->icon_input,  _("Input"));
        gvc_stream_applet_icon_set_display_name (applet->priv->icon_output, _("Output"));

        applet->priv->context = cafe_mixer_context_new ();

        cafe_mixer_context_set_app_name (applet->priv->context, _("CAFE Volume Control Applet"));

        cafe_mixer_context_set_app_id (applet->priv->context, GVC_APPLET_DBUS_NAME);
        cafe_mixer_context_set_app_version (applet->priv->context, VERSION);
        cafe_mixer_context_set_app_icon (applet->priv->context, APPLET_ICON);

        g_signal_connect (G_OBJECT (applet->priv->context),
                          "notify::state",
                          G_CALLBACK (on_context_state_notify),
                          applet);
        g_signal_connect (G_OBJECT (applet->priv->context),
                          "notify::default-input-stream",
                          G_CALLBACK (on_context_default_input_stream_notify),
                          applet);
        g_signal_connect (G_OBJECT (applet->priv->context),
                          "notify::default-output-stream",
                          G_CALLBACK (on_context_default_output_stream_notify),
                          applet);
}

GvcApplet *
gvc_applet_new (void)
{
        return g_object_new (GVC_TYPE_APPLET, NULL);
}

static void
gvc_applet_set_size(CtkWidget* widget, int size, gpointer user_data)
{
        GvcApplet *applet = user_data;

        /*Iterate through the icon sizes so they can be kept sharp*/
        if (size < 22)
                size = 16;
        else if (size < 24)
                size = 22;
        else if (size < 32)
                size = 24;
        else if (size < 48)
                size = 32;

        gvc_stream_applet_icon_set_size (applet->priv->icon_input, size);
        gvc_stream_applet_icon_set_size (applet->priv->icon_output, size);
}

static void
gvc_applet_set_mute (CtkWidget* widget, int size, gpointer user_data)
{
        GvcApplet *applet = user_data;
        gboolean is_muted;
        CtkAction *action;

        is_muted = gvc_stream_applet_icon_get_mute (applet->priv->icon_output);

        action = ctk_action_group_get_action (applet->priv->action_group, "MuteOutput");

        if (is_muted) {
                ctk_action_set_label (action, _("Unmute Output"));
                ctk_action_set_icon_name (action, "audio-volume-medium");
        }
        else
        {
                ctk_action_set_label (action, _("Mute Output"));
                ctk_action_set_icon_name (action, "audio-volume-muted");
        }
}

static void
gvc_applet_set_orient(CtkWidget *widget, CafePanelAppletOrient orient, gpointer user_data)
{
        GvcApplet *applet = user_data;

        gvc_stream_applet_icon_set_orient (applet->priv->icon_input, orient);
        gvc_stream_applet_icon_set_orient (applet->priv->icon_output, orient);
}

static void
menu_output_mute (CtkAction *action, GvcApplet *applet)
{
        gboolean               is_muted;

        is_muted = gvc_stream_applet_icon_get_mute(applet->priv->icon_output);
        if (!is_muted) {
                gvc_stream_applet_icon_set_mute (applet->priv->icon_output, TRUE);
                ctk_action_set_label (action, "Unmute Output");
                ctk_action_set_icon_name( action, "audio-volume-medium");

        }
        else {
                gvc_stream_applet_icon_set_mute (applet->priv->icon_output, FALSE);
                ctk_action_set_label (action, "Mute Output");
                ctk_action_set_icon_name (action, "audio-volume-muted");
       }
}

static void
menu_activate_open_volume_control (CtkAction *action, GvcApplet *applet)
{
        gvc_stream_applet_icon_volume_control (applet->priv->icon_output);
}

gboolean
gvc_applet_fill (GvcApplet *applet, CafePanelApplet* applet_widget)
{
        CdkEventMask    event_mask;
        CdkWindow      *window;

        g_set_application_name (_("Volume Control Applet"));
        ctk_window_set_default_icon_name (APPLET_ICON);

        cafe_panel_applet_set_flags (applet_widget, CAFE_PANEL_APPLET_EXPAND_MINOR);
        cafe_panel_applet_set_background_widget (CAFE_PANEL_APPLET (applet_widget), CTK_WIDGET (applet_widget));

        applet->priv->applet = applet_widget;
        /*FIXME: We haved to set this up BEFORE packing in icons. find a way to update this when the applet is moved that works*/
        switch (cafe_panel_applet_get_orient (applet->priv->applet)) {
        case CAFE_PANEL_APPLET_ORIENT_UP:
                applet->priv->box = CTK_BOX (ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 0));
		break;
        case CAFE_PANEL_APPLET_ORIENT_DOWN:
                applet->priv->box = CTK_BOX (ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 0));
		break;
        case CAFE_PANEL_APPLET_ORIENT_LEFT:
                applet->priv->box = CTK_BOX (ctk_box_new (CTK_ORIENTATION_VERTICAL, 0));
		break;
        case CAFE_PANEL_APPLET_ORIENT_RIGHT:
                applet->priv->box = CTK_BOX (ctk_box_new (CTK_ORIENTATION_VERTICAL, 0));
        break;
        }

        /* Define an initial size and orientation */
        gvc_stream_applet_icon_set_size (applet->priv->icon_input, cafe_panel_applet_get_size (applet->priv->applet));
        gvc_stream_applet_icon_set_size (applet->priv->icon_output, cafe_panel_applet_get_size (applet->priv->applet));
        gvc_stream_applet_icon_set_orient (applet->priv->icon_input, cafe_panel_applet_get_orient (applet->priv->applet));
        gvc_stream_applet_icon_set_orient (applet->priv->icon_output, cafe_panel_applet_get_orient (applet->priv->applet));

        /* we add the Ctk buttons into the applet */
        ctk_box_pack_start (applet->priv->box, CTK_WIDGET (applet->priv->icon_input), TRUE, TRUE, 2);
        ctk_box_pack_start (applet->priv->box, CTK_WIDGET (applet->priv->icon_output), TRUE, TRUE, 2);
        ctk_container_add (CTK_CONTAINER (applet->priv->applet), CTK_WIDGET (applet->priv->box));
        ctk_widget_show_all (CTK_WIDGET (applet->priv->applet));

        /* Enable 'scroll-event' signal to get through */
        window = ctk_widget_get_window (CTK_WIDGET (applet->priv->icon_input));
        event_mask = cdk_window_get_events (window);
        cdk_window_set_events (window, event_mask | CDK_SCROLL_MASK);

        window = ctk_widget_get_window (CTK_WIDGET (applet->priv->icon_output));
        event_mask = cdk_window_get_events (window);
        cdk_window_set_events (window, event_mask | CDK_SCROLL_MASK);

        /* Update icons on size/orientation changes */
        g_object_connect (applet->priv->applet,
                         "signal::change_size", gvc_applet_set_size, applet,
                         "signal::change_orient", gvc_applet_set_orient, applet,
                         "signal::event-after", gvc_applet_set_mute, applet,
                         NULL);

        /* set up context menu */
        applet->priv->action_group = ctk_action_group_new ("Volume Control Applet Actions");
        ctk_action_group_set_translation_domain (applet->priv->action_group, GETTEXT_PACKAGE);
        ctk_action_group_add_actions (applet->priv->action_group, applet_menu_actions,
                                      G_N_ELEMENTS (applet_menu_actions), applet);

        cafe_panel_applet_setup_menu (applet->priv->applet, ui, applet->priv->action_group);

        return TRUE;
}
