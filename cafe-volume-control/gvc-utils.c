/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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

#include <libcafemixer/cafemixer.h>

#include "gvc-utils.h"

/* libkanberra requires a PulseAudio channel name to be given to its
 * KA_PROP_KANBERRA_FORCE_CHANNEL property.
 *
 * The strings here are copied from PulseAudio source code to avoid depending
 * on libpulse. */
static const gchar *pulse_position[CAFE_MIXER_CHANNEL_MAX] = {
        [CAFE_MIXER_CHANNEL_MONO] = "mono",
        [CAFE_MIXER_CHANNEL_FRONT_LEFT] = "front-left",
        [CAFE_MIXER_CHANNEL_FRONT_RIGHT] = "front-right",
        [CAFE_MIXER_CHANNEL_FRONT_CENTER] = "front-center",
        [CAFE_MIXER_CHANNEL_LFE] = "lfe",
        [CAFE_MIXER_CHANNEL_BACK_LEFT] = "rear-left",
        [CAFE_MIXER_CHANNEL_BACK_RIGHT] = "rear-right",
        [CAFE_MIXER_CHANNEL_BACK_CENTER] = "rear-center",
        [CAFE_MIXER_CHANNEL_FRONT_LEFT_CENTER] = "front-left-of-center",
        [CAFE_MIXER_CHANNEL_FRONT_RIGHT_CENTER] = "front-right-of-center",
        [CAFE_MIXER_CHANNEL_SIDE_LEFT] = "side-left",
        [CAFE_MIXER_CHANNEL_SIDE_RIGHT] = "side-right",
        [CAFE_MIXER_CHANNEL_TOP_FRONT_LEFT] = "top-front-left",
        [CAFE_MIXER_CHANNEL_TOP_FRONT_RIGHT] = "top-front-right",
        [CAFE_MIXER_CHANNEL_TOP_FRONT_CENTER] = "top-front-center",
        [CAFE_MIXER_CHANNEL_TOP_CENTER] = "top-center",
        [CAFE_MIXER_CHANNEL_TOP_BACK_LEFT] = "top-rear-left",
        [CAFE_MIXER_CHANNEL_TOP_BACK_RIGHT] = "top-rear-right",
        [CAFE_MIXER_CHANNEL_TOP_BACK_CENTER] = "top-rear-center"
};

static const gchar *pretty_position[CAFE_MIXER_CHANNEL_MAX] = {
        [CAFE_MIXER_CHANNEL_UNKNOWN] = N_("Unknown"),
        /* Speaker channel names */
        [CAFE_MIXER_CHANNEL_MONO] = N_("Mono"),
        [CAFE_MIXER_CHANNEL_FRONT_LEFT] = N_("Front Left"),
        [CAFE_MIXER_CHANNEL_FRONT_RIGHT] = N_("Front Right"),
        [CAFE_MIXER_CHANNEL_FRONT_CENTER] = N_("Front Center"),
        [CAFE_MIXER_CHANNEL_LFE] = N_("LFE"),
        [CAFE_MIXER_CHANNEL_BACK_LEFT] = N_("Rear Left"),
        [CAFE_MIXER_CHANNEL_BACK_RIGHT] = N_("Rear Right"),
        [CAFE_MIXER_CHANNEL_BACK_CENTER] = N_("Rear Center"),
        [CAFE_MIXER_CHANNEL_FRONT_LEFT_CENTER] = N_("Front Left of Center"),
        [CAFE_MIXER_CHANNEL_FRONT_RIGHT_CENTER] = N_("Front Right of Center"),
        [CAFE_MIXER_CHANNEL_SIDE_LEFT] = N_("Side Left"),
        [CAFE_MIXER_CHANNEL_SIDE_RIGHT] = N_("Side Right"),
        [CAFE_MIXER_CHANNEL_TOP_FRONT_LEFT] = N_("Top Front Left"),
        [CAFE_MIXER_CHANNEL_TOP_FRONT_RIGHT] = N_("Top Front Right"),
        [CAFE_MIXER_CHANNEL_TOP_FRONT_CENTER] = N_("Top Front Center"),
        [CAFE_MIXER_CHANNEL_TOP_CENTER] = N_("Top Center"),
        [CAFE_MIXER_CHANNEL_TOP_BACK_LEFT] = N_("Top Rear Left"),
        [CAFE_MIXER_CHANNEL_TOP_BACK_RIGHT] = N_("Top Rear Right"),
        [CAFE_MIXER_CHANNEL_TOP_BACK_CENTER] = N_("Top Rear Center")
};

const gchar *
gvc_channel_position_to_pulse_string (CafeMixerChannelPosition position)
{
        g_return_val_if_fail (position >= 0 && position < CAFE_MIXER_CHANNEL_MAX, NULL);

        return pulse_position[position];
}

const gchar *
gvc_channel_position_to_pretty_string (CafeMixerChannelPosition position)
{
        g_return_val_if_fail (position >= 0 && position < CAFE_MIXER_CHANNEL_MAX, NULL);

        return pretty_position[position];
}

const gchar *
gvc_channel_map_to_pretty_string (CafeMixerStreamControl *control)
{
        g_return_val_if_fail (CAFE_MIXER_IS_STREAM_CONTROL (control), NULL);

#define HAS_POSITION(p) (cafe_mixer_stream_control_has_channel_position (control, (p)))

        /* Modeled after PulseAudio 5.0, probably could be extended with other combinations */
        switch (cafe_mixer_stream_control_get_num_channels (control)) {
        case 1:
                if (HAS_POSITION (CAFE_MIXER_CHANNEL_MONO))
                        return _("Mono");
                break;
        case 2:
                if (HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_RIGHT))
                        return _("Stereo");
                break;
        case 4:
                if (HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_RIGHT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_BACK_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_BACK_RIGHT))
                        return _("Surround 4.0");
                break;
        case 5:
                if (HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_RIGHT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_BACK_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_BACK_RIGHT)) {
                        if (HAS_POSITION (CAFE_MIXER_CHANNEL_LFE))
                                return _("Surround 4.1");
                        if (HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_CENTER))
                                return _("Surround 5.0");
                }
                break;
        case 6:
                if (HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_RIGHT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_CENTER) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_BACK_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_BACK_RIGHT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_LFE))
                        return _("Surround 5.1");
                break;
        case 8:
                if (HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_RIGHT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_FRONT_CENTER) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_BACK_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_BACK_RIGHT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_SIDE_LEFT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_SIDE_RIGHT) &&
                    HAS_POSITION (CAFE_MIXER_CHANNEL_LFE))
                        return _("Surround 7.1");
                break;
        }

#undef HAS_POSITION

        return NULL;
}
