/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Bastien Nocera
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
#include <ctk/ctk.h>

#include <kanberra.h>
#include <libcafemixer/cafemixer.h>

#include "gvc-speaker-test.h"
#include "gvc-utils.h"

struct _GvcSpeakerTestPrivate
{
        GArray           *controls;
        ca_context       *kanberra;
        CafeMixerStream  *stream;
};

enum {
        PROP_0,
        PROP_STREAM,
        N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL, };

static void gvc_speaker_test_dispose    (GObject             *object);
static void gvc_speaker_test_finalize   (GObject             *object);

G_DEFINE_TYPE_WITH_PRIVATE (GvcSpeakerTest, gvc_speaker_test, CTK_TYPE_GRID)

typedef struct {
        CafeMixerChannelPosition position;
        guint left;
        guint top;
} TablePosition;

static const TablePosition positions[] = {
        /* Position, X, Y */
        { CAFE_MIXER_CHANNEL_FRONT_LEFT, 0, 0, },
        { CAFE_MIXER_CHANNEL_FRONT_LEFT_CENTER, 1, 0, },
        { CAFE_MIXER_CHANNEL_FRONT_CENTER, 2, 0, },
        { CAFE_MIXER_CHANNEL_MONO, 2, 0, },
        { CAFE_MIXER_CHANNEL_FRONT_RIGHT_CENTER, 3, 0, },
        { CAFE_MIXER_CHANNEL_FRONT_RIGHT, 4, 0, },
        { CAFE_MIXER_CHANNEL_SIDE_LEFT, 0, 1, },
        { CAFE_MIXER_CHANNEL_SIDE_RIGHT, 4, 1, },
        { CAFE_MIXER_CHANNEL_BACK_LEFT, 0, 2, },
        { CAFE_MIXER_CHANNEL_BACK_CENTER, 2, 2, },
        { CAFE_MIXER_CHANNEL_BACK_RIGHT, 4, 2, },
        { CAFE_MIXER_CHANNEL_LFE, 3, 2 }
};

CafeMixerStream *
gvc_speaker_test_get_stream (GvcSpeakerTest *test)
{
        g_return_val_if_fail (GVC_IS_SPEAKER_TEST (test), NULL);

        return test->priv->stream;
}

static void
gvc_speaker_test_set_stream (GvcSpeakerTest *test, CafeMixerStream *stream)
{
        CafeMixerStreamControl *control;
        const gchar            *name;
        guint                   i;

        name = cafe_mixer_stream_get_name (stream);
        control = cafe_mixer_stream_get_default_control (stream);

        ca_context_change_device (test->priv->kanberra, name);

        for (i = 0; i < G_N_ELEMENTS (positions); i++) {
                gboolean has_position =
                        cafe_mixer_stream_control_has_channel_position (control, positions[i].position);

                ctk_widget_set_visible (g_array_index (test->priv->controls, CtkWidget *, i),
                                        has_position);
        }

        test->priv->stream = g_object_ref (stream);
}

static void
gvc_speaker_test_set_property (GObject       *object,
                               guint          prop_id,
                               const GValue  *value,
                               GParamSpec    *pspec)
{
        GvcSpeakerTest *self = GVC_SPEAKER_TEST (object);

        switch (prop_id) {
        case PROP_STREAM:
                gvc_speaker_test_set_stream (self, g_value_get_object (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_speaker_test_get_property (GObject     *object,
                               guint        prop_id,
                               GValue      *value,
                               GParamSpec  *pspec)
{
        GvcSpeakerTest *self = GVC_SPEAKER_TEST (object);

        switch (prop_id) {
        case PROP_STREAM:
                g_value_set_object (value, self->priv->stream);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gvc_speaker_test_class_init (GvcSpeakerTestClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->dispose  = gvc_speaker_test_dispose;
        object_class->finalize = gvc_speaker_test_finalize;
        object_class->set_property = gvc_speaker_test_set_property;
        object_class->get_property = gvc_speaker_test_get_property;

        properties[PROP_STREAM] =
                g_param_spec_object ("stream",
                                     "Stream",
                                     "CafeMixer stream",
                                     CAFE_MIXER_TYPE_STREAM,
                                     G_PARAM_READWRITE |
                                     G_PARAM_CONSTRUCT_ONLY |
                                     G_PARAM_STATIC_STRINGS);

        g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

static const gchar *
sound_name (CafeMixerChannelPosition position)
{
        switch (position) {
        case CAFE_MIXER_CHANNEL_FRONT_LEFT:
                return "audio-channel-front-left";
        case CAFE_MIXER_CHANNEL_FRONT_RIGHT:
                return "audio-channel-front-right";
        case CAFE_MIXER_CHANNEL_FRONT_CENTER:
                return "audio-channel-front-center";
        case CAFE_MIXER_CHANNEL_BACK_LEFT:
                return "audio-channel-rear-left";
        case CAFE_MIXER_CHANNEL_BACK_RIGHT:
                return "audio-channel-rear-right";
        case CAFE_MIXER_CHANNEL_BACK_CENTER:
                return "audio-channel-rear-center";
        case CAFE_MIXER_CHANNEL_LFE:
                return "audio-channel-lfe";
        case CAFE_MIXER_CHANNEL_SIDE_LEFT:
                return "audio-channel-side-left";
        case CAFE_MIXER_CHANNEL_SIDE_RIGHT:
                return "audio-channel-side-right";
        default:
                return NULL;
        }
}

static const gchar *
icon_name (CafeMixerChannelPosition position, gboolean playing)
{
        switch (position) {
        case CAFE_MIXER_CHANNEL_FRONT_LEFT:
                return playing
                        ? "audio-speaker-left-testing"
                        : "audio-speaker-left";
        case CAFE_MIXER_CHANNEL_FRONT_RIGHT:
                return playing
                        ? "audio-speaker-right-testing"
                        : "audio-speaker-right";
        case CAFE_MIXER_CHANNEL_FRONT_CENTER:
                return playing
                        ? "audio-speaker-center-testing"
                        : "audio-speaker-center";
        case CAFE_MIXER_CHANNEL_BACK_LEFT:
                return playing
                        ? "audio-speaker-left-back-testing"
                        : "audio-speaker-left-back";
        case CAFE_MIXER_CHANNEL_BACK_RIGHT:
                return playing
                        ? "audio-speaker-right-back-testing"
                        : "audio-speaker-right-back";
        case CAFE_MIXER_CHANNEL_BACK_CENTER:
                return playing
                        ? "audio-speaker-center-back-testing"
                        : "audio-speaker-center-back";
        case CAFE_MIXER_CHANNEL_LFE:
                return playing
                        ? "audio-subwoofer-testing"
                        : "audio-subwoofer";
        case CAFE_MIXER_CHANNEL_SIDE_LEFT:
                return playing
                        ? "audio-speaker-left-side-testing"
                        : "audio-speaker-left-side";
        case CAFE_MIXER_CHANNEL_SIDE_RIGHT:
                return playing
                        ? "audio-speaker-right-side-testing"
                        : "audio-speaker-right-side";
        default:
                return NULL;
        }
}

static void
update_button (CtkWidget *control)
{
        CtkWidget *button;
        CtkWidget *image;
        gboolean   playing;
        CafeMixerChannelPosition position;

        button = g_object_get_data (G_OBJECT (control), "button");
        image  = g_object_get_data (G_OBJECT (control), "image");

        position = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (control), "position"));
        playing  = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (control), "playing"));

        ctk_button_set_label (CTK_BUTTON (button), playing ? _("Stop") : _("Test"));

        ctk_image_set_from_icon_name (CTK_IMAGE (image),
                                      icon_name (position, playing),
                                      CTK_ICON_SIZE_DIALOG);
}

static gboolean
idle_cb (CtkWidget *control)
{
        if (control != NULL) {
                /* This is called in the background thread, hence forward to main thread
                 * via idle callback */
                g_object_set_data (G_OBJECT (control), "playing", GINT_TO_POINTER (FALSE));

                update_button (control);
        }
        return FALSE;
}

static void
finish_cb (ca_context *c, uint32_t id, int error_code, void *userdata)
{
        CtkWidget *control = (CtkWidget *) userdata;

        if (error_code == CA_ERROR_DESTROYED || control == NULL)
                return;

        g_idle_add ((GSourceFunc) idle_cb, control);
}

static void
on_test_button_clicked (CtkButton *button, CtkWidget *control)
{
        gboolean    playing;
        ca_context *kanberra;

        kanberra = g_object_get_data (G_OBJECT (control), "kanberra");

        ca_context_cancel (kanberra, 1);

        playing = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (control), "playing"));

        if (playing) {
                g_object_set_data (G_OBJECT (control), "playing", GINT_TO_POINTER (FALSE));
        } else {
                CafeMixerChannelPosition position;
                const gchar *name;
                ca_proplist *proplist;

                position = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (control), "position"));

                ca_proplist_create (&proplist);
                ca_proplist_sets (proplist,
                                  CA_PROP_MEDIA_ROLE, "test");
                ca_proplist_sets (proplist,
                                  CA_PROP_MEDIA_NAME,
                                  gvc_channel_position_to_pretty_string (position));
                ca_proplist_sets (proplist,
                                  CA_PROP_KANBERRA_FORCE_CHANNEL,
                                  gvc_channel_position_to_pulse_string (position));

                ca_proplist_sets (proplist, CA_PROP_KANBERRA_ENABLE, "1");

                name = sound_name (position);
                if (name != NULL) {
                        ca_proplist_sets (proplist, CA_PROP_EVENT_ID, name);
                        playing = ca_context_play_full (kanberra, 1, proplist, finish_cb, control) >= 0;
                }

                if (!playing) {
                        ca_proplist_sets (proplist, CA_PROP_EVENT_ID, "audio-test-signal");
                        playing = ca_context_play_full (kanberra, 1, proplist, finish_cb, control) >= 0;
                }

                if (!playing) {
                        ca_proplist_sets(proplist, CA_PROP_EVENT_ID, "bell-window-system");
                        playing = ca_context_play_full (kanberra, 1, proplist, finish_cb, control) >= 0;
                }

                g_object_set_data (G_OBJECT (control), "playing", GINT_TO_POINTER (playing));
        }

        update_button (control);
}

static CtkWidget *
create_control (ca_context *kanberra, CafeMixerChannelPosition position)
{
        CtkWidget   *control;
        CtkWidget   *box;
        CtkWidget   *label;
        CtkWidget   *image;
        CtkWidget   *test_button;
        const gchar *name;

        control = ctk_box_new (CTK_ORIENTATION_VERTICAL, 6);
        box     = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 0);

        g_object_set_data (G_OBJECT (control), "playing", GINT_TO_POINTER (FALSE));
        g_object_set_data (G_OBJECT (control), "position", GINT_TO_POINTER (position));
        g_object_set_data (G_OBJECT (control), "kanberra", kanberra);

        name = icon_name (position, FALSE);
        if (name == NULL)
                name = "audio-volume-medium";

        image = ctk_image_new_from_icon_name (name, CTK_ICON_SIZE_DIALOG);
        g_object_set_data (G_OBJECT (control), "image", image);
        ctk_box_pack_start (CTK_BOX (control), image, FALSE, FALSE, 0);

        label = ctk_label_new (gvc_channel_position_to_pretty_string (position));
        ctk_box_pack_start (CTK_BOX (control), label, FALSE, FALSE, 0);

        test_button = ctk_button_new_with_label (_("Test"));
        g_signal_connect (G_OBJECT (test_button),
                          "clicked",
                          G_CALLBACK (on_test_button_clicked),
                          control);

        g_object_set_data (G_OBJECT (control), "button", test_button);

        ctk_box_pack_start (CTK_BOX (box), test_button, TRUE, FALSE, 0);
        ctk_box_pack_start (CTK_BOX (control), box, FALSE, FALSE, 0);

        ctk_widget_show_all (control);

        return control;
}

static void
create_controls (GvcSpeakerTest *test)
{
        guint i;

        for (i = 0; i < G_N_ELEMENTS (positions); i++) {
                CtkWidget *control = create_control (test->priv->kanberra, positions[i].position);

                ctk_grid_attach (CTK_GRID (test),
                                 control,
                                 positions[i].left,
                                 positions[i].top,
                                 1, 1);
                g_array_insert_val (test->priv->controls, i, control);
        }
}

static void
gvc_speaker_test_init (GvcSpeakerTest *test)
{
        CtkWidget *face;

        test->priv = gvc_speaker_test_get_instance_private (test);

        ctk_container_set_border_width (CTK_CONTAINER (test), 12);

        face = ctk_image_new_from_icon_name ("face-smile", CTK_ICON_SIZE_DIALOG);

        ctk_grid_attach (CTK_GRID (test),
                         face,
                         1, 1,
                         3, 1);


        ctk_grid_set_baseline_row (CTK_GRID (test), 1);
        ctk_widget_show (face);

        ca_context_create (&test->priv->kanberra);

        /* The test sounds are played for a single channel, set up using the
         * FORCE_CHANNEL property of libkanberra; this property is only supported
         * in the PulseAudio backend, so avoid other backends completely */
        ca_context_set_driver (test->priv->kanberra, "pulse");

        ca_context_change_props (test->priv->kanberra,
                                 CA_PROP_APPLICATION_ID, "org.cafe.VolumeControl",
                                 CA_PROP_APPLICATION_NAME, _("Volume Control"),
                                 CA_PROP_APPLICATION_VERSION, VERSION,
                                 CA_PROP_APPLICATION_ICON_NAME, "multimedia-volume-control",
                                 NULL);

        test->priv->controls = g_array_new (FALSE, FALSE, sizeof (CtkWidget *));

        create_controls (test);
}

static void
gvc_speaker_test_dispose (GObject *object)
{
        GvcSpeakerTest *test;

        test = GVC_SPEAKER_TEST (object);

        g_clear_object (&test->priv->stream);

        G_OBJECT_CLASS (gvc_speaker_test_parent_class)->dispose (object);
}

static void
gvc_speaker_test_finalize (GObject *object)
{
        GvcSpeakerTest *test;

        test = GVC_SPEAKER_TEST (object);

        ca_context_destroy (test->priv->kanberra);

        G_OBJECT_CLASS (gvc_speaker_test_parent_class)->finalize (object);
}

CtkWidget *
gvc_speaker_test_new (CafeMixerStream *stream)
{
        GObject *test;

        g_return_val_if_fail (CAFE_MIXER_IS_STREAM (stream), NULL);

        test = g_object_new (GVC_TYPE_SPEAKER_TEST,
                             "row-spacing", 6,
                             "column-spacing", 6,
                             "row-homogeneous", TRUE,
                             "column-homogeneous", TRUE,
                             "stream", stream,
                             NULL);

        return CTK_WIDGET (test);
}
