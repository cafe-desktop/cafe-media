/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Red Hat, Inc.
 * Copyright (C) 2014 Michal Ratajsky <michal.ratajsky@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>
#include <ctk/ctk.h>

#include <libintl.h>
#include <gio/gio.h>
#include <libcafemixer/cafemixer.h>

#include "gvc-mixer-dialog.h"

#define DIALOG_POPUP_TIMEOUT 3

static guint       popup_id = 0;
static gboolean    debug = FALSE;
static gboolean    show_version = FALSE;

static gchar      *page = NULL;
static CtkWidget  *app_dialog = NULL;
static CtkWidget  *warning_dialog = NULL;

static void
on_dialog_response (CtkDialog *dialog, guint response_id, gpointer data)
{
        gboolean destroy = GPOINTER_TO_INT (data);

        if (destroy == TRUE)
                ctk_widget_destroy (CTK_WIDGET (dialog));

        ctk_main_quit ();
}

static void
on_dialog_close (CtkDialog *dialog, gpointer data)
{
        gboolean destroy = GPOINTER_TO_INT (data);

        if (destroy == TRUE)
                ctk_widget_destroy (CTK_WIDGET (dialog));

        ctk_main_quit ();
}

static void
remove_warning_dialog (void)
{
        if (popup_id != 0) {
                g_source_remove (popup_id);
                popup_id = 0;
        }

        g_clear_pointer (&warning_dialog, ctk_widget_destroy);
}

static void
context_ready (CafeMixerContext *context, CtkApplication *application)
{
        /* The dialog might be already created, e.g. when reconnected
         * to a sound server */
        CtkApplication *app;
        GError *error = NULL;

        app = ctk_application_new ("context.ready", G_APPLICATION_DEFAULT_FLAGS);
        g_application_register (G_APPLICATION (app), NULL, &error);
        if (error != NULL)
        {
                g_warning ("%s", error->message);
                g_error_free (error);
                error = NULL;
        }

        if (g_application_get_is_remote (G_APPLICATION (app)))
        {
                g_application_activate (G_APPLICATION (app));
                g_object_unref (app);
                app_dialog = CTK_WIDGET (gvc_mixer_dialog_new (context));
                ctk_main_quit ();
        }

        if (app_dialog != NULL)
                return;

        app_dialog = CTK_WIDGET (gvc_mixer_dialog_new (context));

        g_signal_connect (G_OBJECT (app_dialog),
                          "response",
                          G_CALLBACK (on_dialog_response),
                          GINT_TO_POINTER (FALSE));
        g_signal_connect (G_OBJECT (app_dialog),
                          "close",
                          G_CALLBACK (on_dialog_close),
                          GINT_TO_POINTER (FALSE));

        gvc_mixer_dialog_set_page (GVC_MIXER_DIALOG (app_dialog), page);

        ctk_widget_show (app_dialog);

        g_signal_connect_swapped (app, "activate", G_CALLBACK (ctk_window_present), app_dialog);
}

static void
on_context_state_notify (CafeMixerContext *context,
                         GParamSpec       *pspec,
                         CtkApplication	  *app)
{
        CafeMixerState state = cafe_mixer_context_get_state (context);

        if (state == CAFE_MIXER_STATE_READY) {
                remove_warning_dialog ();
                context_ready (context, app);
        }
        else if (state == CAFE_MIXER_STATE_FAILED) {
                CtkWidget *dialog;

                remove_warning_dialog ();

                dialog = ctk_message_dialog_new (CTK_WINDOW (app_dialog),
                                                 0,
                                                 CTK_MESSAGE_ERROR,
                                                 CTK_BUTTONS_CLOSE,
                                                 _("Sound system is not available"));

                g_signal_connect (G_OBJECT (dialog),
                                  "response",
                                  G_CALLBACK (on_dialog_response),
                                  GINT_TO_POINTER (TRUE));
                g_signal_connect (G_OBJECT (dialog),
                                  "close",
                                  G_CALLBACK (on_dialog_close),
                                  GINT_TO_POINTER (TRUE));

                ctk_widget_show (dialog);
        }
}

static gboolean
dialog_popup_timeout (gpointer data)
{
	warning_dialog = ctk_message_dialog_new (CTK_WINDOW (app_dialog),
	                                         0,
	                                         CTK_MESSAGE_INFO,
	                                         CTK_BUTTONS_CANCEL,
	                                         _("Waiting for sound system to respond"));

	g_signal_connect (G_OBJECT (warning_dialog),
	                  "response",
                      G_CALLBACK (on_dialog_response),
                      GINT_TO_POINTER (TRUE));
	g_signal_connect (G_OBJECT (warning_dialog),
	                  "close",
                      G_CALLBACK (on_dialog_close),
                      GINT_TO_POINTER (TRUE));

	ctk_widget_show (warning_dialog);

	return FALSE;
}

int
main (int argc, char **argv)
{
        GError           *error = NULL;
        gchar            *backend = NULL;
        CafeMixerContext *context;
        GApplication	 *app;

        GOptionEntry      entries[] = {
                { "backend", 'b', 0, G_OPTION_ARG_STRING, &backend, N_("Sound system backend"), "pulse|alsa|oss|null" },
                { "debug",   'd', 0, G_OPTION_ARG_NONE,   &debug, N_("Enable debug"), NULL },
                { "page",    'p', 0, G_OPTION_ARG_STRING, &page, N_("Startup page"), "effects|hardware|input|output|applications" },
                { "version", 'v', 0, G_OPTION_ARG_NONE,   &show_version, N_("Version of this application"), NULL },
                { NULL }
        };

        bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);

        ctk_init_with_args (&argc, &argv,
                            _(" — CAFE Volume Control"),
                            entries, GETTEXT_PACKAGE,
                            &error);

        if (error != NULL) {
                g_warning ("%s", error->message);
                g_error_free (error);
                return 1;
        }
        if (show_version == TRUE) {
                g_print ("%s %s\n", argv[0], VERSION);
                return 0;
        }
        if (debug == TRUE) {
                g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);
        }

        app = g_application_new (GVC_DIALOG_DBUS_NAME, G_APPLICATION_DEFAULT_FLAGS);

        if (!g_application_register (app, NULL, &error))
        {
                g_warning ("%s", error->message);
                g_error_free (error);
                return 1;
        }

        if (cafe_mixer_init () == FALSE) {
                g_warning ("libcafemixer initialization failed, exiting");
                return 1;
        }

        context = cafe_mixer_context_new ();

        if (backend != NULL) {
                if (strcmp (backend, "pulse") == 0)
                        cafe_mixer_context_set_backend_type (context, CAFE_MIXER_BACKEND_PULSEAUDIO);
                else if (strcmp (backend, "alsa") == 0)
                        cafe_mixer_context_set_backend_type (context, CAFE_MIXER_BACKEND_ALSA);
                else if (strcmp (backend, "oss") == 0)
                        cafe_mixer_context_set_backend_type (context, CAFE_MIXER_BACKEND_OSS);
                else if (strcmp (backend, "null") == 0)
                        cafe_mixer_context_set_backend_type (context, CAFE_MIXER_BACKEND_NULL);
                else {
                        g_warning ("Invalid backend: %s", backend);
                        g_object_unref (context);
                        g_object_unref (app);
                        g_free (backend);
                        return 1;
                }

                g_free (backend);
        }

        cafe_mixer_context_set_app_name (context, _("Volume Control"));
        cafe_mixer_context_set_app_id (context, GVC_DIALOG_DBUS_NAME);
        cafe_mixer_context_set_app_version (context, VERSION);
        cafe_mixer_context_set_app_icon (context, "multimedia-volume-control");

        g_signal_connect (G_OBJECT (context),
                          "notify::state",
                          G_CALLBACK (on_context_state_notify),
                          app);

        cafe_mixer_context_open (context);

        if (cafe_mixer_context_get_state (context) == CAFE_MIXER_STATE_CONNECTING) {
                popup_id = g_timeout_add_seconds (DIALOG_POPUP_TIMEOUT,
                                                  dialog_popup_timeout,
                                                  NULL);
        }

        ctk_icon_theme_append_search_path (ctk_icon_theme_get_default (),
                                           ICON_DATA_DIR);

        ctk_window_set_default_icon_name ("multimedia-volume-control");

        g_object_set (ctk_settings_get_default (), "ctk-button-images", TRUE, NULL);

        ctk_main ();

        g_object_unref (context);
        g_object_unref (app);

        return 0;
}
