/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann
 * Copyright (C) 2014 Michal Ratajsky <michal.ratajsky@gmail.com>
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <ctk/ctk.h>
#include <libcafemixer/cafemixer.h>

#include "gvc-channel-bar.h"
#include "gvc-balance-bar.h"
#include "gvc-combo-box.h"
#include "gvc-mixer-dialog.h"
#include "gvc-sound-theme-chooser.h"
#include "gvc-level-bar.h"
#include "gvc-speaker-test.h"
#include "gvc-utils.h"

struct _GvcMixerDialogPrivate
{
        CafeMixerContext *context;
        CafeMixerBackendFlags backend_flags;
        GHashTable       *bars;
        CtkWidget        *notebook;
        CtkWidget        *output_bar;
        CtkWidget        *input_bar;
        CtkWidget        *input_level_bar;
        CtkWidget        *effects_bar;
        CtkWidget        *output_stream_box;
        CtkWidget        *hw_box;
        CtkWidget        *hw_treeview;
        CtkWidget        *hw_settings_box;
        CtkWidget        *hw_profile_combo;
        CtkWidget        *input_box;
        CtkWidget        *output_box;
        CtkWidget        *applications_box;
        CtkWidget        *applications_window;
        CtkWidget        *no_apps_label;
        CtkWidget        *output_treeview;
        CtkWidget        *output_settings_frame;
        CtkWidget        *output_settings_box;
        CtkWidget        *output_balance_bar;
        CtkWidget        *output_fade_bar;
        CtkWidget        *output_lfe_bar;
        CtkWidget        *output_port_combo;
        CtkWidget        *input_treeview;
        CtkWidget        *input_port_combo;
        CtkWidget        *input_settings_box;
        CtkSizeGroup     *size_group;
        gdouble           last_input_peak;
        guint             num_apps;
};

enum {
        ICON_COLUMN,
        NAME_COLUMN,
        LABEL_COLUMN,
        ACTIVE_COLUMN,
        SPEAKERS_COLUMN,
        NUM_COLUMNS
};

enum {
        HW_ICON_COLUMN,
        HW_NAME_COLUMN,
        HW_LABEL_COLUMN,
        HW_STATUS_COLUMN,
        HW_PROFILE_COLUMN,
        HW_NUM_COLUMNS
};

enum {
        PAGE_EFFECTS,
        PAGE_HARDWARE,
        PAGE_INPUT,
        PAGE_OUTPUT,
        PAGE_APPLICATIONS
};

enum {
        PROP_0,
        PROP_CONTEXT
};

static const guint tab_accel_keys[] = {
        GDK_KEY_1, GDK_KEY_2, GDK_KEY_3, GDK_KEY_4, GDK_KEY_5
};

static void gvc_mixer_dialog_finalize   (GObject                *object);

static void add_stream                  (GvcMixerDialog         *dialog,
                                         CafeMixerStream        *stream);
static void add_application_control     (GvcMixerDialog         *dialog,
                                         CafeMixerStreamControl *control);

static void remove_stream               (GvcMixerDialog         *dialog,
                                         const gchar            *name);
static void remove_application_control  (GvcMixerDialog         *dialog,
                                         const gchar            *name);

static void bar_set_stream              (GvcMixerDialog         *dialog,
                                         CtkWidget              *bar,
                                         CafeMixerStream        *stream);
static void bar_set_stream_control      (GvcMixerDialog         *dialog,
                                         CtkWidget              *bar,
                                         CafeMixerStreamControl *control);

static gboolean dialog_page_scroll_event_cb (CtkWidget          *widget,
                                             GdkEventScroll     *event,
                                             CtkWindow          *window);

G_DEFINE_TYPE_WITH_PRIVATE (GvcMixerDialog, gvc_mixer_dialog, CTK_TYPE_DIALOG)

static CafeMixerSwitch *
find_stream_port_switch (CafeMixerStream *stream)
{
        const GList *switches;

        switches = cafe_mixer_stream_list_switches (stream);
        while (switches != NULL) {
                CafeMixerStreamSwitch *swtch = CAFE_MIXER_STREAM_SWITCH (switches->data);

                if (!CAFE_MIXER_IS_STREAM_TOGGLE (swtch) &&
                    cafe_mixer_stream_switch_get_role (swtch) == CAFE_MIXER_STREAM_SWITCH_ROLE_PORT)
                    return CAFE_MIXER_SWITCH (swtch);

                switches = switches->next;
        }
        return NULL;
}

static CafeMixerSwitch *
find_device_profile_switch (CafeMixerDevice *device)
{
        const GList *switches;

        switches = cafe_mixer_device_list_switches (device);
        while (switches != NULL) {
                CafeMixerDeviceSwitch *swtch = CAFE_MIXER_DEVICE_SWITCH (switches->data);

                if (cafe_mixer_device_switch_get_role (swtch) == CAFE_MIXER_DEVICE_SWITCH_ROLE_PROFILE)
                        return CAFE_MIXER_SWITCH (swtch);

                switches = switches->next;
        }
        return NULL;
}

static CafeMixerStream *
find_device_test_stream (GvcMixerDialog *dialog, CafeMixerDevice *device)
{
        const GList *streams;

        streams = cafe_mixer_device_list_streams (device);
        while (streams != NULL) {
                CafeMixerStream   *stream;
                CafeMixerDirection direction;

                stream = CAFE_MIXER_STREAM (streams->data);
                direction = cafe_mixer_stream_get_direction (stream);

                if (direction == CAFE_MIXER_DIRECTION_OUTPUT) {
                    CafeMixerStreamControl *control;

                    control = cafe_mixer_stream_get_default_control (stream);
                    if (cafe_mixer_stream_control_get_num_channels (control) > 0)
                        return stream;
                }
                streams = streams->next;
        }
        return FALSE;
}

static gboolean
find_tree_item_by_name (CtkTreeModel *model,
                        const gchar  *name,
                        guint         column,
                        CtkTreeIter  *iter)
{
        gboolean found = FALSE;

        if (!ctk_tree_model_get_iter_first (model, iter))
                return FALSE;

        do {
                gchar *n;
                ctk_tree_model_get (model, iter, column, &n, -1);

                if (!g_strcmp0 (name, n))
                        found = TRUE;

                g_free (n);
        } while (!found && ctk_tree_model_iter_next (model, iter));

        return found;
}

static void
update_default_tree_item (GvcMixerDialog  *dialog,
                          CtkTreeModel    *model,
                          CafeMixerStream *stream)
{
        CtkTreeIter  iter;
        const gchar *name = NULL;

        if (ctk_tree_model_get_iter_first (model, &iter) == FALSE)
                return;

        /* The supplied stream is the default, or the selected item. Traverse
         * the item list and mark each item as being selected or not. Also do not
         * presume some known stream is selected and allow NULL here. */
        if (stream != NULL)
                name = cafe_mixer_stream_get_name (stream);

        do {
                gchar *n;
                ctk_tree_model_get (model, &iter,
                                    NAME_COLUMN, &n,
                                    -1);
                ctk_list_store_set (CTK_LIST_STORE (model),
                                    &iter,
                                    ACTIVE_COLUMN, !g_strcmp0 (name, n),
                                    -1);
                g_free (n);
        } while (ctk_tree_model_iter_next (model, &iter));
}

static void
update_output_settings (GvcMixerDialog *dialog)
{
        CafeMixerStream            *stream;
        CafeMixerStreamControl     *control;
        CafeMixerStreamControlFlags flags;
        CafeMixerSwitch            *port_switch;
        gboolean                    has_settings = FALSE;

        g_debug ("Updating output settings");

        if (dialog->priv->output_balance_bar != NULL) {
                ctk_container_remove (CTK_CONTAINER (dialog->priv->output_settings_box),
                                      dialog->priv->output_balance_bar);

                dialog->priv->output_balance_bar = NULL;
        }
        if (dialog->priv->output_fade_bar != NULL) {
                ctk_container_remove (CTK_CONTAINER (dialog->priv->output_settings_box),
                                      dialog->priv->output_fade_bar);

                dialog->priv->output_fade_bar = NULL;
        }
        if (dialog->priv->output_lfe_bar != NULL) {
                ctk_container_remove (CTK_CONTAINER (dialog->priv->output_settings_box),
                                      dialog->priv->output_lfe_bar);

                dialog->priv->output_lfe_bar = NULL;
        }
        if (dialog->priv->output_port_combo != NULL) {
                ctk_container_remove (CTK_CONTAINER (dialog->priv->output_settings_box),
                                      dialog->priv->output_port_combo);

                dialog->priv->output_port_combo = NULL;
        }

        /* Get the control currently associated with the output slider */
        control = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (dialog->priv->output_bar));
        if (control == NULL) {
                g_debug ("There is no control for the default output stream");
                ctk_widget_hide (dialog->priv->output_settings_frame);
                return;
        }
        flags = cafe_mixer_stream_control_get_flags (control);

        /* Enable balance bar if it is available */
        if (flags & CAFE_MIXER_STREAM_CONTROL_CAN_BALANCE) {
                dialog->priv->output_balance_bar =
                        gvc_balance_bar_new (control, BALANCE_TYPE_RL);

                gvc_balance_bar_set_size_group (GVC_BALANCE_BAR (dialog->priv->output_balance_bar),
                                                dialog->priv->size_group,
                                                TRUE);

                ctk_box_pack_start (CTK_BOX (dialog->priv->output_settings_box),
                                    dialog->priv->output_balance_bar,
                                    FALSE, FALSE, 6);

                ctk_widget_show (dialog->priv->output_balance_bar);
                has_settings = TRUE;
        }

        /* Enable fade bar if it is available */
        if (flags & CAFE_MIXER_STREAM_CONTROL_CAN_FADE) {
                dialog->priv->output_fade_bar =
                        gvc_balance_bar_new (control, BALANCE_TYPE_FR);

                gvc_balance_bar_set_size_group (GVC_BALANCE_BAR (dialog->priv->output_fade_bar),
                                                dialog->priv->size_group,
                                                TRUE);

                ctk_box_pack_start (CTK_BOX (dialog->priv->output_settings_box),
                                    dialog->priv->output_fade_bar,
                                    FALSE, FALSE, 6);

                ctk_widget_show (dialog->priv->output_fade_bar);
                has_settings = TRUE;
        }

        /* Enable subwoofer volume bar if subwoofer is available */
        if (cafe_mixer_stream_control_has_channel_position (control, CAFE_MIXER_CHANNEL_LFE)) {
                dialog->priv->output_lfe_bar =
                        gvc_balance_bar_new (control, BALANCE_TYPE_LFE);

                gvc_balance_bar_set_size_group (GVC_BALANCE_BAR (dialog->priv->output_lfe_bar),
                                                dialog->priv->size_group,
                                                TRUE);

                ctk_box_pack_start (CTK_BOX (dialog->priv->output_settings_box),
                                    dialog->priv->output_lfe_bar,
                                    FALSE, FALSE, 6);

                ctk_widget_show (dialog->priv->output_lfe_bar);
                has_settings = TRUE;
        }

        /* Get owning stream of the control */
        stream = cafe_mixer_stream_control_get_stream (control);
        if (G_UNLIKELY (stream == NULL))
                return;

        /* Enable the port selector if the stream has one */
        port_switch = find_stream_port_switch (stream);
        if (port_switch != NULL) {
                dialog->priv->output_port_combo =
                        gvc_combo_box_new (port_switch, _("Co_nnector:"));

                gvc_combo_box_set_size_group (GVC_COMBO_BOX (dialog->priv->output_port_combo),
                                              dialog->priv->size_group,
                                              FALSE);

                ctk_box_pack_start (CTK_BOX (dialog->priv->output_settings_box),
                                    dialog->priv->output_port_combo,
                                    TRUE, FALSE, 6);

                ctk_widget_show (dialog->priv->output_port_combo);
                has_settings = TRUE;
        }

        if (has_settings == TRUE)
                ctk_widget_show (dialog->priv->output_settings_frame);
        else
                ctk_widget_hide (dialog->priv->output_settings_frame);
}

static void
set_output_stream (GvcMixerDialog *dialog, CafeMixerStream *stream)
{
        CtkTreeModel           *model;
        CafeMixerStreamControl *control;

        control = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (dialog->priv->output_bar));
        if (control != NULL) {
                /* Disconnect port switch of the previous stream */
                if (dialog->priv->output_port_combo != NULL) {
                        CafeMixerSwitch        *swtch;

                        swtch = g_object_get_data (G_OBJECT (dialog->priv->output_port_combo),
                                                   "switch");
                        if (swtch != NULL)
                                g_signal_handlers_disconnect_by_data (G_OBJECT (swtch),
                                                                      dialog);
                }
        }

        bar_set_stream (dialog, dialog->priv->output_bar, stream);

        if (stream != NULL) {
                const GList *controls;

                controls = cafe_mixer_context_list_stored_controls (dialog->priv->context);

                /* Move all stored controls to the newly selected default stream */
                while (controls != NULL) {
                        CafeMixerStream        *parent;

                        control = CAFE_MIXER_STREAM_CONTROL (controls->data);
                        parent  = cafe_mixer_stream_control_get_stream (control);

                        /* Prefer streamless controls to stay the way they are, forcing them to
                         * a particular owning stream would be wrong for eg. event controls */
                        if (parent != NULL && parent != stream) {
                                CafeMixerDirection direction =
                                        cafe_mixer_stream_get_direction (parent);

                                if (direction == CAFE_MIXER_DIRECTION_OUTPUT)
                                        cafe_mixer_stream_control_set_stream (control, stream);
                        }
                        controls = controls->next;
                }
        }

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->output_treeview));
        update_default_tree_item (dialog, model, stream);

        update_output_settings (dialog);
}

static void
on_context_default_output_stream_notify (CafeMixerContext *context,
                                         GParamSpec       *pspec,
                                         GvcMixerDialog   *dialog)
{
        CafeMixerStream *stream;

        stream = cafe_mixer_context_get_default_output_stream (context);

        set_output_stream (dialog, stream);
}

#define DECAY_STEP .15

static void
on_stream_control_monitor_value (CafeMixerStream *stream,
                                 gdouble          value,
                                 GvcMixerDialog  *dialog)
{
        CtkAdjustment *adj;

        if (dialog->priv->last_input_peak >= DECAY_STEP) {
                if (value < dialog->priv->last_input_peak - DECAY_STEP) {
                        value = dialog->priv->last_input_peak - DECAY_STEP;
                }
        }

        dialog->priv->last_input_peak = value;

        adj = gvc_level_bar_get_peak_adjustment (GVC_LEVEL_BAR (dialog->priv->input_level_bar));
        if (value >= 0)
                ctk_adjustment_set_value (adj, value);
        else
                ctk_adjustment_set_value (adj, 0.0);
}

static void
update_input_settings (GvcMixerDialog *dialog)
{
        CafeMixerStream            *stream;
        CafeMixerStreamControl     *control;
        CafeMixerStreamControlFlags flags;
        CafeMixerSwitch            *port_switch;

        g_debug ("Updating input settings");

        if (dialog->priv->input_port_combo != NULL) {
                ctk_container_remove (CTK_CONTAINER (dialog->priv->input_settings_box),
                                      dialog->priv->input_port_combo);

                dialog->priv->input_port_combo = NULL;
        }

        /* Get the control currently associated with the input slider */
        control = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (dialog->priv->input_bar));
        if (control == NULL)
                return;

        flags = cafe_mixer_stream_control_get_flags (control);

        /* Enable level bar only if supported by the control */
        if (flags & CAFE_MIXER_STREAM_CONTROL_HAS_MONITOR)
                g_signal_connect (G_OBJECT (control),
                                  "monitor-value",
                                  G_CALLBACK (on_stream_control_monitor_value),
                                  dialog);

        /* Get owning stream of the control */
        stream = cafe_mixer_stream_control_get_stream (control);
        if (G_UNLIKELY (stream == NULL))
                return;

        /* Enable the port selector if the stream has one */
        port_switch = find_stream_port_switch (stream);
        if (port_switch != NULL) {
                dialog->priv->input_port_combo =
                        gvc_combo_box_new (port_switch, _("Co_nnector:"));

                gvc_combo_box_set_size_group (GVC_COMBO_BOX (dialog->priv->input_port_combo),
                                              dialog->priv->size_group,
                                              FALSE);

                ctk_box_pack_start (CTK_BOX (dialog->priv->input_settings_box),
                                    dialog->priv->input_port_combo,
                                    TRUE, TRUE, 0);

                ctk_widget_show (dialog->priv->input_port_combo);
        }
}

static void
on_stream_control_mute_notify (CafeMixerStreamControl *control,
                               GParamSpec             *pspec,
                               GvcMixerDialog         *dialog)
{
        /* Stop monitoring the input stream when it gets muted */
        if (cafe_mixer_stream_control_get_mute (control) == TRUE)
                cafe_mixer_stream_control_set_monitor_enabled (control, FALSE);
        else
                cafe_mixer_stream_control_set_monitor_enabled (control, TRUE);
}

static void
set_input_stream (GvcMixerDialog *dialog, CafeMixerStream *stream)
{
        CtkTreeModel           *model;
        CafeMixerStreamControl *control;

        control = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (dialog->priv->input_bar));
        if (control != NULL) {
                /* Disconnect port switch of the previous stream */
                if (dialog->priv->input_port_combo != NULL) {
                        CafeMixerSwitch        *swtch;

                        swtch = g_object_get_data (G_OBJECT (dialog->priv->input_port_combo),
                                                   "switch");
                        if (swtch != NULL)
                                g_signal_handlers_disconnect_by_data (G_OBJECT (swtch),
                                                                      dialog);
                }

                /* Disable monitoring of the previous control */
                g_signal_handlers_disconnect_by_func (G_OBJECT (control),
                                                      G_CALLBACK (on_stream_control_monitor_value),
                                                      dialog);

                cafe_mixer_stream_control_set_monitor_enabled (control, FALSE);
        }

        bar_set_stream (dialog, dialog->priv->input_bar, stream);

        if (stream != NULL) {
                const GList *controls;
                guint page = ctk_notebook_get_current_page (CTK_NOTEBOOK (dialog->priv->notebook));

                controls = cafe_mixer_context_list_stored_controls (dialog->priv->context);

                /* Move all stored controls to the newly selected default stream */
                while (controls != NULL) {
                        CafeMixerStream *parent;

                        control = CAFE_MIXER_STREAM_CONTROL (controls->data);
                        parent  = cafe_mixer_stream_control_get_stream (control);

                        /* Prefer streamless controls to stay the way they are, forcing them to
                         * a particular owning stream would be wrong for eg. event controls */
                        if (parent != NULL && parent != stream) {
                                CafeMixerDirection direction =
                                        cafe_mixer_stream_get_direction (parent);

                                if (direction == CAFE_MIXER_DIRECTION_INPUT)
                                        cafe_mixer_stream_control_set_stream (control, stream);
                        }
                        controls = controls->next;
                }

                if (page == PAGE_INPUT) {
                        control = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (dialog->priv->input_bar));

                        if (G_LIKELY (control != NULL))
                                cafe_mixer_stream_control_set_monitor_enabled (control, TRUE);
                }

                /* Enable/disable the peak level monitor according to mute state */
                g_signal_connect (G_OBJECT (stream),
                                  "notify::mute",
                                  G_CALLBACK (on_stream_control_mute_notify),
                                  dialog);
        }

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->input_treeview));
        update_default_tree_item (dialog, model, stream);

        update_input_settings (dialog);
}

static void
on_context_default_input_stream_notify (CafeMixerContext *context,
                                        GParamSpec       *pspec,
                                        GvcMixerDialog   *dialog)
{
        CafeMixerStream *stream;

        g_debug ("Default input stream has changed");

        stream = cafe_mixer_context_get_default_input_stream (context);

        set_input_stream (dialog, stream);
}

static CtkWidget *
create_bar (GvcMixerDialog *dialog, gboolean use_size_group, gboolean symmetric)
{
        CtkWidget *bar;

        bar = gvc_channel_bar_new (NULL);

        if (use_size_group == TRUE)
                gvc_channel_bar_set_size_group (GVC_CHANNEL_BAR (bar),
                                                dialog->priv->size_group,
                                                symmetric);

        g_object_set (G_OBJECT (bar),
                      "orientation", CTK_ORIENTATION_HORIZONTAL,
                      "show-mute",   TRUE,
                      "show-icons",  TRUE,
                      "show-marks",  TRUE,
                      "extended",    TRUE, NULL);
        return bar;
}

static void
bar_set_stream (GvcMixerDialog  *dialog,
                CtkWidget       *bar,
                CafeMixerStream *stream)
{
        CafeMixerStreamControl *control = NULL;

        if (stream != NULL)
                control = cafe_mixer_stream_get_default_control (stream);

        bar_set_stream_control (dialog, bar, control);
}

static void
bar_set_stream_control (GvcMixerDialog         *dialog,
                        CtkWidget              *bar,
                        CafeMixerStreamControl *control)
{
        const gchar            *name;
        CafeMixerStreamControl *previous;

        previous = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (bar));
        if (previous == control)
                return;

        if (previous != NULL) {
                name = cafe_mixer_stream_control_get_name (previous);

                g_debug ("Removing stream control %s from bar %s",
                         name,
                         gvc_channel_bar_get_name (GVC_CHANNEL_BAR (bar)));

                g_signal_handlers_disconnect_by_data (G_OBJECT (previous), dialog);

                /* This may not do anything because we no longer have the information
                 * about the owning stream, in case it was an input stream, make
                 * sure to disconnected from the peak level monitor */
                cafe_mixer_stream_control_set_monitor_enabled (previous, FALSE);

                g_hash_table_remove (dialog->priv->bars, name);
        }

        gvc_channel_bar_set_control (GVC_CHANNEL_BAR (bar), control);

        if (control != NULL) {
                name = cafe_mixer_stream_control_get_name (control);

                g_debug ("Setting stream control %s for bar %s",
                         name,
                         gvc_channel_bar_get_name (GVC_CHANNEL_BAR (bar)));

                g_hash_table_insert (dialog->priv->bars,
                                     (gpointer) name,
                                     bar);

                ctk_widget_set_sensitive (CTK_WIDGET (bar), TRUE);
        } else
                ctk_widget_set_sensitive (CTK_WIDGET (bar), TRUE);
}

static void
add_application_control (GvcMixerDialog *dialog, CafeMixerStreamControl *control)
{
        CafeMixerStream                *stream;
        CafeMixerStreamControlMediaRole media_role;
        CafeMixerAppInfo               *info;
        CafeMixerDirection              direction = CAFE_MIXER_DIRECTION_UNKNOWN;
        CtkWidget                      *bar;
        const gchar                    *app_id;
        const gchar                    *app_name;
        const gchar                    *app_icon;

        media_role = cafe_mixer_stream_control_get_media_role (control);

        /* Add stream to the applications page, but make sure the stream qualifies
         * for the inclusion */
        info = cafe_mixer_stream_control_get_app_info (control);
        if (info == NULL)
                return;

        /* Skip streams with roles we don't care about */
        if (media_role == CAFE_MIXER_STREAM_CONTROL_MEDIA_ROLE_EVENT ||
            media_role == CAFE_MIXER_STREAM_CONTROL_MEDIA_ROLE_TEST ||
            media_role == CAFE_MIXER_STREAM_CONTROL_MEDIA_ROLE_ABSTRACT ||
            media_role == CAFE_MIXER_STREAM_CONTROL_MEDIA_ROLE_FILTER)
                return;

        app_id = cafe_mixer_app_info_get_id (info);

        /* These applications may have associated streams because they do peak
         * level monitoring, skip these too */
        if (!g_strcmp0 (app_id, "org.cafe.VolumeControl") ||
            !g_strcmp0 (app_id, "org.gnome.VolumeControl") ||
            !g_strcmp0 (app_id, "org.PulseAudio.pavucontrol"))
                return;

        app_name = cafe_mixer_app_info_get_name (info);
        if (app_name == NULL)
                app_name = cafe_mixer_stream_control_get_label (control);
        if (app_name == NULL)
                app_name = cafe_mixer_stream_control_get_name (control);
        if (G_UNLIKELY (app_name == NULL))
                return;

        bar = create_bar (dialog, FALSE, FALSE);

        g_object_set (G_OBJECT (bar),
                      "show-marks", FALSE,
                      "extended", FALSE,
                      NULL);

        /* By default channel bars use speaker icons, use microphone icons
         * instead for recording applications */
        stream = cafe_mixer_stream_control_get_stream (control);
        if (stream != NULL)
                direction = cafe_mixer_stream_get_direction (stream);

        if (direction == CAFE_MIXER_DIRECTION_INPUT)
                g_object_set (G_OBJECT (bar),
                              "low-icon-name", "audio-input-microphone-low",
                              "high-icon-name", "audio-input-microphone-high",
                              NULL);

        app_icon = cafe_mixer_app_info_get_icon (info);
        if (app_icon == NULL) {
                if (direction == CAFE_MIXER_DIRECTION_INPUT)
                        app_icon = "audio-input-microphone";
                else
                        app_icon = "applications-multimedia";
        }

        gvc_channel_bar_set_name (GVC_CHANNEL_BAR (bar), app_name);
        gvc_channel_bar_set_icon_name (GVC_CHANNEL_BAR (bar), app_icon);

        ctk_box_pack_start (CTK_BOX (dialog->priv->applications_box),
                            bar,
                            FALSE, FALSE, 12);

        bar_set_stream_control (dialog, bar, control);
        dialog->priv->num_apps++;

        ctk_widget_hide (dialog->priv->no_apps_label);
        ctk_widget_show (bar);
}

static void
on_stream_control_added (CafeMixerStream *stream,
                         const gchar     *name,
                         GvcMixerDialog  *dialog)
{
        CafeMixerStreamControl    *control;
        CafeMixerStreamControlRole role;

        control = cafe_mixer_stream_get_control (stream, name);
        if (G_UNLIKELY (control == NULL))
                return;

        role = cafe_mixer_stream_control_get_role (control);

        if (role == CAFE_MIXER_STREAM_CONTROL_ROLE_APPLICATION)
                add_application_control (dialog, control);
}

static void
on_stream_control_removed (CafeMixerStream *stream,
                           const gchar     *name,
                           GvcMixerDialog  *dialog)
{
        CafeMixerStreamControl *control;

        control = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (dialog->priv->input_bar));
        if (control != NULL) {
                const gchar *input_name = cafe_mixer_stream_control_get_name (control);

                if (strcmp (name, input_name) == 0) {
                        // XXX probably can't even happen, but handle it somehow
                        return;
                }
        }

        control = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (dialog->priv->output_bar));
        if (control != NULL) {
                const gchar *input_name = cafe_mixer_stream_control_get_name (control);

                if (strcmp (name, input_name) == 0) {
                        // XXX probably can't even happen, but handle it somehow
                        return;
                }
        }

        /* No way to be sure that it is an application control, but we don't have
         * any other than application bars that could match the name */
        remove_application_control (dialog, name);
}

static void
add_stream (GvcMixerDialog *dialog, CafeMixerStream *stream)
{
        CtkTreeModel      *model = NULL;
        CtkTreeIter        iter;
        const gchar       *speakers = NULL;
        const GList       *controls;
        gboolean           is_default = FALSE;
        CafeMixerDirection direction;

        direction = cafe_mixer_stream_get_direction (stream);

        if (direction == CAFE_MIXER_DIRECTION_INPUT) {
                CafeMixerStream *input;

                input = cafe_mixer_context_get_default_input_stream (dialog->priv->context);
                if (stream == input) {
                        bar_set_stream (dialog, dialog->priv->input_bar, stream);

                        update_input_settings (dialog);
                        is_default = TRUE;
                }
                model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->input_treeview));
        }
        else if (direction == CAFE_MIXER_DIRECTION_OUTPUT) {
                CafeMixerStream        *output;
                CafeMixerStreamControl *control;

                output = cafe_mixer_context_get_default_output_stream (dialog->priv->context);
                if (stream == output) {
                        bar_set_stream (dialog, dialog->priv->output_bar, stream);

                        update_output_settings (dialog);
                        is_default = TRUE;
                }
                model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->output_treeview));

                control = cafe_mixer_stream_get_default_control (stream);
                if (G_LIKELY (control != NULL))
                        speakers = gvc_channel_map_to_pretty_string (control);
        }

        controls = cafe_mixer_stream_list_controls (stream);
        while (controls != NULL) {
                CafeMixerStreamControl    *control = CAFE_MIXER_STREAM_CONTROL (controls->data);
                CafeMixerStreamControlRole role;

                role = cafe_mixer_stream_control_get_role (control);

                if (role == CAFE_MIXER_STREAM_CONTROL_ROLE_APPLICATION)
                        add_application_control (dialog, control);

                controls = controls->next;
        }

        if (model != NULL) {
                const gchar *name;
                const gchar *label;

                name  = cafe_mixer_stream_get_name (stream);
                label = cafe_mixer_stream_get_label (stream);

                ctk_list_store_append (CTK_LIST_STORE (model), &iter);
                ctk_list_store_set (CTK_LIST_STORE (model),
                                    &iter,
                                    NAME_COLUMN, name,
                                    LABEL_COLUMN, label,
                                    ACTIVE_COLUMN, is_default,
                                    SPEAKERS_COLUMN, speakers,
                                    -1);
        }

        // XXX find a way to disconnect when removed
        g_signal_connect (G_OBJECT (stream),
                          "control-added",
                          G_CALLBACK (on_stream_control_added),
                          dialog);
        g_signal_connect (G_OBJECT (stream),
                          "control-removed",
                          G_CALLBACK (on_stream_control_removed),
                          dialog);
}

static void
update_device_test_visibility (GvcMixerDialog *dialog)
{
        CafeMixerDevice *device;
        CafeMixerStream *stream;

        device = g_object_get_data (G_OBJECT (dialog->priv->hw_profile_combo), "device");
        if (G_UNLIKELY (device == NULL)) {
                return;
        }

        stream = find_device_test_stream (dialog, device);

        g_object_set (G_OBJECT (dialog->priv->hw_profile_combo),
                      "show-button", (stream != NULL),
                      NULL);
}

static void
on_context_stream_added (CafeMixerContext *context,
                         const gchar      *name,
                         GvcMixerDialog   *dialog)
{
        CafeMixerStream   *stream;
        CafeMixerDirection direction;
        CtkWidget         *bar;

        stream = cafe_mixer_context_get_stream (context, name);
        if (G_UNLIKELY (stream == NULL))
                return;

        direction = cafe_mixer_stream_get_direction (stream);

        /* If the newly added stream belongs to the currently selected device and
         * the test button is hidden, this stream may be the one to allow the
         * sound test and therefore we may need to enable the button */
        if (dialog->priv->hw_profile_combo != NULL && direction == CAFE_MIXER_DIRECTION_OUTPUT) {
                CafeMixerDevice *device1;
                CafeMixerDevice *device2;

                device1 = cafe_mixer_stream_get_device (stream);
                device2 = g_object_get_data (G_OBJECT (dialog->priv->hw_profile_combo),
                                             "device");

                if (device1 == device2) {
                        gboolean show_button;

                        g_object_get (G_OBJECT (dialog->priv->hw_profile_combo),
                                      "show-button", &show_button,
                                      NULL);

                        if (show_button == FALSE)
                                update_device_test_visibility (dialog);
                }
        }

        bar = g_hash_table_lookup (dialog->priv->bars, name);
        if (G_UNLIKELY (bar != NULL))
                return;

        add_stream (dialog, stream);
}

static void
remove_stream (GvcMixerDialog *dialog, const gchar *name)
{
        CtkWidget    *bar;
        CtkTreeIter   iter;
        CtkTreeModel *model;

        bar = g_hash_table_lookup (dialog->priv->bars, name);

        if (bar != NULL) {
                g_debug ("Removing stream %s from bar %s",
                         name,
                         gvc_channel_bar_get_name (GVC_CHANNEL_BAR (bar)));

                bar_set_stream (dialog, bar, NULL);
        }

        /* Remove from any models */
        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->output_treeview));
        if (find_tree_item_by_name (CTK_TREE_MODEL (model),
                                    name,
                                    NAME_COLUMN,
                                    &iter) == TRUE)
                ctk_list_store_remove (CTK_LIST_STORE (model), &iter);

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->input_treeview));
        if (find_tree_item_by_name (CTK_TREE_MODEL (model),
                                    name,
                                    NAME_COLUMN,
                                    &iter) == TRUE)
                ctk_list_store_remove (CTK_LIST_STORE (model), &iter);
}

static void
remove_application_control (GvcMixerDialog *dialog, const gchar *name)
{
        CtkWidget *bar;

        bar = g_hash_table_lookup (dialog->priv->bars, name);
        if (G_UNLIKELY (bar == NULL))
                return;

        g_debug ("Removing application stream %s", name);

        /* We could call bar_set_stream_control here, but that would pointlessly
         * invalidate the channel bar, so just remove it ourselves */
        g_hash_table_remove (dialog->priv->bars, name);

        ctk_container_remove (CTK_CONTAINER (ctk_widget_get_parent (bar)), bar);

        if (G_UNLIKELY (dialog->priv->num_apps <= 0)) {
                g_warn_if_reached ();
                dialog->priv->num_apps = 1;
        }

        dialog->priv->num_apps--;

        if (dialog->priv->num_apps == 0)
                ctk_widget_show (dialog->priv->no_apps_label);
}

static void
on_context_stream_removed (CafeMixerContext *context,
                           const gchar      *name,
                           GvcMixerDialog   *dialog)
{
        if (dialog->priv->hw_profile_combo != NULL) {
                gboolean show_button;

                g_object_get (G_OBJECT (dialog->priv->hw_profile_combo),
                              "show-button", &show_button,
                              NULL);

                if (show_button == TRUE)
                        update_device_test_visibility (dialog);
        }

        remove_stream (dialog, name);
}

static void
on_context_stored_control_added (CafeMixerContext *context,
                                 const gchar      *name,
                                 GvcMixerDialog   *dialog)
{
        CafeMixerStreamControl         *control;
        CafeMixerStreamControlMediaRole media_role;

        control = CAFE_MIXER_STREAM_CONTROL (cafe_mixer_context_get_stored_control (context, name));
        if (G_UNLIKELY (control == NULL))
                return;

        media_role = cafe_mixer_stream_control_get_media_role (control);

        if (media_role == CAFE_MIXER_STREAM_CONTROL_MEDIA_ROLE_EVENT)
                bar_set_stream_control (dialog, dialog->priv->effects_bar, control);
}

static void
on_context_stored_control_removed (CafeMixerContext *context,
                                   const gchar      *name,
                                   GvcMixerDialog   *dialog)
{
        CtkWidget *bar;

        bar = g_hash_table_lookup (dialog->priv->bars, name);

        if (bar != NULL) {
                /* We only use a stored control in the effects bar */
                if (G_UNLIKELY (bar != dialog->priv->effects_bar)) {
                        g_warn_if_reached ();
                        return;
                }

                bar_set_stream (dialog, bar, NULL);
        }
}

static gchar *
device_status (CafeMixerDevice *device)
{
        guint        inputs = 0;
        guint        outputs = 0;
        gchar       *inputs_str = NULL;
        gchar       *outputs_str = NULL;
        const GList *streams;

        /* Get number of input and output streams in the device */
        streams = cafe_mixer_device_list_streams (device);
        while (streams != NULL) {
                CafeMixerStream   *stream = CAFE_MIXER_STREAM (streams->data);
                CafeMixerDirection direction;

                direction = cafe_mixer_stream_get_direction (stream);

                if (direction == CAFE_MIXER_DIRECTION_INPUT)
                        inputs++;
                else if (direction == CAFE_MIXER_DIRECTION_OUTPUT)
                        outputs++;

                streams = streams->next;
        }

        if (inputs == 0 && outputs == 0) {
                /* translators:
                 * The device has been disabled */
                return g_strdup (_("Disabled"));
        }

        if (outputs > 0) {
                /* translators:
                 * The number of sound outputs on a particular device */
                outputs_str = g_strdup_printf (ngettext ("%u Output",
                                                         "%u Outputs",
                                                          outputs),
                                               outputs);
        }

        if (inputs > 0) {
                /* translators:
                 * The number of sound inputs on a particular device */
                inputs_str = g_strdup_printf (ngettext ("%u Input",
                                                        "%u Inputs",
                                                        inputs),
                                              inputs);
        }

        if (inputs_str != NULL && outputs_str != NULL) {
                gchar *ret = g_strdup_printf ("%s / %s",
                                              outputs_str,
                                              inputs_str);
                g_free (outputs_str);
                g_free (inputs_str);
                return ret;
        }

        if (inputs_str != NULL)
                return inputs_str;

        return outputs_str;
}

static void
update_device_info (GvcMixerDialog *dialog, CafeMixerDevice *device)
{
        CtkTreeModel    *model = NULL;
        CtkTreeIter      iter;
        const gchar     *label;
        const gchar     *profile_label = NULL;
        gchar           *status;
        CafeMixerSwitch *profile_switch;

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->hw_treeview));

        if (find_tree_item_by_name (model,
                                    cafe_mixer_device_get_name (device),
                                    HW_NAME_COLUMN,
                                    &iter) == FALSE)
                return;

        label = cafe_mixer_device_get_label (device);

        profile_switch = find_device_profile_switch (device);
        if (profile_switch != NULL) {
                CafeMixerSwitchOption *active;

                active = cafe_mixer_switch_get_active_option (profile_switch);
                if (G_LIKELY (active != NULL))
                        profile_label = cafe_mixer_switch_option_get_label (active);
        }

        status = device_status (device);

        ctk_list_store_set (CTK_LIST_STORE (model),
                            &iter,
                            HW_LABEL_COLUMN, label,
                            HW_PROFILE_COLUMN, profile_label,
                            HW_STATUS_COLUMN, status,
                            -1);
        g_free (status);
}

static void
on_device_profile_active_option_notify (CafeMixerDeviceSwitch *swtch,
                                        GParamSpec            *pspec,
                                        GvcMixerDialog        *dialog)
{
        CafeMixerDevice *device;

        device = cafe_mixer_device_switch_get_device (swtch);

        update_device_info (dialog, device);
}

static void
add_device (GvcMixerDialog *dialog, CafeMixerDevice *device)
{
        CtkTreeModel    *model;
        CtkTreeIter      iter;
        GIcon           *icon;
        const gchar     *name;
        const gchar     *label;
        gchar           *status;
        const gchar     *profile_label = NULL;
        CafeMixerSwitch *profile_switch;

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->hw_treeview));

        name  = cafe_mixer_device_get_name (device);
        label = cafe_mixer_device_get_label (device);

        if (find_tree_item_by_name (CTK_TREE_MODEL (model),
                                    name,
                                    HW_NAME_COLUMN,
                                    &iter) == FALSE)
                ctk_list_store_append (CTK_LIST_STORE (model), &iter);

        icon = g_themed_icon_new_with_default_fallbacks (cafe_mixer_device_get_icon (device));

        profile_switch = find_device_profile_switch (device);
        if (profile_switch != NULL) {
                CafeMixerSwitchOption *active;

                active = cafe_mixer_switch_get_active_option (profile_switch);
                if (G_LIKELY (active != NULL))
                        profile_label = cafe_mixer_switch_option_get_label (active);

                g_signal_connect (G_OBJECT (profile_switch),
                                  "notify::active-option",
                                  G_CALLBACK (on_device_profile_active_option_notify),
                                  dialog);
        }

        status = device_status (device);

        ctk_list_store_set (CTK_LIST_STORE (model),
                            &iter,
                            HW_NAME_COLUMN, name,
                            HW_LABEL_COLUMN, label,
                            HW_ICON_COLUMN, icon,
                            HW_PROFILE_COLUMN, profile_label,
                            HW_STATUS_COLUMN, status,
                            -1);
        g_free (status);

}

static void
on_context_device_added (CafeMixerContext *context, const gchar *name, GvcMixerDialog *dialog)
{
        CafeMixerDevice *device;

        device = cafe_mixer_context_get_device (context, name);
        if (G_UNLIKELY (device == NULL))
                return;

        add_device (dialog, device);
}

static void
on_context_device_removed (CafeMixerContext *context,
                           const gchar      *name,
                           GvcMixerDialog   *dialog)
{
        CtkTreeIter   iter;
        CtkTreeModel *model;

        /* Remove from the device model */
        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->hw_treeview));

        if (find_tree_item_by_name (CTK_TREE_MODEL (model),
                                    name,
                                    HW_NAME_COLUMN,
                                    &iter) == TRUE)
                ctk_list_store_remove (CTK_LIST_STORE (model), &iter);
}

static void
make_label_bold (CtkLabel *label)
{
        PangoFontDescription *font_desc;

        font_desc = pango_font_description_new ();

        pango_font_description_set_weight (font_desc, PANGO_WEIGHT_BOLD);

        /* This will only affect the weight of the font, the rest is
         * from the current state of the widget, which comes from the
         * theme or user prefs, since the font desc only has the
         * weight flag turned on. */
        PangoAttrList *attrs = pango_attr_list_new ();
        PangoAttribute *font_desc_attr = pango_attr_font_desc_new (font_desc);
        pango_attr_list_insert (attrs, font_desc_attr);
        ctk_label_set_attributes (label, attrs);
        pango_attr_list_unref (attrs);

        pango_font_description_free (font_desc);
}

static void
on_input_radio_toggled (CtkCellRendererToggle *renderer,
                        gchar                 *path_str,
                        GvcMixerDialog        *dialog)
{
        CtkTreeModel *model;
        CtkTreeIter   iter;
        CtkTreePath  *path;
        gboolean      toggled = FALSE;
        gchar        *name = NULL;

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->input_treeview));
        path  = ctk_tree_path_new_from_string (path_str);

        ctk_tree_model_get_iter (model, &iter, path);
        ctk_tree_path_free (path);

        ctk_tree_model_get (model, &iter,
                            NAME_COLUMN, &name,
                            ACTIVE_COLUMN, &toggled,
                            -1);
        if (toggled ^ 1) {
                CafeMixerStream      *stream;
                CafeMixerBackendFlags flags;

                stream = cafe_mixer_context_get_stream (dialog->priv->context, name);
                if (G_UNLIKELY (stream == NULL)) {
                        g_warn_if_reached ();
                        g_free (name);
                        return;
                }

                g_debug ("Default input stream selection changed to %s", name);

                // XXX cache this
                flags = cafe_mixer_context_get_backend_flags (dialog->priv->context);

                if (flags & CAFE_MIXER_BACKEND_CAN_SET_DEFAULT_INPUT_STREAM)
                        cafe_mixer_context_set_default_input_stream (dialog->priv->context, stream);
                else
                        set_input_stream (dialog, stream);
        }
        g_free (name);
}

static void
on_output_radio_toggled (CtkCellRendererToggle *renderer,
                         gchar                 *path_str,
                         GvcMixerDialog        *dialog)
{
        CtkTreeModel *model;
        CtkTreeIter   iter;
        CtkTreePath  *path;
        gboolean      toggled = FALSE;
        gchar        *name = NULL;

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->output_treeview));
        path  = ctk_tree_path_new_from_string (path_str);

        ctk_tree_model_get_iter (model, &iter, path);
        ctk_tree_path_free (path);

        ctk_tree_model_get (model, &iter,
                            NAME_COLUMN, &name,
                            ACTIVE_COLUMN, &toggled,
                            -1);
        if (toggled ^ 1) {
                CafeMixerStream      *stream;
                CafeMixerBackendFlags flags;

                stream = cafe_mixer_context_get_stream (dialog->priv->context, name);
                if (G_UNLIKELY (stream == NULL)) {
                        g_warn_if_reached ();
                        g_free (name);
                        return;
                }

                g_debug ("Default output stream selection changed to %s", name);

                // XXX cache this
                flags = cafe_mixer_context_get_backend_flags (dialog->priv->context);

                if (flags & CAFE_MIXER_BACKEND_CAN_SET_DEFAULT_OUTPUT_STREAM)
                        cafe_mixer_context_set_default_output_stream (dialog->priv->context, stream);
                else
                        set_output_stream (dialog, stream);
        }
        g_free (name);
}

static void
stream_name_to_text (CtkTreeViewColumn *column,
                     CtkCellRenderer   *cell,
                     CtkTreeModel      *model,
                     CtkTreeIter       *iter,
                     gpointer           user_data)
{
        gchar *label;
        gchar *speakers;

        ctk_tree_model_get (model, iter,
                            LABEL_COLUMN, &label,
                            SPEAKERS_COLUMN, &speakers,
                            -1);

        if (speakers != NULL) {
                gchar *str = g_strdup_printf ("%s\n<i>%s</i>",
                                              label,
                                              speakers);

                g_object_set (cell, "markup", str, NULL);
                g_free (str);
                g_free (speakers);
        } else {
                g_object_set (cell, "text", label, NULL);
        }

        g_free (label);
}

static gint
compare_stream_treeview_items (CtkTreeModel *model,
                               CtkTreeIter  *a,
                               CtkTreeIter  *b,
                               gpointer      user_data)
{
        gchar *desc_a = NULL;
        gchar *desc_b = NULL;
        gint   result;

        ctk_tree_model_get (model, a,
                            LABEL_COLUMN, &desc_a,
                            -1);
        ctk_tree_model_get (model, b,
                            LABEL_COLUMN, &desc_b,
                            -1);

        if (desc_a == NULL) {
                g_free (desc_b);
                return -1;
        }
        if (desc_b == NULL) {
                g_free (desc_a);
                return 1;
        }

        result = g_ascii_strcasecmp (desc_a, desc_b);

        g_free (desc_a);
        g_free (desc_b);
        return result;
}

static CtkWidget *
create_stream_treeview (GvcMixerDialog *dialog, GCallback on_toggled)
{
        CtkWidget         *treeview;
        CtkListStore      *store;
        CtkCellRenderer   *renderer;
        CtkTreeViewColumn *column;

        treeview = ctk_tree_view_new ();
        ctk_tree_view_set_headers_visible (CTK_TREE_VIEW (treeview), FALSE);

        store = ctk_list_store_new (NUM_COLUMNS,
                                    G_TYPE_ICON,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_BOOLEAN,
                                    G_TYPE_STRING);

        ctk_tree_view_set_model (CTK_TREE_VIEW (treeview),
                                 CTK_TREE_MODEL (store));

        renderer = ctk_cell_renderer_toggle_new ();
        ctk_cell_renderer_toggle_set_radio (CTK_CELL_RENDERER_TOGGLE (renderer), TRUE);

        column = ctk_tree_view_column_new_with_attributes (NULL,
                                                           renderer,
                                                           "active", ACTIVE_COLUMN,
                                                           NULL);

        ctk_tree_view_append_column (CTK_TREE_VIEW (treeview), column);

        g_signal_connect (G_OBJECT (renderer),
                          "toggled",
                          G_CALLBACK (on_toggled),
                          dialog);

        ctk_tree_view_insert_column_with_data_func (CTK_TREE_VIEW (treeview), -1,
                                                    _("Name"),
                                                    ctk_cell_renderer_text_new (),
                                                    stream_name_to_text,
                                                    NULL, NULL);

        /* Keep the list of streams sorted by the name */
        ctk_tree_sortable_set_sort_column_id (CTK_TREE_SORTABLE (store),
                                              LABEL_COLUMN,
                                              CTK_SORT_ASCENDING);

        ctk_tree_sortable_set_sort_func (CTK_TREE_SORTABLE (store),
                                         LABEL_COLUMN,
                                         compare_stream_treeview_items,
                                         NULL, NULL);
        return treeview;
}

static void
on_device_profile_changing (GvcComboBox           *combobox,
                            CafeMixerSwitchOption *option,
                            GvcMixerDialog        *dialog)
{
        g_debug ("Changing device profile");
        // TODO
}

static void
on_test_speakers_clicked (GvcComboBox *widget, GvcMixerDialog *dialog)
{
        CtkWidget       *d,
                        *test,
                        *container;
        gchar           *title;
        CafeMixerDevice *device;
        CafeMixerStream *stream;

        device = g_object_get_data (G_OBJECT (widget), "device");
        if (G_UNLIKELY (device == NULL)) {
                g_warn_if_reached ();
                return;
        }

        stream = find_device_test_stream (dialog, device);
        if (G_UNLIKELY (stream == NULL)) {
                g_warn_if_reached ();
                return;
        }

        title = g_strdup_printf (_("Speaker Testing for %s"),
                                 cafe_mixer_device_get_label (device));

        d = ctk_dialog_new_with_buttons (title,
                                         CTK_WINDOW (dialog),
                                         CTK_DIALOG_MODAL |
                                         CTK_DIALOG_DESTROY_WITH_PARENT,
                                         "ctk-close",
                                         CTK_RESPONSE_CLOSE,
                                         NULL);
        g_free (title);

        ctk_window_set_resizable (CTK_WINDOW (d), FALSE);

        test = gvc_speaker_test_new (stream);
        ctk_widget_show (test);

        container = ctk_dialog_get_content_area (CTK_DIALOG (d));
        ctk_container_add (CTK_CONTAINER (container), test);

        ctk_dialog_run (CTK_DIALOG (d));
        ctk_widget_destroy (d);
}

static void
on_device_selection_changed (CtkTreeSelection *selection, GvcMixerDialog *dialog)
{
        CtkTreeIter          iter;
        gchar               *name;
        CafeMixerDevice     *device;
        CafeMixerSwitch     *profile_switch;

        g_debug ("Device selection changed");

        if (dialog->priv->hw_profile_combo != NULL) {
                ctk_container_remove (CTK_CONTAINER (dialog->priv->hw_settings_box),
                                      dialog->priv->hw_profile_combo);

                dialog->priv->hw_profile_combo = NULL;
        }

        if (ctk_tree_selection_get_selected (selection, NULL, &iter) == FALSE)
                return;

        ctk_tree_model_get (ctk_tree_view_get_model (CTK_TREE_VIEW (dialog->priv->hw_treeview)),
                            &iter,
                            HW_NAME_COLUMN, &name,
                            -1);

        device = cafe_mixer_context_get_device (dialog->priv->context, name);
        if (G_UNLIKELY (device == NULL)) {
                g_warn_if_reached ();
                g_free (name);
                return;
        }
        g_free (name);

        /* Profile/speaker test combo */
        profile_switch = find_device_profile_switch (device);
        if (profile_switch != NULL) {
                dialog->priv->hw_profile_combo =
                        gvc_combo_box_new (profile_switch, _("_Profile:"));

                g_object_set (G_OBJECT (dialog->priv->hw_profile_combo),
                              "button-label", _("Test Speakers"),
                              NULL);

                g_signal_connect (G_OBJECT (dialog->priv->hw_profile_combo),
                                  "changing",
                                  G_CALLBACK (on_device_profile_changing),
                                  dialog);

                g_signal_connect (G_OBJECT (dialog->priv->hw_profile_combo),
                                  "button-clicked",
                                  G_CALLBACK (on_test_speakers_clicked),
                                  dialog);

                g_object_set_data_full (G_OBJECT (dialog->priv->hw_profile_combo),
                                        "device",
                                        g_object_ref (device),
                                        g_object_unref);

                ctk_box_pack_start (CTK_BOX (dialog->priv->hw_settings_box),
                                    dialog->priv->hw_profile_combo,
                                    TRUE, TRUE, 6);

                /* Enable the speaker test button if the selected device
                 * is capable of sound output */
                update_device_test_visibility (dialog);

                ctk_widget_show (dialog->priv->hw_profile_combo);
        }
}

static void
on_notebook_switch_page (CtkNotebook    *notebook,
                         CtkWidget      *page,
                         guint           page_num,
                         GvcMixerDialog *dialog)
{
        CafeMixerStreamControl *control;

        // XXX because this is called too early in constructor
        if (G_UNLIKELY (dialog->priv->input_bar == NULL))
                return;

        control = gvc_channel_bar_get_control (GVC_CHANNEL_BAR (dialog->priv->input_bar));
        if (control == NULL)
                return;

        if (page_num == PAGE_INPUT)
                cafe_mixer_stream_control_set_monitor_enabled (control, TRUE);
        else
                cafe_mixer_stream_control_set_monitor_enabled (control, FALSE);
}

static void
device_name_to_text (CtkTreeViewColumn *column,
                     CtkCellRenderer   *cell,
                     CtkTreeModel      *model,
                     CtkTreeIter       *iter,
                     gpointer           user_data)
{
        gchar *label = NULL;
        gchar *profile = NULL;
        gchar *status = NULL;

        ctk_tree_model_get (model, iter,
                            HW_LABEL_COLUMN, &label,
                            HW_PROFILE_COLUMN, &profile,
                            HW_STATUS_COLUMN, &status,
                            -1);

        if (G_LIKELY (status != NULL)) {
                gchar *str;

                if (profile != NULL)
                        str = g_strdup_printf ("%s\n<i>%s</i>\n<i>%s</i>",
                                               label,
                                               status,
                                               profile);
                else
                        str = g_strdup_printf ("%s\n<i>%s</i>",
                                               label,
                                               status);

                g_object_set (cell, "markup", str, NULL);
                g_free (str);
                g_free (profile);
                g_free (status);
        } else
                g_object_set (cell, "text", label, NULL);

        g_free (label);
}

static gint
compare_device_treeview_items (CtkTreeModel *model,
                               CtkTreeIter  *a,
                               CtkTreeIter  *b,
                               gpointer      user_data)
{
        gchar *desc_a = NULL;
        gchar *desc_b = NULL;
        gint   result;

        ctk_tree_model_get (model, a,
                            HW_LABEL_COLUMN, &desc_a,
                            -1);
        ctk_tree_model_get (model, b,
                            HW_LABEL_COLUMN, &desc_b,
                            -1);

        result = g_ascii_strcasecmp (desc_a, desc_b);

        g_free (desc_a);
        g_free (desc_b);
        return result;
}

static CtkWidget *
create_device_treeview (GvcMixerDialog *dialog, GCallback on_changed)
{
        CtkWidget         *treeview;
        CtkListStore      *store;
        CtkCellRenderer   *renderer;
        CtkTreeViewColumn *column;
        CtkTreeSelection  *selection;

        treeview = ctk_tree_view_new ();
        ctk_tree_view_set_headers_visible (CTK_TREE_VIEW (treeview), FALSE);

        selection = ctk_tree_view_get_selection (CTK_TREE_VIEW (treeview));
        g_signal_connect (G_OBJECT (selection),
                          "changed",
                          on_changed,
                          dialog);

        store = ctk_list_store_new (HW_NUM_COLUMNS,
                                    G_TYPE_ICON,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING);

        ctk_tree_view_set_model (CTK_TREE_VIEW (treeview),
                                 CTK_TREE_MODEL (store));

        renderer = ctk_cell_renderer_pixbuf_new ();
        g_object_set (G_OBJECT (renderer),
                      "stock-size",
                      CTK_ICON_SIZE_DIALOG,
                      NULL);

        column = ctk_tree_view_column_new_with_attributes (NULL,
                                                           renderer,
                                                           "gicon", HW_ICON_COLUMN,
                                                           NULL);

        ctk_tree_view_append_column (CTK_TREE_VIEW (treeview), column);
        ctk_tree_view_insert_column_with_data_func (CTK_TREE_VIEW (treeview), -1,
                                                    _("Name"),
                                                    ctk_cell_renderer_text_new (),
                                                    device_name_to_text,
                                                    NULL, NULL);

        /* Keep the list of streams sorted by the name */
        ctk_tree_sortable_set_sort_column_id (CTK_TREE_SORTABLE (store),
                                              HW_LABEL_COLUMN,
                                              CTK_SORT_ASCENDING);

        ctk_tree_sortable_set_sort_func (CTK_TREE_SORTABLE (store),
                                         HW_LABEL_COLUMN,
                                         compare_device_treeview_items,
                                         NULL, NULL);
        return treeview;
}

static void
dialog_accel_cb (CtkAccelGroup    *accelgroup,
                 GObject          *object,
                 guint             key,
                 GdkModifierType   mod,
                 GvcMixerDialog   *self)
{
        gint num = -1;
        gint i;

        for (i = 0; i < G_N_ELEMENTS (tab_accel_keys); i++) {
                if (tab_accel_keys[i] == key) {
                        num = i;
                        break;
                }
        }

        if (num != -1)
                ctk_notebook_set_current_page (CTK_NOTEBOOK (self->priv->notebook), num);
}

static void
create_page_effects (GvcMixerDialog *self)
{
        CtkWidget *box;
        CtkWidget *label;
        CtkWidget *chooser;

        box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 6);
        ctk_container_set_border_width (CTK_CONTAINER (box), 12);

        label = ctk_label_new (_("Sound Effects"));
        ctk_notebook_append_page (CTK_NOTEBOOK (self->priv->notebook),
                                  box,
                                  label);

        /*
         * Create a volume slider for the sound effect sounds.
         *
         * Only look for a stored control because regular controls only exist
         * for short time periods when an event sound is played.
         */
        if (self->priv->backend_flags & CAFE_MIXER_BACKEND_HAS_STORED_CONTROLS) {
                GvcChannelBar *bar;
                const GList   *list;

                bar = GVC_CHANNEL_BAR (create_bar (self, TRUE, TRUE));

                ctk_box_pack_start (CTK_BOX (box),
                                    CTK_WIDGET (bar),
                                    FALSE, FALSE, 0);

                gvc_channel_bar_set_show_marks (bar, FALSE);
                gvc_channel_bar_set_extended (bar, FALSE);
                gvc_channel_bar_set_name (bar, _("_Alert volume: "));

                /* Find an event role stored control */
                list = cafe_mixer_context_list_stored_controls (self->priv->context);
                while (list != NULL) {
                        CafeMixerStreamControl *control = CAFE_MIXER_STREAM_CONTROL (list->data);
                        CafeMixerStreamControlMediaRole media_role;

                        media_role = cafe_mixer_stream_control_get_media_role (control);

                        if (media_role == CAFE_MIXER_STREAM_CONTROL_MEDIA_ROLE_EVENT) {
                                bar_set_stream_control (self, CTK_WIDGET (bar), control);
                                break;
                        }

                        list = list->next;
                }

                self->priv->effects_bar = CTK_WIDGET (bar);
        }

        chooser = gvc_sound_theme_chooser_new ();
        ctk_box_pack_start (CTK_BOX (box),
                            chooser,
                            TRUE, TRUE, 6);
}

static GObject *
gvc_mixer_dialog_constructor (GType                  type,
                              guint                  n_construct_properties,
                              GObjectConstructParam *construct_params)
{
        GObject          *object;
        GvcMixerDialog   *self;
        CtkWidget        *main_vbox;
        CtkWidget        *label;
        CtkWidget        *box;
        CtkWidget        *scroll_box;
        CtkWidget        *sbox;
        CtkWidget        *ebox;
        CtkTreeSelection *selection;
        CtkAccelGroup    *accel_group;
        CtkTreeIter       iter;
        gint              i;
        const GList      *list;
        GClosure         *closure = NULL;

        object = G_OBJECT_CLASS (gvc_mixer_dialog_parent_class)->constructor (type,
                                                                              n_construct_properties,
                                                                              construct_params);

        self = GVC_MIXER_DIALOG (object);

        ctk_dialog_add_button (CTK_DIALOG (self), "ctk-close", CTK_RESPONSE_OK);

        main_vbox = ctk_dialog_get_content_area (CTK_DIALOG (self));
        ctk_box_set_spacing (CTK_BOX (main_vbox), 2);

        ctk_container_set_border_width (CTK_CONTAINER (self), 6);

        self->priv->output_stream_box = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 12);
        ctk_widget_set_margin_top (self->priv->output_stream_box, 12);

        ctk_box_pack_start (CTK_BOX (main_vbox),
                            self->priv->output_stream_box,
                            FALSE, FALSE, 0);

        self->priv->output_bar = create_bar (self, TRUE, TRUE);
        gvc_channel_bar_set_name (GVC_CHANNEL_BAR (self->priv->output_bar),
                                  _("_Output volume: "));

        ctk_widget_show (self->priv->output_bar);
        ctk_widget_set_sensitive (self->priv->output_bar, FALSE);

        ctk_box_pack_start (CTK_BOX (self->priv->output_stream_box),
                            self->priv->output_bar, TRUE, TRUE, 12);

        self->priv->notebook = ctk_notebook_new ();

        ctk_widget_add_events (self->priv->notebook, GDK_SCROLL_MASK);
        g_signal_connect (self->priv->notebook,
                          "scroll-event",
                          G_CALLBACK (dialog_page_scroll_event_cb),
                          self);

        ctk_box_pack_start (CTK_BOX (main_vbox),
                            self->priv->notebook,
                            TRUE, TRUE, 0);

        g_signal_connect (G_OBJECT (self->priv->notebook),
                          "switch-page",
                          G_CALLBACK (on_notebook_switch_page),
                          self);

        ctk_container_set_border_width (CTK_CONTAINER (self->priv->notebook), 5);

        /* Set up accels (borrowed from Empathy) */
        accel_group = ctk_accel_group_new ();
        ctk_window_add_accel_group (CTK_WINDOW (self), accel_group);

        for (i = 0; i < G_N_ELEMENTS (tab_accel_keys); i++) {
                closure = g_cclosure_new (G_CALLBACK (dialog_accel_cb),
                                          self,
                                          NULL);
                ctk_accel_group_connect (accel_group,
                                         tab_accel_keys[i],
                                         GDK_MOD1_MASK,
                                         0,
                                         closure);
        }

        g_object_unref (accel_group);

        /* Create notebook pages */
        create_page_effects (self);

        self->priv->hw_box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 12);
        ctk_container_set_border_width (CTK_CONTAINER (self->priv->hw_box), 12);

        label = ctk_label_new (_("Hardware"));
        ctk_notebook_append_page (CTK_NOTEBOOK (self->priv->notebook),
                                  self->priv->hw_box,
                                  label);

        box = ctk_frame_new (_("C_hoose a device to configure:"));
        label = ctk_frame_get_label_widget (CTK_FRAME (box));
        make_label_bold (CTK_LABEL (label));
        ctk_label_set_use_underline (CTK_LABEL (label), TRUE);
        ctk_frame_set_shadow_type (CTK_FRAME (box), CTK_SHADOW_NONE);
        ctk_box_pack_start (CTK_BOX (self->priv->hw_box), box, TRUE, TRUE, 0);

        self->priv->hw_treeview = create_device_treeview (self,
                                                         G_CALLBACK (on_device_selection_changed));
        ctk_label_set_mnemonic_widget (CTK_LABEL (label), self->priv->hw_treeview);

        scroll_box = ctk_scrolled_window_new (NULL, NULL);
        ctk_widget_set_margin_top (scroll_box, 6);
        ctk_scrolled_window_set_policy (CTK_SCROLLED_WINDOW (scroll_box),
                                        CTK_POLICY_NEVER,
                                        CTK_POLICY_AUTOMATIC);
        ctk_scrolled_window_set_shadow_type (CTK_SCROLLED_WINDOW (scroll_box),
                                             CTK_SHADOW_IN);
        ctk_container_add (CTK_CONTAINER (scroll_box), self->priv->hw_treeview);
        ctk_container_add (CTK_CONTAINER (box), scroll_box);

        selection = ctk_tree_view_get_selection (CTK_TREE_VIEW (self->priv->hw_treeview));
        ctk_tree_selection_set_mode (selection, CTK_SELECTION_SINGLE);

        box = ctk_frame_new (_("Settings for the selected device:"));
        label = ctk_frame_get_label_widget (CTK_FRAME (box));
        make_label_bold (CTK_LABEL (label));
        ctk_frame_set_shadow_type (CTK_FRAME (box), CTK_SHADOW_NONE);
        ctk_box_pack_start (CTK_BOX (self->priv->hw_box), box, FALSE, TRUE, 12);

        self->priv->hw_settings_box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 12);

        ctk_container_add (CTK_CONTAINER (box), self->priv->hw_settings_box);

        self->priv->input_box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 12);

        ctk_container_set_border_width (CTK_CONTAINER (self->priv->input_box), 12);

        label = ctk_label_new (_("Input"));
        ctk_notebook_append_page (CTK_NOTEBOOK (self->priv->notebook),
                                  self->priv->input_box,
                                  label);

        self->priv->input_bar = create_bar (self, TRUE, TRUE);
        ctk_widget_set_margin_top (self->priv->input_bar, 6);

        gvc_channel_bar_set_name (GVC_CHANNEL_BAR (self->priv->input_bar),
                                  _("_Input volume: "));

        gvc_channel_bar_set_low_icon_name (GVC_CHANNEL_BAR (self->priv->input_bar),
                                           "audio-input-microphone-low");
        gvc_channel_bar_set_high_icon_name (GVC_CHANNEL_BAR (self->priv->input_bar),
                                            "audio-input-microphone-high");

        ctk_widget_show (self->priv->input_bar);
        ctk_widget_set_sensitive (self->priv->input_bar, FALSE);

        ctk_box_pack_start (CTK_BOX (self->priv->input_box),
                            self->priv->input_bar,
                            FALSE, FALSE, 0);

        box  = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 6);
        sbox = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 6);
        ebox = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 6);

        ctk_box_pack_start (CTK_BOX (self->priv->input_box),
                            box,
                            FALSE, FALSE, 6);
        ctk_box_pack_start (CTK_BOX (box),
                            sbox,
                            FALSE, FALSE, 0);

        label = ctk_label_new (_("Input level:"));
        ctk_box_pack_start (CTK_BOX (sbox),
                            label,
                            FALSE, FALSE, 0);
        ctk_size_group_add_widget (self->priv->size_group, sbox);

        self->priv->input_level_bar = gvc_level_bar_new ();
        gvc_level_bar_set_orientation (GVC_LEVEL_BAR (self->priv->input_level_bar),
                                       CTK_ORIENTATION_HORIZONTAL);
        gvc_level_bar_set_scale (GVC_LEVEL_BAR (self->priv->input_level_bar),
                                 GVC_LEVEL_SCALE_LINEAR);
        ctk_box_pack_start (CTK_BOX (box),
                            self->priv->input_level_bar,
                            TRUE, TRUE, 6);

        ctk_box_pack_start (CTK_BOX (box),
                            ebox,
                            FALSE, FALSE, 0);
        ctk_size_group_add_widget (self->priv->size_group, ebox);

        self->priv->input_settings_box = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 6);
        ctk_box_pack_start (CTK_BOX (self->priv->input_box),
                            self->priv->input_settings_box,
                            FALSE, FALSE, 0);

        box = ctk_frame_new (_("C_hoose a device for sound input:"));
        label = ctk_frame_get_label_widget (CTK_FRAME (box));
        make_label_bold (CTK_LABEL (label));
        ctk_label_set_use_underline (CTK_LABEL (label), TRUE);
        ctk_frame_set_shadow_type (CTK_FRAME (box), CTK_SHADOW_NONE);
        ctk_box_pack_start (CTK_BOX (self->priv->input_box), box, TRUE, TRUE, 0);

        self->priv->input_treeview =
                create_stream_treeview (self, G_CALLBACK (on_input_radio_toggled));

        ctk_label_set_mnemonic_widget (CTK_LABEL (label), self->priv->input_treeview);

        scroll_box = ctk_scrolled_window_new (NULL, NULL);
        ctk_widget_set_margin_top (scroll_box, 6);
        ctk_scrolled_window_set_policy (CTK_SCROLLED_WINDOW (scroll_box),
                                        CTK_POLICY_NEVER,
                                        CTK_POLICY_AUTOMATIC);
        ctk_scrolled_window_set_shadow_type (CTK_SCROLLED_WINDOW (scroll_box),
                                             CTK_SHADOW_IN);
        ctk_container_add (CTK_CONTAINER (scroll_box), self->priv->input_treeview);
        ctk_container_add (CTK_CONTAINER (box), scroll_box);

        selection = ctk_tree_view_get_selection (CTK_TREE_VIEW (self->priv->input_treeview));
        ctk_tree_selection_set_mode (selection, CTK_SELECTION_SINGLE);

        /* Output page */
        self->priv->output_box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 12);
        ctk_container_set_border_width (CTK_CONTAINER (self->priv->output_box), 12);
        label = ctk_label_new (_("Output"));
        ctk_notebook_append_page (CTK_NOTEBOOK (self->priv->notebook),
                                  self->priv->output_box,
                                  label);

        box = ctk_frame_new (_("C_hoose a device for sound output:"));
        label = ctk_frame_get_label_widget (CTK_FRAME (box));
        make_label_bold (CTK_LABEL (label));
        ctk_label_set_use_underline (CTK_LABEL (label), TRUE);
        ctk_frame_set_shadow_type (CTK_FRAME (box), CTK_SHADOW_NONE);
        ctk_box_pack_start (CTK_BOX (self->priv->output_box), box, TRUE, TRUE, 0);

        self->priv->output_treeview = create_stream_treeview (self,
                                                              G_CALLBACK (on_output_radio_toggled));
        ctk_label_set_mnemonic_widget (CTK_LABEL (label), self->priv->output_treeview);

        scroll_box = ctk_scrolled_window_new (NULL, NULL);
        ctk_widget_set_margin_top (scroll_box, 6);
        ctk_scrolled_window_set_policy (CTK_SCROLLED_WINDOW (scroll_box),
                                        CTK_POLICY_NEVER,
                                        CTK_POLICY_AUTOMATIC);
        ctk_scrolled_window_set_shadow_type (CTK_SCROLLED_WINDOW (scroll_box),
                                             CTK_SHADOW_IN);
        ctk_container_add (CTK_CONTAINER (scroll_box), self->priv->output_treeview);
        ctk_container_add (CTK_CONTAINER (box), scroll_box);

        selection = ctk_tree_view_get_selection (CTK_TREE_VIEW (self->priv->output_treeview));
        ctk_tree_selection_set_mode (selection, CTK_SELECTION_SINGLE);

        box = ctk_frame_new (_("Settings for the selected device:"));
        label = ctk_frame_get_label_widget (CTK_FRAME (box));
        make_label_bold (CTK_LABEL (label));
        ctk_frame_set_shadow_type (CTK_FRAME (box), CTK_SHADOW_NONE);
        ctk_box_pack_start (CTK_BOX (self->priv->output_box), box, FALSE, FALSE, 12);
        self->priv->output_settings_box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 0);
        ctk_container_add (CTK_CONTAINER (box), self->priv->output_settings_box);

        self->priv->output_settings_frame = box;

        /* Applications */
        self->priv->applications_window = ctk_scrolled_window_new (NULL, NULL);
        ctk_scrolled_window_set_policy (CTK_SCROLLED_WINDOW (self->priv->applications_window),
                                        CTK_POLICY_NEVER,
                                        CTK_POLICY_AUTOMATIC);

        ctk_scrolled_window_set_shadow_type (CTK_SCROLLED_WINDOW (self->priv->applications_window),
                                             CTK_SHADOW_NONE);

        self->priv->applications_box = ctk_box_new (CTK_ORIENTATION_VERTICAL, 12);
        ctk_container_set_border_width (CTK_CONTAINER (self->priv->applications_box), 12);

        ctk_container_add (CTK_CONTAINER (self->priv->applications_window),
                           self->priv->applications_box);

        label = ctk_label_new (_("Applications"));
        ctk_notebook_append_page (CTK_NOTEBOOK (self->priv->notebook),
                                  self->priv->applications_window,
                                  label);

        self->priv->no_apps_label = ctk_label_new (_("No application is currently playing or recording audio."));
        ctk_box_pack_start (CTK_BOX (self->priv->applications_box),
                            self->priv->no_apps_label,
                            TRUE, TRUE, 0);

        ctk_widget_show_all (main_vbox);

        list = cafe_mixer_context_list_streams (self->priv->context);
        while (list != NULL) {
                add_stream (self, CAFE_MIXER_STREAM (list->data));
                list = list->next;
        }

        list = cafe_mixer_context_list_devices (self->priv->context);
        while (list != NULL) {
                add_device (self, CAFE_MIXER_DEVICE (list->data));
                list = list->next;
        }

        selection = ctk_tree_view_get_selection (CTK_TREE_VIEW (self->priv->hw_treeview));

        /* Select the first device in the list */
        // XXX handle no devices
        if (ctk_tree_selection_get_selected (selection, NULL, NULL) == FALSE) {
                CtkTreeModel *model =
                        ctk_tree_view_get_model (CTK_TREE_VIEW (self->priv->hw_treeview));

                if (ctk_tree_model_get_iter_first (model, &iter))
                        ctk_tree_selection_select_iter (selection, &iter);
        }

        return object;
}

static CafeMixerContext *
gvc_mixer_dialog_get_context (GvcMixerDialog *dialog)
{
        return dialog->priv->context;
}

static void
gvc_mixer_dialog_set_context (GvcMixerDialog *dialog, CafeMixerContext *context)
{
        dialog->priv->context = g_object_ref (context);

        g_signal_connect (G_OBJECT (dialog->priv->context),
                          "stream-added",
                          G_CALLBACK (on_context_stream_added),
                          dialog);
        g_signal_connect (G_OBJECT (dialog->priv->context),
                          "stream-removed",
                          G_CALLBACK (on_context_stream_removed),
                          dialog);

        g_signal_connect (G_OBJECT (dialog->priv->context),
                          "device-added",
                          G_CALLBACK (on_context_device_added),
                          dialog);
        g_signal_connect (G_OBJECT (dialog->priv->context),
                          "device-removed",
                          G_CALLBACK (on_context_device_removed),
                          dialog);

        g_signal_connect (G_OBJECT (dialog->priv->context),
                          "notify::default-input-stream",
                          G_CALLBACK (on_context_default_input_stream_notify),
                          dialog);
        g_signal_connect (G_OBJECT (dialog->priv->context),
                          "notify::default-output-stream",
                          G_CALLBACK (on_context_default_output_stream_notify),
                          dialog);

        g_signal_connect (G_OBJECT (dialog->priv->context),
                          "stored-control-added",
                          G_CALLBACK (on_context_stored_control_added),
                          dialog);
        g_signal_connect (G_OBJECT (dialog->priv->context),
                          "stored-control-removed",
                          G_CALLBACK (on_context_stored_control_removed),
                          dialog);

        dialog->priv->backend_flags = cafe_mixer_context_get_backend_flags (context);

        g_object_notify (G_OBJECT (dialog), "context");
}

static void
gvc_mixer_dialog_set_property (GObject       *object,
                               guint          prop_id,
                               const GValue  *value,
                               GParamSpec    *pspec)
{
        GvcMixerDialog *self = GVC_MIXER_DIALOG (object);

        switch (prop_id) {
        case PROP_CONTEXT:
                gvc_mixer_dialog_set_context (self, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_mixer_dialog_get_property (GObject     *object,
                               guint        prop_id,
                               GValue      *value,
                               GParamSpec  *pspec)
{
        GvcMixerDialog *self = GVC_MIXER_DIALOG (object);

        switch (prop_id) {
        case PROP_CONTEXT:
                g_value_set_object (value, gvc_mixer_dialog_get_context (self));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_mixer_dialog_dispose (GObject *object)
{
        GvcMixerDialog *dialog = GVC_MIXER_DIALOG (object);

        if (dialog->priv->context != NULL) {
                g_signal_handlers_disconnect_by_data (G_OBJECT (dialog->priv->context),
                                                      dialog);

                g_clear_object (&dialog->priv->context);
        }

        G_OBJECT_CLASS (gvc_mixer_dialog_parent_class)->dispose (object);
}

static void
gvc_mixer_dialog_class_init (GvcMixerDialogClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = gvc_mixer_dialog_constructor;
        object_class->dispose = gvc_mixer_dialog_dispose;
        object_class->finalize = gvc_mixer_dialog_finalize;
        object_class->set_property = gvc_mixer_dialog_set_property;
        object_class->get_property = gvc_mixer_dialog_get_property;

        g_object_class_install_property (object_class,
                                         PROP_CONTEXT,
                                         g_param_spec_object ("context",
                                                              "Context",
                                                              "CafeMixer context",
                                                              CAFE_MIXER_TYPE_CONTEXT,
                                                              G_PARAM_READWRITE |
                                                              G_PARAM_CONSTRUCT_ONLY |
                                                              G_PARAM_STATIC_STRINGS));

        CtkWidgetClass *widget_class  = CTK_WIDGET_CLASS (klass);
        ctk_widget_class_set_css_name (widget_class, "GvcMixerDialog");
}

static void
gvc_mixer_dialog_init (GvcMixerDialog *dialog)
{
        dialog->priv = gvc_mixer_dialog_get_instance_private (dialog);

        dialog->priv->bars = g_hash_table_new (g_str_hash, g_str_equal);
        dialog->priv->size_group = ctk_size_group_new (CTK_SIZE_GROUP_HORIZONTAL);
}

static void
gvc_mixer_dialog_finalize (GObject *object)
{
        GvcMixerDialog *dialog;

        dialog = GVC_MIXER_DIALOG (object);

        g_hash_table_destroy (dialog->priv->bars);

        G_OBJECT_CLASS (gvc_mixer_dialog_parent_class)->finalize (object);
}

GvcMixerDialog *
gvc_mixer_dialog_new (CafeMixerContext *context)
{
        return g_object_new (GVC_TYPE_MIXER_DIALOG,
                             "icon-name", "multimedia-volume-control",
                             "title", _("Sound Preferences"),
                             "context", context,
                             NULL);
}

gboolean
gvc_mixer_dialog_set_page (GvcMixerDialog *self, const gchar *page)
{
        guint num = 0;

        g_return_val_if_fail (GVC_IS_MIXER_DIALOG (self), FALSE);

        if (page != NULL) {
                if (g_str_equal (page, "effects"))
                        num = PAGE_EFFECTS;
                else if (g_str_equal (page, "hardware"))
                        num = PAGE_HARDWARE;
                else if (g_str_equal (page, "input"))
                        num = PAGE_INPUT;
                else if (g_str_equal (page, "output"))
                        num = PAGE_OUTPUT;
                else if (g_str_equal (page, "applications"))
                        num = PAGE_APPLICATIONS;
        }

        ctk_notebook_set_current_page (CTK_NOTEBOOK (self->priv->notebook), num);

        return TRUE;
}

static gboolean
dialog_page_scroll_event_cb (CtkWidget      *widget,
                             GdkEventScroll *event,
                             CtkWindow      *window)
{
        CtkNotebook *notebook = CTK_NOTEBOOK (widget);
        CtkWidget *child, *event_widget, *action_widget;

        child = ctk_notebook_get_nth_page (notebook, ctk_notebook_get_current_page (notebook));
        if (child == NULL)
                return FALSE;

        event_widget = ctk_get_event_widget ((GdkEvent *) event);

        /* Ignore scroll events from the content of the page */
        if (event_widget == NULL ||
            event_widget == child ||
            ctk_widget_is_ancestor (event_widget, child))
                return FALSE;

        /* And also from the action widgets */
        action_widget = ctk_notebook_get_action_widget (notebook, CTK_PACK_START);
        if (event_widget == action_widget ||
            (action_widget != NULL && ctk_widget_is_ancestor (event_widget, action_widget)))
                return FALSE;
        action_widget = ctk_notebook_get_action_widget (notebook, CTK_PACK_END);
        if (event_widget == action_widget ||
            (action_widget != NULL && ctk_widget_is_ancestor (event_widget, action_widget)))
                return FALSE;

        switch (event->direction) {

                case GDK_SCROLL_RIGHT:
                case GDK_SCROLL_DOWN:
                        ctk_notebook_next_page (notebook);
                        break;
                case GDK_SCROLL_LEFT:
                case GDK_SCROLL_UP:
                        ctk_notebook_prev_page (notebook);
                        break;

                case GDK_SCROLL_SMOOTH:
                        switch (ctk_notebook_get_tab_pos (notebook)) {
                            case CTK_POS_LEFT:
                            case CTK_POS_RIGHT:
                                    if (event->delta_y > 0)
                                        ctk_notebook_next_page (notebook);
                                    else if (event->delta_y < 0)
                                        ctk_notebook_prev_page (notebook);
                                    break;
                            case CTK_POS_TOP:
                            case CTK_POS_BOTTOM:
                                    if (event->delta_x > 0)
                                        ctk_notebook_next_page (notebook);
                                    else if (event->delta_x < 0)
                                        ctk_notebook_prev_page (notebook);
                                    break;
                        }
                        break;
        }

        return TRUE;
}
