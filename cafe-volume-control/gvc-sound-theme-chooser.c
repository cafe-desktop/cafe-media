/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2008 William Jon McCann
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <utime.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <ctk/ctk.h>
#include <kanberra-ctk.h>
#include <libxml/tree.h>

#include "gvc-sound-theme-chooser.h"
#include "sound-theme-file-utils.h"

struct GvcSoundThemeChooserPrivate
{
        CtkWidget *combo_box;
        CtkWidget *treeview;
        CtkWidget *theme_box;
        CtkWidget *selection_box;
        CtkWidget *click_feedback_button;
        GSettings *sound_settings;
};

static void     gvc_sound_theme_chooser_dispose   (GObject            *object);

G_DEFINE_TYPE_WITH_PRIVATE (GvcSoundThemeChooser, gvc_sound_theme_chooser, CTK_TYPE_BOX)

#define KEY_SOUNDS_SCHEMA          "org.cafe.sound"
#define EVENT_SOUNDS_KEY           "event-sounds"
#define INPUT_SOUNDS_KEY           "input-feedback-sounds"
#define SOUND_THEME_KEY            "theme-name"

#define DEFAULT_ALERT_ID        "__default"
#define CUSTOM_THEME_NAME       "__custom"
#define NO_SOUNDS_THEME_NAME    "__no_sounds"

enum {
        THEME_DISPLAY_COL,
        THEME_IDENTIFIER_COL,
        THEME_PARENT_ID_COL,
        THEME_NUM_COLS
};

enum {
        ALERT_DISPLAY_COL,
        ALERT_IDENTIFIER_COL,
        ALERT_SOUND_TYPE_COL,
        ALERT_ACTIVE_COL,
        ALERT_NUM_COLS
};

enum {
        SOUND_TYPE_UNSET,
        SOUND_TYPE_OFF,
        SOUND_TYPE_DEFAULT_FROM_THEME,
        SOUND_TYPE_BUILTIN,
        SOUND_TYPE_CUSTOM
};

static void
on_combobox_changed (CtkComboBox          *widget,
                     GvcSoundThemeChooser *chooser)
{
        CtkTreeIter   iter;
        CtkTreeModel *model;
        char         *theme_name;

        if (ctk_combo_box_get_active_iter (CTK_COMBO_BOX (chooser->priv->combo_box), &iter) == FALSE) {
                return;
        }

        model = ctk_combo_box_get_model (CTK_COMBO_BOX (chooser->priv->combo_box));
        ctk_tree_model_get (model, &iter, THEME_IDENTIFIER_COL, &theme_name, -1);

        g_assert (theme_name != NULL);

        /* special case for no sounds */
        if (strcmp (theme_name, NO_SOUNDS_THEME_NAME) == 0) {
                g_settings_set_boolean (chooser->priv->sound_settings, EVENT_SOUNDS_KEY, FALSE);
                return;
        } else {
                g_settings_set_boolean (chooser->priv->sound_settings, EVENT_SOUNDS_KEY, TRUE);
        }

        g_settings_set_string (chooser->priv->sound_settings, SOUND_THEME_KEY, theme_name);

        g_free (theme_name);

        /* FIXME: reset alert model */
}

static char *
load_index_theme_name (const char *index,
                       char      **parent)
{
        GKeyFile *file;
        char *indexname = NULL;
        gboolean hidden;

        file = g_key_file_new ();
        if (g_key_file_load_from_file (file, index, G_KEY_FILE_KEEP_TRANSLATIONS, NULL) == FALSE) {
                g_key_file_free (file);
                return NULL;
        }
        /* Don't add hidden themes to the list */
        hidden = g_key_file_get_boolean (file, "Sound Theme", "Hidden", NULL);
        if (!hidden) {
                indexname = g_key_file_get_locale_string (file,
                                                          "Sound Theme",
                                                          "Name",
                                                          NULL,
                                                          NULL);

                /* Save the parent theme, if there's one */
                if (parent != NULL) {
                        *parent = g_key_file_get_string (file,
                                                         "Sound Theme",
                                                         "Inherits",
                                                         NULL);
                }
        }

        g_key_file_free (file);
        return indexname;
}

static void
sound_theme_in_dir (GHashTable *hash,
                    const char *dir)
{
        GDir *d;
        const char *name;

        d = g_dir_open (dir, 0, NULL);
        if (d == NULL) {
                return;
        }

        while ((name = g_dir_read_name (d)) != NULL) {
                char *dirname, *index, *indexname;

                /* Look for directories */
                dirname = g_build_filename (dir, name, NULL);
                if (g_file_test (dirname, G_FILE_TEST_IS_DIR) == FALSE) {
                        g_free (dirname);
                        continue;
                }

                /* Look for index files */
                index = g_build_filename (dirname, "index.theme", NULL);
                g_free (dirname);

                /* Check the name of the theme in the index.theme file */
                indexname = load_index_theme_name (index, NULL);
                g_free (index);
                if (indexname == NULL) {
                        continue;
                }

                g_hash_table_insert (hash, g_strdup (name), indexname);
        }

        g_dir_close (d);
}

static void
add_theme_to_store (const char   *key,
                    const char   *value,
                    CtkListStore *store)
{
        char *parent;

        parent = NULL;

        /* Get the parent, if we're checking the custom theme */
        if (strcmp (key, CUSTOM_THEME_NAME) == 0) {
                char *name, *path;

                path = custom_theme_dir_path ("index.theme");
                name = load_index_theme_name (path, &parent);
                g_free (name);
                g_free (path);
        }
        ctk_list_store_insert_with_values (store, NULL, G_MAXINT,
                                           THEME_DISPLAY_COL, value,
                                           THEME_IDENTIFIER_COL, key,
                                           THEME_PARENT_ID_COL, parent,
                                           -1);
        g_free (parent);
}

static void
set_combox_for_theme_name (GvcSoundThemeChooser *chooser,
                           const char           *name)
{
        CtkTreeIter   iter;
        CtkTreeModel *model;
        gboolean      found;

        /* If the name is empty, use "freedesktop" */
        if (name == NULL || *name == '\0') {
                name = "freedesktop";
        }

        model = ctk_combo_box_get_model (CTK_COMBO_BOX (chooser->priv->combo_box));

        if (ctk_tree_model_get_iter_first (model, &iter) == FALSE) {
                return;
        }

        do {
                char *value;

                ctk_tree_model_get (model, &iter, THEME_IDENTIFIER_COL, &value, -1);
                found = (value != NULL && strcmp (value, name) == 0);
                g_free (value);

        } while (!found && ctk_tree_model_iter_next (model, &iter));

        /* When we can't find the theme we need to set, try to set the default
         * one "freedesktop" */
        if (found) {
                ctk_combo_box_set_active_iter (CTK_COMBO_BOX (chooser->priv->combo_box), &iter);
        } else if (strcmp (name, "freedesktop") != 0) {
                g_debug ("not found, falling back to fdo");
                set_combox_for_theme_name (chooser, "freedesktop");
        }
}

static void
set_input_feedback_enabled (GvcSoundThemeChooser *chooser,
                            gboolean              enabled)
{
        ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (chooser->priv->click_feedback_button),
                                      enabled);
}

static void
setup_theme_selector (GvcSoundThemeChooser *chooser)
{
        GHashTable           *hash;
        CtkListStore         *store;
        CtkCellRenderer      *renderer;
        const char * const   *data_dirs;
        const char           *data_dir;
        char                 *dir;
        guint                 i;

        /* Add the theme names and their display name to a hash table,
         * makes it easy to avoid duplicate themes */
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        data_dirs = g_get_system_data_dirs ();
        for (i = 0; data_dirs[i] != NULL; i++) {
                dir = g_build_filename (data_dirs[i], "sounds", NULL);
                sound_theme_in_dir (hash, dir);
                g_free (dir);
        }

        data_dir = g_get_user_data_dir ();
        dir = g_build_filename (data_dir, "sounds", NULL);
        sound_theme_in_dir (hash, dir);
        g_free (dir);

        /* If there isn't at least one theme, make everything
         * insensitive, LAME! */
        if (g_hash_table_size (hash) == 0) {
                ctk_widget_set_sensitive (CTK_WIDGET (chooser), FALSE);
                g_warning ("Bad setup, install the freedesktop sound theme");
                g_hash_table_destroy (hash);
                return;
        }

        /* Setup the tree model, 3 columns:
         * - internal theme name/directory
         * - display theme name
         * - the internal id for the parent theme, used for the custom theme */
        store = ctk_list_store_new (THEME_NUM_COLS,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING);

        /* Add the themes to a combobox */
        ctk_list_store_insert_with_values (store,
                                           NULL,
                                           G_MAXINT,
                                           THEME_DISPLAY_COL, _("No sounds"),
                                           THEME_IDENTIFIER_COL, "__no_sounds",
                                           THEME_PARENT_ID_COL, NULL,
                                           -1);
        g_hash_table_foreach (hash, (GHFunc) add_theme_to_store, store);
        g_hash_table_destroy (hash);

        /* Set the display */
        ctk_combo_box_set_model (CTK_COMBO_BOX (chooser->priv->combo_box),
                                 CTK_TREE_MODEL (store));

        renderer = ctk_cell_renderer_text_new ();
        ctk_cell_layout_pack_start (CTK_CELL_LAYOUT (chooser->priv->combo_box),
                                    renderer,
                                    TRUE);
        ctk_cell_layout_set_attributes (CTK_CELL_LAYOUT (chooser->priv->combo_box),
                                        renderer,
                                        "text", THEME_DISPLAY_COL,
                                        NULL);

        g_signal_connect (G_OBJECT (chooser->priv->combo_box),
                          "changed",
                          G_CALLBACK (on_combobox_changed),
                          chooser);
}

#define GVC_SOUND_SOUND    (xmlChar *) "sound"
#define GVC_SOUND_NAME     (xmlChar *) "name"
#define GVC_SOUND_FILENAME (xmlChar *) "filename"

/* Adapted from yelp-toc-pager.c */
static xmlChar *
xml_get_and_trim_names (xmlNodePtr node)
{
        xmlNodePtr cur;
        xmlChar *keep_lang = NULL;
        xmlChar *value;
        int j, keep_pri = INT_MAX;

        const gchar * const * langs = g_get_language_names ();

        value = NULL;

        for (cur = node->children; cur; cur = cur->next) {
                if (! xmlStrcmp (cur->name, GVC_SOUND_NAME)) {
                        xmlChar *cur_lang = NULL;
                        int cur_pri = INT_MAX;

                        cur_lang = xmlNodeGetLang (cur);

                        if (cur_lang) {
                                for (j = 0; langs[j]; j++) {
                                        if (g_str_equal (cur_lang, langs[j])) {
                                                cur_pri = j;
                                                break;
                                        }
                                }
                        } else {
                                cur_pri = INT_MAX - 1;
                        }

                        if (cur_pri <= keep_pri) {
                                if (keep_lang)
                                        xmlFree (keep_lang);
                                if (value)
                                        xmlFree (value);

                                value = xmlNodeGetContent (cur);

                                keep_lang = cur_lang;
                                keep_pri = cur_pri;
                        } else {
                                if (cur_lang)
                                        xmlFree (cur_lang);
                        }
                }
        }

        /* Delete all GVC_SOUND_NAME nodes */
        cur = node->children;
        while (cur) {
                xmlNodePtr this = cur;
                cur = cur->next;
                if (! xmlStrcmp (this->name, GVC_SOUND_NAME)) {
                        xmlUnlinkNode (this);
                        xmlFreeNode (this);
                }
        }

        return value;
}

static void
populate_model_from_node (GvcSoundThemeChooser *chooser,
                          CtkTreeModel         *model,
                          xmlNodePtr            node)
{
        xmlNodePtr child;
        xmlChar   *filename;
        xmlChar   *name;

        filename = NULL;
        name = xml_get_and_trim_names (node);
        for (child = node->children; child; child = child->next) {
                if (xmlNodeIsText (child)) {
                        continue;
                }

                if (xmlStrcmp (child->name, GVC_SOUND_FILENAME) == 0) {
                        filename = xmlNodeGetContent (child);
                } else if (xmlStrcmp (child->name, GVC_SOUND_NAME) == 0) {
                        /* EH? should have been trimmed */
                }
        }

        if (filename != NULL && name != NULL) {
                ctk_list_store_insert_with_values (CTK_LIST_STORE (model),
                                                   NULL,
                                                   G_MAXINT,
                                                   ALERT_IDENTIFIER_COL, filename,
                                                   ALERT_DISPLAY_COL, name,
                                                   ALERT_SOUND_TYPE_COL, _("Built-in"),
                                                   ALERT_ACTIVE_COL, FALSE,
                                                   -1);
        }

        xmlFree (filename);
        xmlFree (name);
}

static void
populate_model_from_file (GvcSoundThemeChooser *chooser,
                          CtkTreeModel         *model,
                          const char           *filename)
{
        xmlDocPtr  doc;
        xmlNodePtr root;
        xmlNodePtr child;
        gboolean   exists;

        exists = g_file_test (filename, G_FILE_TEST_EXISTS);
        if (! exists) {
                return;
        }

        doc = xmlParseFile (filename);
        if (doc == NULL) {
                return;
        }

        root = xmlDocGetRootElement (doc);

        for (child = root->children; child; child = child->next) {
                if (xmlNodeIsText (child)) {
                        continue;
                }
                if (xmlStrcmp (child->name, GVC_SOUND_SOUND) != 0) {
                        continue;
                }

                populate_model_from_node (chooser, model, child);
        }

        xmlFreeDoc (doc);
}

static void
populate_model_from_dir (GvcSoundThemeChooser *chooser,
                         CtkTreeModel         *model,
                         const char           *dirname)
{
        GDir       *d;
        const char *name;

        d = g_dir_open (dirname, 0, NULL);
        if (d == NULL) {
                return;
        }

        while ((name = g_dir_read_name (d)) != NULL) {
                char *path;

                if (! g_str_has_suffix (name, ".xml")) {
                        continue;
                }

                path = g_build_filename (dirname, name, NULL);
                populate_model_from_file (chooser, model, path);
                g_free (path);
        }

        g_dir_close (d);
}

static gboolean
save_alert_sounds (GvcSoundThemeChooser  *chooser,
                   const char            *id)
{
        const char *sounds[3] = { "bell-terminal", "bell-window-system", NULL };
        char *path;

        if (strcmp (id, DEFAULT_ALERT_ID) == 0) {
                delete_old_files (sounds);
                delete_disabled_files (sounds);
        } else {
                delete_old_files (sounds);
                delete_disabled_files (sounds);
                add_custom_file (sounds, id);
        }

        /* And poke the directory so the theme gets updated */
        path = custom_theme_dir_path (NULL);
        if (utime (path, NULL) != 0) {
                g_warning ("Failed to update mtime for directory '%s': %s",
                           path, g_strerror (errno));
        }
        g_free (path);

        return FALSE;
}


static void
update_alert_model (GvcSoundThemeChooser  *chooser,
                    const char            *id)
{
        CtkTreeModel *model;
        CtkTreeIter   iter;

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (chooser->priv->treeview));
        ctk_tree_model_get_iter_first (model, &iter);
        do {
                gboolean toggled;
                char    *this_id;

                ctk_tree_model_get (model, &iter,
                                    ALERT_IDENTIFIER_COL, &this_id,
                                    -1);

                if (strcmp (this_id, id) == 0) {
                        toggled = TRUE;
                } else {
                        toggled = FALSE;
                }
                g_free (this_id);

                ctk_list_store_set (CTK_LIST_STORE (model),
                                    &iter,
                                    ALERT_ACTIVE_COL, toggled,
                                    -1);
        } while (ctk_tree_model_iter_next (model, &iter));
}

static void
update_alert (GvcSoundThemeChooser *chooser,
              const char           *alert_id)
{
        CtkTreeModel *theme_model;
        CtkTreeIter   iter;
        char         *theme;
        char         *parent;
        gboolean      is_custom;
        gboolean      is_default;
        gboolean      add_custom;
        gboolean      remove_custom;

        theme_model = ctk_combo_box_get_model (CTK_COMBO_BOX (chooser->priv->combo_box));
        /* Get the current theme's name, and set the parent */
        if (ctk_combo_box_get_active_iter (CTK_COMBO_BOX (chooser->priv->combo_box), &iter) == FALSE) {
                return;
        }

        ctk_tree_model_get (theme_model, &iter,
                            THEME_IDENTIFIER_COL, &theme,
                            THEME_IDENTIFIER_COL, &parent,
                            -1);
        is_custom = strcmp (theme, CUSTOM_THEME_NAME) == 0;
        is_default = strcmp (alert_id, DEFAULT_ALERT_ID) == 0;

        /* So a few possibilities:
         * 1. Named theme, default alert selected: noop
         * 2. Named theme, alternate alert selected: create new custom with sound
         * 3. Custom theme, default alert selected: remove sound and possibly custom
         * 4. Custom theme, alternate alert selected: update custom sound
         */
        add_custom = FALSE;
        remove_custom = FALSE;
        if (! is_custom && is_default) {
                /* remove custom just in case */
                remove_custom = TRUE;
        } else if (! is_custom && ! is_default) {
                create_custom_theme (parent);
                save_alert_sounds (chooser, alert_id);
                add_custom = TRUE;
        } else if (is_custom && is_default) {
                save_alert_sounds (chooser, alert_id);
                /* after removing files check if it is empty */
                if (custom_theme_dir_is_empty ()) {
                        remove_custom = TRUE;
                }
        } else if (is_custom && ! is_default) {
                save_alert_sounds (chooser, alert_id);
        }

        if (add_custom) {
                ctk_list_store_insert_with_values (CTK_LIST_STORE (theme_model),
                                                   NULL,
                                                   G_MAXINT,
                                                   THEME_DISPLAY_COL, _("Custom"),
                                                   THEME_IDENTIFIER_COL, CUSTOM_THEME_NAME,
                                                   THEME_PARENT_ID_COL, theme,
                                                   -1);
                set_combox_for_theme_name (chooser, CUSTOM_THEME_NAME);
        } else if (remove_custom) {
                ctk_tree_model_get_iter_first (theme_model, &iter);
                do {
                        char *this_parent;

                        ctk_tree_model_get (theme_model, &iter,
                                            THEME_PARENT_ID_COL, &this_parent,
                                            -1);
                        if (this_parent != NULL && strcmp (this_parent, CUSTOM_THEME_NAME) != 0) {
                                g_free (this_parent);
                                ctk_list_store_remove (CTK_LIST_STORE (theme_model), &iter);
                                break;
                        }
                        g_free (this_parent);
                } while (ctk_tree_model_iter_next (theme_model, &iter));

                delete_custom_theme_dir ();

                set_combox_for_theme_name (chooser, parent);
        }

        update_alert_model (chooser, alert_id);

        g_free (theme);
        g_free (parent);
}

static void
on_alert_toggled (CtkCellRendererToggle *renderer,
                  char                  *path_str,
                  GvcSoundThemeChooser  *chooser)
{
        CtkTreeModel *model;
        CtkTreeIter   iter;
        CtkTreePath  *path;
        gboolean      toggled;
        char         *id;

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (chooser->priv->treeview));

        path = ctk_tree_path_new_from_string (path_str);
        ctk_tree_model_get_iter (model, &iter, path);
        ctk_tree_path_free (path);

        id = NULL;
        ctk_tree_model_get (model, &iter,
                            ALERT_IDENTIFIER_COL, &id,
                            ALERT_ACTIVE_COL, &toggled,
                            -1);

        toggled ^= 1;
        if (toggled) {
                update_alert (chooser, id);
        }

        g_free (id);
}

static void
play_preview_for_path (GvcSoundThemeChooser *chooser, CtkTreePath *path)
{
        CtkTreeModel *model;
        CtkTreeIter   iter;
        CtkTreeIter   theme_iter;
        gchar        *id = NULL;
        gchar        *parent_theme = NULL;

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (chooser->priv->treeview));
        if (ctk_tree_model_get_iter (model, &iter, path) == FALSE)
                return;

        ctk_tree_model_get (model, &iter,
                            ALERT_IDENTIFIER_COL, &id,
                            -1);
        if (id == NULL)
                return;

        if (ctk_combo_box_get_active_iter (CTK_COMBO_BOX (chooser->priv->combo_box), &theme_iter)) {
                CtkTreeModel *theme_model;
                gchar        *theme_id = NULL;
                gchar        *parent_id = NULL;

                theme_model = ctk_combo_box_get_model (CTK_COMBO_BOX (chooser->priv->combo_box));

                ctk_tree_model_get (theme_model, &theme_iter,
                                    THEME_IDENTIFIER_COL, &theme_id,
                                    THEME_PARENT_ID_COL, &parent_id, -1);
                if (theme_id && strcmp (theme_id, CUSTOM_THEME_NAME) == 0)
                        parent_theme = g_strdup (parent_id);

                g_free (theme_id);
                g_free (parent_id);
        }

        /* special case: for the default item on custom themes
         * play the alert for the parent theme */
        if (strcmp (id, DEFAULT_ALERT_ID) == 0) {
                if (parent_theme != NULL) {
                        ka_ctk_play_for_widget (CTK_WIDGET (chooser), 0,
                                                KA_PROP_APPLICATION_NAME, _("Sound Preferences"),
                                                KA_PROP_EVENT_ID, "bell-window-system",
                                                KA_PROP_KANBERRA_XDG_THEME_NAME, parent_theme,
                                                KA_PROP_EVENT_DESCRIPTION, _("Testing event sound"),
                                                KA_PROP_KANBERRA_CACHE_CONTROL, "never",
                                                KA_PROP_APPLICATION_ID, "org.cafe.VolumeControl",
#ifdef KA_PROP_KANBERRA_ENABLE
                                                KA_PROP_KANBERRA_ENABLE, "1",
#endif
                                                NULL);
                } else {
                        ka_ctk_play_for_widget (CTK_WIDGET (chooser), 0,
                                                KA_PROP_APPLICATION_NAME, _("Sound Preferences"),
                                                KA_PROP_EVENT_ID, "bell-window-system",
                                                KA_PROP_EVENT_DESCRIPTION, _("Testing event sound"),
                                                KA_PROP_KANBERRA_CACHE_CONTROL, "never",
                                                KA_PROP_APPLICATION_ID, "org.cafe.VolumeControl",
#ifdef KA_PROP_KANBERRA_ENABLE
                                                KA_PROP_KANBERRA_ENABLE, "1",
#endif
                                                NULL);
                }
        } else {
                ka_ctk_play_for_widget (CTK_WIDGET (chooser), 0,
                                        KA_PROP_APPLICATION_NAME, _("Sound Preferences"),
                                        KA_PROP_MEDIA_FILENAME, id,
                                        KA_PROP_EVENT_DESCRIPTION, _("Testing event sound"),
                                        KA_PROP_KANBERRA_CACHE_CONTROL, "never",
                                        KA_PROP_APPLICATION_ID, "org.cafe.VolumeControl",
#ifdef KA_PROP_KANBERRA_ENABLE
                                        KA_PROP_KANBERRA_ENABLE, "1",
#endif
                                        NULL);

        }
        g_free (parent_theme);
        g_free (id);
}

static void
on_treeview_row_activated (CtkTreeView          *treeview,
                           CtkTreePath          *path,
                           CtkTreeViewColumn    *column,
                           GvcSoundThemeChooser *chooser)
{
        play_preview_for_path (chooser, path);
}

static void
on_treeview_selection_changed (CtkTreeSelection     *selection,
                               GvcSoundThemeChooser *chooser)
{
        GList        *paths;
        CtkTreeModel *model;
        CtkTreePath  *path;

        if (chooser->priv->treeview == NULL)
                return;

        model = ctk_tree_view_get_model (CTK_TREE_VIEW (chooser->priv->treeview));

        paths = ctk_tree_selection_get_selected_rows (selection, &model);
        if (paths == NULL)
                return;

        path = paths->data;
        play_preview_for_path (chooser, path);

        g_list_foreach (paths, (GFunc)ctk_tree_path_free, NULL);
        g_list_free (paths);
}

static CtkWidget *
create_alert_treeview (GvcSoundThemeChooser *chooser)
{
        CtkListStore         *store;
        CtkWidget            *treeview;
        CtkCellRenderer      *renderer;
        CtkTreeViewColumn    *column;
        CtkTreeSelection     *selection;

        treeview = ctk_tree_view_new ();

        ctk_tree_view_set_headers_visible (CTK_TREE_VIEW (treeview), FALSE);
        g_signal_connect (G_OBJECT (treeview),
                          "row-activated",
                          G_CALLBACK (on_treeview_row_activated),
                          chooser);

        selection = ctk_tree_view_get_selection (CTK_TREE_VIEW (treeview));

        ctk_tree_selection_set_mode (selection, CTK_SELECTION_SINGLE);
        g_signal_connect (G_OBJECT (selection),
                          "changed",
                          G_CALLBACK (on_treeview_selection_changed),
                          chooser);

        /* Setup the tree model, 3 columns:
         * - display name
         * - sound id
         * - sound type
         */
        store = ctk_list_store_new (ALERT_NUM_COLS,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_STRING,
                                    G_TYPE_BOOLEAN);

        ctk_list_store_insert_with_values (store,
                                           NULL,
                                           G_MAXINT,
                                           ALERT_IDENTIFIER_COL, DEFAULT_ALERT_ID,
                                           ALERT_DISPLAY_COL, _("Default"),
                                           ALERT_SOUND_TYPE_COL, _("From theme"),
                                           ALERT_ACTIVE_COL, TRUE,
                                           -1);

        populate_model_from_dir (chooser, CTK_TREE_MODEL (store), SOUND_SET_DIR);

        ctk_tree_view_set_model (CTK_TREE_VIEW (treeview),
                                 CTK_TREE_MODEL (store));

        renderer = ctk_cell_renderer_toggle_new ();
        ctk_cell_renderer_toggle_set_radio (CTK_CELL_RENDERER_TOGGLE (renderer), TRUE);

        column = ctk_tree_view_column_new_with_attributes (NULL,
                                                           renderer,
                                                           "active", ALERT_ACTIVE_COL,
                                                           NULL);
        ctk_tree_view_append_column (CTK_TREE_VIEW (treeview), column);
        g_signal_connect (renderer,
                          "toggled",
                          G_CALLBACK (on_alert_toggled),
                          chooser);

        renderer = ctk_cell_renderer_text_new ();
        column = ctk_tree_view_column_new_with_attributes (_("Name"),
                                                           renderer,
                                                           "text", ALERT_DISPLAY_COL,
                                                           NULL);
        ctk_tree_view_append_column (CTK_TREE_VIEW (treeview), column);

        renderer = ctk_cell_renderer_text_new ();
        column = ctk_tree_view_column_new_with_attributes (_("Type"),
                                                           renderer,
                                                           "text", ALERT_SOUND_TYPE_COL,
                                                           NULL);

        ctk_tree_view_append_column (CTK_TREE_VIEW (treeview), column);

        return treeview;
}

static int
get_file_type (const char *sound_name,
               char      **linked_name)
{
        char *name, *filename;

        *linked_name = NULL;

        name = g_strdup_printf ("%s.disabled", sound_name);
        filename = custom_theme_dir_path (name);
        g_free (name);

        if (g_file_test (filename, G_FILE_TEST_IS_REGULAR) != FALSE) {
                g_free (filename);
                return SOUND_TYPE_OFF;
        }
        g_free (filename);

        /* We only check for .ogg files because those are the
         * only ones we create */
        name = g_strdup_printf ("%s.ogg", sound_name);
        filename = custom_theme_dir_path (name);
        g_free (name);

        if (g_file_test (filename, G_FILE_TEST_IS_SYMLINK) != FALSE) {
                *linked_name = g_file_read_link (filename, NULL);
                g_free (filename);
                return SOUND_TYPE_CUSTOM;
        }
        g_free (filename);

        return SOUND_TYPE_BUILTIN;
}

static void
update_alerts_from_theme_name (GvcSoundThemeChooser *chooser,
                               const gchar          *name)
{
        if (strcmp (name, CUSTOM_THEME_NAME) != 0) {
                /* reset alert to default */
                update_alert (chooser, DEFAULT_ALERT_ID);
        } else {
                int   sound_type;
                char *linkname;

                linkname = NULL;
                sound_type = get_file_type ("bell-terminal", &linkname);
                g_debug ("Found link: %s", linkname);
                if (sound_type == SOUND_TYPE_CUSTOM) {
                        update_alert (chooser, linkname);
                }
        }
}

static void
update_theme (GvcSoundThemeChooser *chooser)
{
        char        *theme_name;
        gboolean     events_enabled;
        gboolean     feedback_enabled;

        feedback_enabled = g_settings_get_boolean (chooser->priv->sound_settings, INPUT_SOUNDS_KEY);
        set_input_feedback_enabled (chooser, feedback_enabled);

        events_enabled = g_settings_get_boolean (chooser->priv->sound_settings, EVENT_SOUNDS_KEY);
        if (events_enabled) {
                theme_name = g_settings_get_string (chooser->priv->sound_settings, SOUND_THEME_KEY);
        } else {
                theme_name = g_strdup (NO_SOUNDS_THEME_NAME);
        }

        ctk_widget_set_sensitive (chooser->priv->selection_box, events_enabled);
        ctk_widget_set_sensitive (chooser->priv->click_feedback_button, events_enabled);

        set_combox_for_theme_name (chooser, theme_name);

        update_alerts_from_theme_name (chooser, theme_name);

        g_free (theme_name);
}

static void
gvc_sound_theme_chooser_class_init (GvcSoundThemeChooserClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->dispose = gvc_sound_theme_chooser_dispose;
}

static void
on_click_feedback_toggled (CtkToggleButton      *button,
                           GvcSoundThemeChooser *chooser)
{
        gboolean enabled;

        enabled = ctk_toggle_button_get_active (button);

        g_settings_set_boolean (chooser->priv->sound_settings, INPUT_SOUNDS_KEY, enabled);
}

static void
on_key_changed (GSettings            *settings,
                gchar                *key,
                GvcSoundThemeChooser *chooser)
{
        if (!strcmp (key, EVENT_SOUNDS_KEY) ||
            !strcmp (key, SOUND_THEME_KEY) ||
            !strcmp (key, INPUT_SOUNDS_KEY))
                update_theme (chooser);
}

static void
setup_list_size_constraint (CtkWidget *widget,
                            CtkWidget *to_size)
{
        CtkRequisition req;
        gint           sc_height;
        int            max_height;

        /* Constrain height to be the tree height up to a max */
        cdk_window_get_geometry (cdk_screen_get_root_window (ctk_widget_get_screen (widget)),
                                 NULL, NULL, NULL, &sc_height);

        max_height = sc_height / 4;

        // XXX this doesn't work
        ctk_widget_get_preferred_size (to_size, NULL, &req);

        ctk_scrolled_window_set_min_content_height (CTK_SCROLLED_WINDOW (widget),
                                                    MIN (req.height, max_height));
}

static void
gvc_sound_theme_chooser_init (GvcSoundThemeChooser *chooser)
{
        CtkWidget   *box;
        CtkWidget   *label;
        CtkWidget   *scrolled_window;
        gchar       *str;

        chooser->priv = gvc_sound_theme_chooser_get_instance_private (chooser);

        chooser->priv->theme_box = ctk_box_new (CTK_ORIENTATION_HORIZONTAL, 0);

        ctk_box_pack_start (CTK_BOX (chooser),
                            chooser->priv->theme_box, FALSE, FALSE, 0);

        label = ctk_label_new_with_mnemonic (_("Sound _theme:"));
        ctk_box_pack_start (CTK_BOX (chooser->priv->theme_box), label, FALSE, FALSE, 0);
        chooser->priv->combo_box = ctk_combo_box_new ();
        ctk_box_pack_start (CTK_BOX (chooser->priv->theme_box), chooser->priv->combo_box, FALSE, FALSE, 6);
        ctk_label_set_mnemonic_widget (CTK_LABEL (label), chooser->priv->combo_box);

        chooser->priv->sound_settings = g_settings_new (KEY_SOUNDS_SCHEMA);

        g_signal_connect (G_OBJECT (chooser->priv->sound_settings),
                          "changed",
                          G_CALLBACK (on_key_changed),
                          chooser);

        str = g_strdup_printf ("<b>%s</b>", _("C_hoose an alert sound:"));
        chooser->priv->selection_box = box = ctk_frame_new (str);
        g_free (str);

        label = ctk_frame_get_label_widget (CTK_FRAME (box));
        ctk_label_set_use_underline (CTK_LABEL (label), TRUE);
        ctk_label_set_use_markup (CTK_LABEL (label), TRUE);
        ctk_frame_set_shadow_type (CTK_FRAME (box), CTK_SHADOW_NONE);

        ctk_box_pack_start (CTK_BOX (chooser), box, TRUE, TRUE, 6);

        chooser->priv->treeview = create_alert_treeview (chooser);
        ctk_label_set_mnemonic_widget (CTK_LABEL (label), chooser->priv->treeview);

        scrolled_window = ctk_scrolled_window_new (NULL, NULL);
        ctk_widget_set_hexpand (scrolled_window, TRUE);
        ctk_widget_set_vexpand (scrolled_window, TRUE);
        ctk_widget_set_margin_top (scrolled_window, 6);

        ctk_scrolled_window_set_policy (CTK_SCROLLED_WINDOW (scrolled_window),
                                        CTK_POLICY_NEVER,
                                        CTK_POLICY_AUTOMATIC);
        ctk_scrolled_window_set_shadow_type (CTK_SCROLLED_WINDOW (scrolled_window),
                                             CTK_SHADOW_IN);

        ctk_container_add (CTK_CONTAINER (scrolled_window), chooser->priv->treeview);
        ctk_container_add (CTK_CONTAINER (box), scrolled_window);

        chooser->priv->click_feedback_button = ctk_check_button_new_with_mnemonic (_("Enable _window and button sounds"));
        ctk_toggle_button_set_active (CTK_TOGGLE_BUTTON (chooser->priv->click_feedback_button),
                                      g_settings_get_boolean (chooser->priv->sound_settings, INPUT_SOUNDS_KEY));

        ctk_box_pack_start (CTK_BOX (chooser),
                            chooser->priv->click_feedback_button,
                            FALSE, FALSE, 0);

        g_signal_connect (G_OBJECT (chooser->priv->click_feedback_button),
                          "toggled",
                          G_CALLBACK (on_click_feedback_toggled),
                          chooser);

        setup_theme_selector (chooser);
        update_theme (chooser);

        setup_list_size_constraint (scrolled_window, chooser->priv->treeview);
}

static void
gvc_sound_theme_chooser_dispose (GObject *object)
{
        GvcSoundThemeChooser *chooser;

        chooser = GVC_SOUND_THEME_CHOOSER (object);

        g_clear_object (&chooser->priv->sound_settings);

        G_OBJECT_CLASS (gvc_sound_theme_chooser_parent_class)->dispose (object);
}

CtkWidget *
gvc_sound_theme_chooser_new (void)
{
        return g_object_new (GVC_TYPE_SOUND_THEME_CHOOSER,
                                "spacing", 6,
                                "orientation", CTK_ORIENTATION_VERTICAL,
                                NULL);
}
