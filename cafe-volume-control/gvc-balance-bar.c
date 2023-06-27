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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <ctk/ctk.h>

#include <canberra-ctk.h>
#include <libcafemixer/cafemixer.h>

#include "gvc-balance-bar.h"

#define BALANCE_BAR_STYLE                                       \
        "style \"balance-bar-scale-style\" {\n"                 \
        " CtkScale::trough-side-details = 0\n"                  \
        "}\n"                                                   \
        "widget \"*.balance-bar-scale\" style : rc \"balance-bar-scale-style\"\n"

#define SCALE_SIZE 128

struct _GvcBalanceBarPrivate
{
        GvcBalanceType   btype;
        CtkWidget       *scale_box;
        CtkWidget       *start_box;
        CtkWidget       *end_box;
        CtkWidget       *label;
        CtkWidget       *scale;
        CtkAdjustment   *adjustment;
        CtkSizeGroup    *size_group;
        gboolean         symmetric;
        CafeMixerStreamControl *control;
        gint             lfe_channel;
};

enum
{
        PROP_0,
        PROP_CONTROL,
        PROP_BALANCE_TYPE,
        N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

static void     gvc_balance_bar_dispose     (GObject            *object);

static gboolean on_scale_scroll_event       (CtkWidget          *widget,
                                             GdkEventScroll     *event,
                                             GvcBalanceBar      *bar);

static void     on_adjustment_value_changed (CtkAdjustment      *adjustment,
                                             GvcBalanceBar      *bar);

G_DEFINE_TYPE_WITH_PRIVATE (GvcBalanceBar, gvc_balance_bar, CTK_TYPE_BOX)

static void
create_scale_box (GvcBalanceBar *bar)
{
        bar->priv->scale_box = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 6);
        bar->priv->start_box = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 6);
        bar->priv->end_box   = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 6);
        bar->priv->scale     = ctk_scale_new (CTK_ORIENTATION_HORIZONTAL,
                                              bar->priv->adjustment);

        /* Balance and fade scales do not have an origin */
        if (bar->priv->btype != BALANCE_TYPE_LFE)
                ctk_scale_set_has_origin (CTK_SCALE (bar->priv->scale), FALSE);

        ctk_widget_set_size_request (bar->priv->scale, SCALE_SIZE, -1);

        ctk_box_pack_start (CTK_BOX (bar->priv->scale_box),
                            bar->priv->start_box,
                            FALSE, FALSE, 0);
        ctk_box_pack_start (CTK_BOX (bar->priv->start_box),
                            bar->priv->label,
                            FALSE, FALSE, 0);
        ctk_box_pack_start (CTK_BOX (bar->priv->scale_box),
                            bar->priv->scale,
                            TRUE, TRUE, 0);
        ctk_box_pack_start (CTK_BOX (bar->priv->scale_box),
                            bar->priv->end_box,
                            FALSE, FALSE, 0);

        ca_ctk_widget_disable_sounds (bar->priv->scale, FALSE);

        ctk_widget_add_events (bar->priv->scale, GDK_SCROLL_MASK);

        g_signal_connect (G_OBJECT (bar->priv->scale),
                          "scroll-event",
                          G_CALLBACK (on_scale_scroll_event),
                          bar);

        if (bar->priv->size_group != NULL) {
                ctk_size_group_add_widget (bar->priv->size_group,
                                           bar->priv->start_box);

                if (bar->priv->symmetric)
                        ctk_size_group_add_widget (bar->priv->size_group,
                                                   bar->priv->end_box);
        }

        ctk_scale_set_draw_value (CTK_SCALE (bar->priv->scale), FALSE);
}

static void
update_scale_marks (GvcBalanceBar *bar)
{
        gchar    *str_lower = NULL,
                 *str_upper = NULL;
        gdouble   lower,
                  upper;

        ctk_scale_clear_marks (CTK_SCALE (bar->priv->scale));

        switch (bar->priv->btype) {
        case BALANCE_TYPE_RL:
                str_lower = g_strdup_printf ("<small>%s</small>", C_("balance", "Left"));
                str_upper = g_strdup_printf ("<small>%s</small>", C_("balance", "Right"));
                break;
        case BALANCE_TYPE_FR:
                str_lower = g_strdup_printf ("<small>%s</small>", C_("balance", "Rear"));
                str_upper = g_strdup_printf ("<small>%s</small>", C_("balance", "Front"));
                break;
        case BALANCE_TYPE_LFE:
                str_lower = g_strdup_printf ("<small>%s</small>", C_("balance", "Minimum"));
                str_upper = g_strdup_printf ("<small>%s</small>", C_("balance", "Maximum"));
                break;
        }

        lower = ctk_adjustment_get_lower (bar->priv->adjustment);
        ctk_scale_add_mark (CTK_SCALE (bar->priv->scale),
                            lower,
                            CTK_POS_BOTTOM,
                            str_lower);
        upper = ctk_adjustment_get_upper (bar->priv->adjustment);
        ctk_scale_add_mark (CTK_SCALE (bar->priv->scale),
                            upper,
                            CTK_POS_BOTTOM,
                            str_upper);
        g_free (str_lower);
        g_free (str_upper);

        if (bar->priv->btype != BALANCE_TYPE_LFE)
                ctk_scale_add_mark (CTK_SCALE (bar->priv->scale),
                                    (upper - lower) / 2 + lower,
                                    CTK_POS_BOTTOM,
                                    NULL);
}

void
gvc_balance_bar_set_size_group (GvcBalanceBar *bar,
                                CtkSizeGroup  *group,
                                gboolean       symmetric)
{
        g_return_if_fail (GVC_IS_BALANCE_BAR (bar));
        g_return_if_fail (CTK_IS_SIZE_GROUP (group));

        bar->priv->size_group = group;
        bar->priv->symmetric = symmetric;

        if (bar->priv->size_group != NULL) {
                ctk_size_group_add_widget (bar->priv->size_group,
                                           bar->priv->start_box);

                if (bar->priv->symmetric)
                        ctk_size_group_add_widget (bar->priv->size_group,
                                                   bar->priv->end_box);
        }
        ctk_widget_queue_draw (CTK_WIDGET (bar));
}

static void
update_balance_value (GvcBalanceBar *bar)
{
        gdouble value = 0;

        switch (bar->priv->btype) {
        case BALANCE_TYPE_RL:
                value = cafe_mixer_stream_control_get_balance (bar->priv->control);
                g_debug ("Balance value changed to %.2f", value);
                break;
        case BALANCE_TYPE_FR:
                value = cafe_mixer_stream_control_get_fade (bar->priv->control);
                g_debug ("Fade value changed to %.2f", value);
                break;
        case BALANCE_TYPE_LFE:
                value = cafe_mixer_stream_control_get_channel_volume (bar->priv->control,
                                                                      bar->priv->lfe_channel);

                g_debug ("Subwoofer volume changed to %.0f", value);
                break;
        }

        ctk_adjustment_set_value (bar->priv->adjustment, value);
}

static void
on_balance_value_changed (CafeMixerStream *stream,
                          GParamSpec      *pspec,
                          GvcBalanceBar   *bar)
{
        update_balance_value (bar);
}

static gint
find_stream_lfe_channel (CafeMixerStreamControl *control)
{
        guint i;

        for (i = 0; i < cafe_mixer_stream_control_get_num_channels (control); i++) {
                CafeMixerChannelPosition position;

                position = cafe_mixer_stream_control_get_channel_position (control, i);
                if (position == CAFE_MIXER_CHANNEL_LFE)
                        return i;
        }

        return -1;
}

static void
gvc_balance_bar_set_control (GvcBalanceBar *bar, CafeMixerStreamControl *control)
{
        g_return_if_fail (GVC_BALANCE_BAR (bar));
        g_return_if_fail (CAFE_MIXER_IS_STREAM_CONTROL (control));

        if (bar->priv->control != NULL) {
                g_signal_handlers_disconnect_by_func (G_OBJECT (bar->priv->control),
                                                      on_balance_value_changed,
                                                      bar);
                g_object_unref (bar->priv->control);
        }

        bar->priv->control = g_object_ref (control);

        if (bar->priv->btype == BALANCE_TYPE_LFE) {
                gdouble minimum;
                gdouble maximum;

                minimum = cafe_mixer_stream_control_get_min_volume (bar->priv->control);
                maximum = cafe_mixer_stream_control_get_normal_volume (bar->priv->control);

                /* Configure the adjustment for the volume limits of the current
                 * stream.
                 * Only subwoofer scale uses volume, balance and fade use fixed
                 * limits which do not need to be updated as balance type is
                 * only set during construction. */
                ctk_adjustment_configure (CTK_ADJUSTMENT (bar->priv->adjustment),
                                          ctk_adjustment_get_value (bar->priv->adjustment),
                                          minimum,
                                          maximum,
                                          (maximum - minimum) / 100.0,
                                          (maximum - minimum) / 10.0,
                                          0.0);

                bar->priv->lfe_channel = find_stream_lfe_channel (bar->priv->control);

                if (G_LIKELY (bar->priv->lfe_channel > -1))
                        g_debug ("Found LFE channel at position %d", bar->priv->lfe_channel);
                else
                        g_warn_if_reached ();
        } else
                bar->priv->lfe_channel = -1;

        switch (bar->priv->btype) {
        case BALANCE_TYPE_RL:
                g_signal_connect (G_OBJECT (bar->priv->control),
                                  "notify::balance",
                                  G_CALLBACK (on_balance_value_changed),
                                  bar);
                break;
        case BALANCE_TYPE_FR:
                g_signal_connect (G_OBJECT (bar->priv->control),
                                  "notify::fade",
                                  G_CALLBACK (on_balance_value_changed),
                                  bar);
                break;
        case BALANCE_TYPE_LFE:
                g_signal_connect (G_OBJECT (bar->priv->control),
                                  "notify::volume",
                                  G_CALLBACK (on_balance_value_changed),
                                  bar);
                break;
        }

        update_balance_value (bar);
        update_scale_marks (bar);

        g_object_notify_by_pspec (G_OBJECT (bar), properties[PROP_CONTROL]);
}

static void
gvc_balance_bar_set_balance_type (GvcBalanceBar *bar, GvcBalanceType btype)
{
        CtkWidget     *frame;
        CtkAdjustment *adjustment;

        /* Create adjustment with limits for balance and fade types because
         * some limits must be provided.
         * If subwoofer type is used instead, the limits will be changed when
         * stream is set. */
        adjustment = CTK_ADJUSTMENT (ctk_adjustment_new (0.0, -1.0, 1.0, 0.05, 0.5, 0.0));

        bar->priv->btype = btype;
        bar->priv->adjustment = CTK_ADJUSTMENT (g_object_ref_sink (adjustment));

        g_signal_connect (G_OBJECT (adjustment),
                          "value-changed",
                          G_CALLBACK (on_adjustment_value_changed),
                          bar);

        switch (btype) {
        case BALANCE_TYPE_RL:
                bar->priv->label = ctk_label_new_with_mnemonic (_("_Balance:"));
                break;
        case BALANCE_TYPE_FR:
                bar->priv->label = ctk_label_new_with_mnemonic (_("_Fade:"));
                break;
        case BALANCE_TYPE_LFE:
                bar->priv->label = ctk_label_new_with_mnemonic (_("_Subwoofer:"));
                break;
        }

        ctk_label_set_xalign (CTK_LABEL (bar->priv->label), 0.0);
        ctk_label_set_yalign (CTK_LABEL (bar->priv->label), 0.0);

        /* Frame */
        frame = ctk_frame_new (NULL);
        ctk_frame_set_shadow_type (CTK_FRAME (frame), CTK_SHADOW_NONE);
        ctk_box_pack_start (CTK_BOX (bar), frame, TRUE, TRUE, 0);

        /* Box with scale */
        create_scale_box (bar);
        ctk_container_add (CTK_CONTAINER (frame), bar->priv->scale_box);

        ctk_label_set_mnemonic_widget (CTK_LABEL (bar->priv->label),
                                       bar->priv->scale);

        ctk_widget_set_direction (bar->priv->scale, CTK_TEXT_DIR_LTR);
        ctk_widget_show_all (frame);

        g_object_notify_by_pspec (G_OBJECT (bar), properties[PROP_BALANCE_TYPE]);
}

static void
gvc_balance_bar_set_property (GObject       *object,
                              guint          prop_id,
                              const GValue  *value,
                              GParamSpec    *pspec)
{
        GvcBalanceBar *self = GVC_BALANCE_BAR (object);

        switch (prop_id) {
        case PROP_CONTROL:
                gvc_balance_bar_set_control (self, g_value_get_object (value));
                break;
        case PROP_BALANCE_TYPE:
                gvc_balance_bar_set_balance_type (self, g_value_get_int (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_balance_bar_get_property (GObject     *object,
                              guint        prop_id,
                              GValue      *value,
                              GParamSpec  *pspec)
{
        GvcBalanceBar *self = GVC_BALANCE_BAR (object);

        switch (prop_id) {
        case PROP_CONTROL:
                g_value_set_object (value, self->priv->control);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_balance_bar_class_init (GvcBalanceBarClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->dispose = gvc_balance_bar_dispose;
        object_class->set_property = gvc_balance_bar_set_property;
        object_class->get_property = gvc_balance_bar_get_property;

        properties[PROP_CONTROL] =
                g_param_spec_object ("control",
                                     "Control",
                                     "CafeMixer stream control",
                                     CAFE_MIXER_TYPE_STREAM_CONTROL,
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_STRINGS);

        properties[PROP_BALANCE_TYPE] =
                g_param_spec_int ("balance-type",
                                  "balance type",
                                  "Whether the balance is right-left or front-rear",
                                  BALANCE_TYPE_RL,
                                  NUM_BALANCE_TYPES - 1,
                                  BALANCE_TYPE_RL,
                                  G_PARAM_READWRITE |
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_STATIC_STRINGS);

        g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

static gboolean
on_scale_scroll_event (CtkWidget      *widget,
                       GdkEventScroll *event,
                       GvcBalanceBar  *bar)
{
        gdouble value;
        gdouble minimum;
        gdouble maximum;
        gdouble step;

        value   = ctk_adjustment_get_value (bar->priv->adjustment);
        minimum = ctk_adjustment_get_lower (bar->priv->adjustment);
        maximum = ctk_adjustment_get_upper (bar->priv->adjustment);

        // XXX fix this for CTK3

        if (bar->priv->btype == BALANCE_TYPE_LFE)
                step = (maximum - minimum) / 100.0;
        else
                step = 0.05;

        if (event->direction == GDK_SCROLL_UP) {
                if (value + step > maximum)
                        value = maximum;
                else
                        value = value + step;
        } else if (event->direction == GDK_SCROLL_DOWN) {
                if (value - step < minimum)
                        value = minimum;
                else
                        value = value - step;
        }

        ctk_adjustment_set_value (bar->priv->adjustment, value);
        return TRUE;
}

static void
on_adjustment_value_changed (CtkAdjustment *adjustment, GvcBalanceBar *bar)
{
        gdouble value;

        if (bar->priv->control == NULL)
                return;

        value = ctk_adjustment_get_value (adjustment);

        switch (bar->priv->btype) {
        case BALANCE_TYPE_RL:
                cafe_mixer_stream_control_set_balance (bar->priv->control, value);
                break;
        case BALANCE_TYPE_FR:
                cafe_mixer_stream_control_set_fade (bar->priv->control, value);
                break;
        case BALANCE_TYPE_LFE:
                cafe_mixer_stream_control_set_channel_volume (bar->priv->control,
                                                      bar->priv->lfe_channel,
                                                      value);
                break;
        }
}

static void
gvc_balance_bar_init (GvcBalanceBar *bar)
{
        bar->priv = gvc_balance_bar_get_instance_private (bar);
}

static void
gvc_balance_bar_dispose (GObject *object)
{
        GvcBalanceBar *bar;

        bar = GVC_BALANCE_BAR (object);

        if (bar->priv->control != NULL) {
                g_signal_handlers_disconnect_by_func (G_OBJECT (bar->priv->control),
                                                      on_balance_value_changed,
                                                      bar);
                g_clear_object (&bar->priv->control);
        }

        G_OBJECT_CLASS (gvc_balance_bar_parent_class)->dispose (object);
}

CtkWidget *
gvc_balance_bar_new (CafeMixerStreamControl *control, GvcBalanceType btype)
{
        return g_object_new (GVC_TYPE_BALANCE_BAR,
                            "balance-type", btype,
                            "control", control,
                            "orientation", CTK_ORIENTATION_HORIZONTAL,
                            NULL);
}
