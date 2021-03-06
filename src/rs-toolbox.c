/*
 * * Copyright (C) 2006-2011 Anders Brander <anders@brander.dk>, 
 * * Anders Kvist <akv@lnxbx.dk> and Klaus Post <klauspost@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <rawstudio.h>
#include <gtk/gtk.h>
#include <config.h>
#ifndef WIN32
#include <gconf/gconf-client.h>
#endif
#include "gettext.h"
#include "rs-toolbox.h"
#include "gtk-interface.h"
#include "gtk-helper.h"
#include "rs-settings.h"
#include "rs-curve.h"
#include "rs-image.h"
#include "rs-histogram.h"
#include "rs-utils.h"
#include "rs-photo.h"
#include "conf_interface.h"
#include "rs-actions.h"
#include "rs-lens-db-editor.h"
#include "rs-profile-camera.h"
#include "rs-actions.h"
#include "rs-camera-db.h"

/* Some helpers for creating the basic sliders */
typedef struct {
	const gchar *property_name;
	gfloat step;
	RSSettingsMask mask;
} BasicSettings;

const static BasicSettings basic[] = {
	{ "exposure",       0.05, MASK_EXPOSURE},
	{ "saturation",     0.05, MASK_SATURATION},
	{ "hue",            1.5,  MASK_HUE },
	{ "contrast",       0.05, MASK_CONTRAST },
	{ "dcp-temp",       10.0,  MASK_DCP_TEMP },
	{ "dcp-tint",       1.0,  MASK_DCP_TINT},
	{ "sharpen",        0.5,  MASK_SHARPEN },
	{ "denoise_luma",   0.5,  MASK_DENOISE_LUMA},
	{ "denoise_chroma", 0.5,  MASK_DENOISE_CHROMA },
};
#define NBASICS (9)

const static BasicSettings channelmixer[] = {
	{ "channelmixer_red",   1.0, MASK_CHANNELMIXER_RED },
	{ "channelmixer_green", 1.0, MASK_CHANNELMIXER_GREEN },
	{ "channelmixer_blue",  1.0, MASK_CHANNELMIXER_BLUE },
};
#define NCHANNELMIXER (3)

const static BasicSettings lens[] = {
	{ "tca_kr",         0.025, MASK_TCA_KR },
	{ "tca_kb",         0.025, MASK_TCA_KB },
	{ "vignetting",  0.025,    MASK_VIGNETTING },
};
#define NLENS (3)

struct _RSToolbox {
	GtkScrolledWindow parent;

	RSProfileSelector *selector;

	GtkWidget *notebook;
	GtkBox *toolbox;
	GtkRange *ranges[3][NBASICS];
	GtkRange *channelmixer[3][NCHANNELMIXER];
	GtkRange *lens[3][NLENS];
	GtkWidget *lenslabel[3];
	GtkWidget *lensbutton[3];
	RSLens *rs_lens;
	RSSettings *settings[3];
	GtkWidget *curve[3];

	GtkWidget *transforms;
	gint selected_snapshot;
	RS_PHOTO *photo;
	RSFilter* histogram_input;
	RSColorSpace* histogram_colorspace;
	GtkWidget *histogram;
	rs_profile_camera last_camera;

 #ifndef WIN32
	guint histogram_connection; /* Got GConf notification */

	GConfClient *gconf;
#endif

	gboolean mute_from_sliders;
	gboolean mute_from_photo;
};

G_DEFINE_TYPE (RSToolbox, rs_toolbox, GTK_TYPE_SCROLLED_WINDOW)

enum {
	SNAPSHOT_CHANGED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

static void dcp_profile_selected(RSProfileSelector *selector, RSDcpFile *dcp, RSToolbox *toolbox);
static void icc_profile_selected(RSProfileSelector *selector, RSIccProfile *icc, RSToolbox *toolbox);
static void add_profile_selected(RSProfileSelector *selector, RSToolbox *toolbox);
#ifndef WIN32
static void conf_histogram_height_changed(GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer user_data);
#endif
static void notebook_switch_page(GtkNotebook *notebook, gpointer page, guint page_num, RSToolbox *toolbox);
static void basic_range_value_changed(GtkRange *range, gpointer user_data);
static gboolean basic_range_reset(GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static GtkRange *basic_slider(RSToolbox *toolbox, const gint snapshot, GtkTable *table, const gint row, const BasicSettings *basic);
static void curve_changed(GtkWidget *widget, gpointer user_data);
static GtkWidget *new_snapshot_page(RSToolbox *toolbox, const gint snapshot);
static GtkWidget *new_transform(RSToolbox *toolbox, gboolean show);
static void toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo);
static void photo_settings_changed(RS_PHOTO *photo, RSSettingsMask mask, gpointer user_data);
static void photo_wb_changed(RSSettings *settings, gpointer user_data);
static void photo_spatial_changed(RS_PHOTO *photo, gpointer user_data);
static void photo_finalized(gpointer data, GObject *where_the_object_was);
static void toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo);
static void toolbox_lens_set_label(RSToolbox *toolbox, gint snapshot);

static void
rs_toolbox_finalize (GObject *object)
{
	RSToolbox *toolbox = RS_TOOLBOX(object);

#ifndef WIN32
	gconf_client_notify_remove(toolbox->gconf, toolbox->histogram_connection);

	g_object_unref(toolbox->gconf);
#endif

	g_free(toolbox->last_camera.make);
	g_free(toolbox->last_camera.model);

	if (G_OBJECT_CLASS (rs_toolbox_parent_class)->finalize)
		G_OBJECT_CLASS (rs_toolbox_parent_class)->finalize (object);
}

static void
rs_toolbox_class_init (RSToolboxClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	signals[SNAPSHOT_CHANGED] = g_signal_new ("snapshot-changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
		0,
		NULL,
		NULL,
		g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE, 1, G_TYPE_INT);

	object_class->finalize = rs_toolbox_finalize;
}

static void
rs_toolbox_init (RSToolbox *self)
{
	GtkScrolledWindow *scrolled_window = GTK_SCROLLED_WINDOW(self);
	gint page;
	GtkWidget *label[3];
	GtkWidget *viewport;
	gint height;

	/* A box to hold everything */
	self->toolbox = GTK_BOX(gtk_vbox_new (FALSE, 1));

	self->selector = rs_profile_selector_new();
	g_object_set(self->selector, "width-request", 75, NULL);
	g_signal_connect(self->selector, "dcp-selected", G_CALLBACK(dcp_profile_selected), self);
	g_signal_connect(self->selector, "icc-selected", G_CALLBACK(icc_profile_selected), self);
	g_signal_connect(self->selector, "add-selected", G_CALLBACK(add_profile_selected), self);
	gtk_box_pack_start(self->toolbox, GTK_WIDGET(self->selector), FALSE, FALSE, 0);

	for(page=0;page<3;page++)
		self->settings[page] = NULL;

	/* Set up our scrolled window */
	gtk_scrolled_window_set_policy(scrolled_window, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_hadjustment(scrolled_window, NULL);
	gtk_scrolled_window_set_vadjustment(scrolled_window, NULL);

#ifndef WIN32
	/* Get a GConfClient */
	self->gconf = gconf_client_get_default();
#endif

	/* Snapshot labels */
	label[0] = gtk_label_new(_(" A "));
	label[1] = gtk_label_new(_(" B "));
	label[2] = gtk_label_new(_(" C "));

	/* A notebook for the snapshots */
	self->notebook = gtk_notebook_new();
	g_signal_connect(self->notebook, "switch-page", G_CALLBACK(notebook_switch_page), self);

	/* Iterate over 3 snapshots */
	for(page=0;page<3;page++)
		gtk_notebook_append_page(GTK_NOTEBOOK(self->notebook), new_snapshot_page(self, page), label[page]);

	gtk_box_pack_start(self->toolbox, self->notebook, FALSE, FALSE, 0);

	self->transforms = new_transform(self, TRUE);
	gtk_box_pack_start(self->toolbox, self->transforms, FALSE, FALSE, 0);

	/* Histogram */
	self->histogram = rs_histogram_new();
	if (!rs_conf_get_integer(CONF_HISTHEIGHT, &height))
		height = 70;
	gtk_widget_set_size_request(self->histogram, 64, height); /* FIXME: Get height from gconf */
	gtk_box_pack_start(self->toolbox, gui_box(_("Histogram"), self->histogram, "show_histogram", TRUE), FALSE, FALSE, 0);

	/* Watch out for gconf-changes */
#ifndef WIN32
	self->histogram_connection = gconf_client_notify_add(self->gconf, "/apps/" PACKAGE "/histogram_height", conf_histogram_height_changed, self, NULL, NULL);
#endif

	/* Pack everything nice with scrollers */
	viewport = gtk_viewport_new (gtk_scrolled_window_get_hadjustment (scrolled_window),
		gtk_scrolled_window_get_vadjustment (scrolled_window));
	gtk_container_add (GTK_CONTAINER (viewport), GTK_WIDGET(self->toolbox));
	gtk_container_add (GTK_CONTAINER (scrolled_window), viewport);
		
	rs_toolbox_set_selected_snapshot(self, 0);
	rs_toolbox_set_photo(self, NULL);

	self->mute_from_sliders = FALSE;
	self->mute_from_photo = FALSE;
}

static void
dcp_profile_selected(RSProfileSelector *selector, RSDcpFile *dcp, RSToolbox *toolbox)
{
	if (toolbox->photo)
		rs_photo_set_dcp_profile(toolbox->photo, dcp);
}

static void
icc_profile_selected(RSProfileSelector *selector, RSIccProfile *icc, RSToolbox *toolbox)
{
	if (toolbox->photo)
		rs_photo_set_icc_profile(toolbox->photo, icc);
}

static void
add_profile_selected(RSProfileSelector *selector, RSToolbox *toolbox)
{
	rs_core_action_group_activate("AddProfile");
}

#ifndef WIN32
static void
conf_histogram_height_changed(GConfClient *client, guint cnxn_id, GConfEntry *entry, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (entry->value)
	{
		gint height = gconf_value_get_int(entry->value);
		height = CLAMP(height, 30, 500);
		gtk_widget_set_size_request(toolbox->histogram, 64, height);
	}
}
#endif

static void
notebook_switch_page(GtkNotebook *notebook, gpointer page, guint page_num, RSToolbox *toolbox)
{
	toolbox->selected_snapshot = page_num;

	/* Propagate event */
	g_signal_emit(toolbox, signals[SNAPSHOT_CHANGED], 0, toolbox->selected_snapshot);

	if (toolbox->photo)
		photo_settings_changed(toolbox->photo, page_num<<24|MASK_ALL, toolbox);
}

static void
basic_range_value_changed(GtkRange *range, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (!toolbox->mute_from_sliders && toolbox->photo)
	{
		/* Remember which snapshot we belong to */
		gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "rs-snapshot"));
		gfloat value = gtk_range_get_value(range);
		BasicSettings *basic = g_object_get_data(G_OBJECT(range), "rs-basic");
		g_object_set(toolbox->photo->settings[snapshot], basic->property_name, value, NULL);
	}

	if (toolbox->photo)
	{
		GtkAdjustment *adjustment = gtk_range_get_adjustment(range);
		gdouble upper = gtk_adjustment_get_upper(adjustment);
		/* Always label ... What?! */
		GtkLabel *label = g_object_get_data(G_OBJECT(range), "rs-value-label");
		if (upper >= 99.0)
			gui_label_set_text_printf(label, "%.0f", gtk_range_get_value(range));
		else
			gui_label_set_text_printf(label, "%.2f", gtk_range_get_value(range));
	}
}

static gboolean
basic_range_reset(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	BasicSettings *basic = g_object_get_data(G_OBJECT(user_data), "rs-basic");
	RSToolbox *toolbox = g_object_get_data(G_OBJECT(user_data), "rs-toolbox");
	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(user_data), "rs-snapshot"));

	g_assert(basic != NULL);
	g_assert(RS_IS_TOOLBOX(toolbox));

	gint mask = basic->mask;

	/* If we reset warmth or tint slider, we go back to camera whitebalance */
	if (toolbox->photo && 0 != (mask & MASK_WB))
	{
		rs_photo_set_wb_from_camera(toolbox->photo, snapshot);
	}
	else if (toolbox->photo)
	{
		RSCameraDb *db = rs_camera_db_get_singleton();
		gpointer p;
		RSSettings *s[3];

		if (rs_camera_db_photo_get_defaults(db, toolbox->photo, s, &p) && s[snapshot] && RS_IS_SETTINGS(s[snapshot]))
		{
			rs_settings_copy(s[snapshot], mask, toolbox->photo->settings[snapshot]);
		}
		else
			rs_object_class_property_reset(G_OBJECT(toolbox->photo->settings[snapshot]), basic->property_name);
	}


	return TRUE;
}

static gboolean
value_label_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
	GtkRange *range = GTK_RANGE(user_data);
	gdouble value = gtk_range_get_value(range);

	if (event->direction == GDK_SCROLL_UP)
		gtk_range_set_value(range, value+0.01);
	else
		gtk_range_set_value(range, value-0.01);

	return TRUE;
}

static gboolean
value_enterleaveclick(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{

	switch (event->type)
	{
		case GDK_ENTER_NOTIFY:
			gtk_widget_set_state(gtk_bin_get_child(GTK_BIN(widget)), GTK_STATE_PRELIGHT);
			break;
		case GDK_LEAVE_NOTIFY:
			gtk_widget_set_state(gtk_bin_get_child(GTK_BIN(widget)), GTK_STATE_NORMAL);
			break;
		case GDK_BUTTON_PRESS:
		{
			GtkRange *range = GTK_RANGE(user_data);
			GtkWidget *popup;

			/* Check if we can find a hidden window and just re-use that */
			if ((popup = g_object_get_data(G_OBJECT(range), "rs-popup")))
			{
				gtk_widget_show_all(popup);
				gtk_window_present(GTK_WINDOW(popup));
				break;
			}

			const gchar *blurp = g_object_get_data(G_OBJECT(range), "rs-blurb");
			GtkAdjustment* adjustment = gtk_range_get_adjustment(range);
			GtkWidget *spinner = gtk_spin_button_new(adjustment,
				gtk_adjustment_get_step_increment(adjustment)/10.0,
				(gtk_adjustment_get_upper(adjustment) > 99.0) ? 0 : 3);

			popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
			GtkWidget *label = gtk_label_new(blurp);
			GtkWidget *box = gtk_hbox_new(FALSE, 10);
			gtk_window_set_title(GTK_WINDOW(popup), blurp);
			gtk_window_set_position(GTK_WINDOW(popup), GTK_WIN_POS_MOUSE);
			gtk_window_set_transient_for(GTK_WINDOW(popup), rawstudio_window);
			gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_UTILITY);
			gtk_box_pack_start(GTK_BOX(box), label, FALSE, TRUE, 5);
			gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(spinner), FALSE, TRUE, 0);

			gtk_container_set_border_width(GTK_CONTAINER(box), 10);
			gtk_container_add(GTK_CONTAINER(popup), box);

			/* We save this for later by hiding it instead of closing */
			g_object_set_data(G_OBJECT(range), "rs-popup", popup);
			g_signal_connect (popup, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

			gtk_widget_show_all(popup);
			gtk_window_present(GTK_WINDOW(popup));
		}
		default:
			break;
	}

	/* Propagate - might result in a hover  */
	return FALSE;
}


static GtkRange *
basic_slider(RSToolbox *toolbox, const gint snapshot, GtkTable *table, const gint row, const BasicSettings *basic)
{
	static RSSettings *settings;
	static GMutex lock;
	g_mutex_lock(&lock);
	if (!settings)
		settings = rs_settings_new();
	g_mutex_unlock(&lock);

	GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(settings), basic->property_name);
	GParamSpecFloat *fspec = G_PARAM_SPEC_FLOAT(spec);
	
	GtkWidget *label = gui_label_new_with_mouseover(g_param_spec_get_nick(spec), _("Reset"));
	gtk_widget_set_tooltip_text(label, g_strconcat(g_param_spec_get_blurb(spec),_(". Click to reset value"), NULL));
	GtkWidget *seperator1 = gtk_vseparator_new();
	GtkWidget *seperator2 = gtk_vseparator_new();
	GtkWidget *scale = gtk_hscale_new_with_range(fspec->minimum, fspec->maximum, basic->step);
	GtkWidget *event = gtk_event_box_new();
	GtkWidget *value_label = gtk_label_new(NULL);
	gtk_widget_set_tooltip_text(value_label, g_strconcat(g_param_spec_get_blurb(spec),_(". Click to edit value"), NULL));

	gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
	/* Set default value */
	gtk_range_set_value(GTK_RANGE(scale), fspec->default_value);
	gtk_widget_set_sensitive(scale, FALSE);

	/* Remember which snapshot we belong to */
	g_object_set_data(G_OBJECT(scale), "rs-snapshot", GINT_TO_POINTER(snapshot));
	g_object_set_data(G_OBJECT(scale), "rs-basic", (gpointer) basic);
	g_object_set_data(G_OBJECT(scale), "rs-value-label", value_label);
	g_object_set_data(G_OBJECT(scale), "rs-toolbox", toolbox);
	g_object_set_data(G_OBJECT(scale), "rs-blurb", (gpointer) g_param_spec_get_blurb(spec));

	gtk_scale_set_value_pos(GTK_SCALE(scale), GTK_POS_RIGHT);
	g_signal_connect(scale, "value-changed", G_CALLBACK(basic_range_value_changed), toolbox);

	gtk_widget_set_events(label, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(label, "button_press_event", G_CALLBACK (basic_range_reset), GTK_RANGE(scale));

	if (fspec->maximum >= 99.0)
		gui_label_set_text_printf(GTK_LABEL(value_label), "%.0f", fspec->default_value);
	else
		gui_label_set_text_printf(GTK_LABEL(value_label), "%.2f", fspec->default_value);

	gtk_label_set_width_chars(GTK_LABEL(value_label), 5);
	gtk_widget_set_events(event, GDK_SCROLL_MASK|GDK_ENTER_NOTIFY_MASK|GDK_LEAVE_NOTIFY_MASK|GDK_BUTTON_PRESS_MASK);
	gtk_container_add(GTK_CONTAINER(event), value_label);
	g_signal_connect(event, "scroll-event", G_CALLBACK (value_label_scroll), GTK_RANGE(scale));
	g_signal_connect(event, "button-press-event", G_CALLBACK (value_enterleaveclick), GTK_RANGE(scale));
	g_signal_connect(event, "enter-notify-event", G_CALLBACK(value_enterleaveclick), NULL);
	g_signal_connect(event, "leave-notify-event", G_CALLBACK(value_enterleaveclick), NULL);

	gtk_table_attach(table, label,      0, 1, row, row+1, GTK_SHRINK|GTK_FILL, GTK_SHRINK, 0, 0);
	gtk_table_attach(table, seperator1, 1, 2, row, row+1, GTK_SHRINK,          GTK_FILL, 0, 0);
	gtk_table_attach(table, scale,      2, 3, row, row+1, GTK_EXPAND|GTK_FILL, GTK_SHRINK, 0, 0);
	gtk_table_attach(table, seperator2, 3, 4, row, row+1, GTK_SHRINK,          GTK_FILL, 0, 0);
	gtk_table_attach(table, event,      4, 5, row, row+1, GTK_SHRINK,          GTK_SHRINK, 0, 0);

	return GTK_RANGE(scale);
}

static void
curve_changed(GtkWidget *widget, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	/* Remember which snapshot we belong to */
	gint snapshot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "rs-snapshot"));

	if (toolbox->mute_from_sliders)
		return;

	/* Copy curve to photo if any */
	if (toolbox->photo)
	{
		gfloat *knots;
		guint nknots;
		toolbox->mute_from_photo = TRUE;
		rs_curve_widget_get_knots(RS_CURVE_WIDGET(toolbox->curve[snapshot]), &knots, &nknots);
		rs_settings_set_curve_knots(toolbox->photo->settings[snapshot], knots, nknots);
		g_free(knots);
		toolbox->mute_from_photo = FALSE;
	}
}

static void
curve_context_callback_save(GtkMenuItem *menuitem, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(user_data);
	GtkWidget *fc;
	gchar *dir;

	fc = gtk_file_chooser_dialog_new (_("Export File"), NULL,
		GTK_FILE_CHOOSER_ACTION_SAVE,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(fc), GTK_RESPONSE_ACCEPT);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (fc), TRUE);

	/* Set default directory */
	dir = g_build_filename(rs_confdir_get(), "curves", NULL);
	g_mkdir_with_parents(dir, 00755);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (fc), dir);
	g_free(dir);

	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fc));
		if (filename)
		{
			if (!g_str_has_suffix(filename, ".rscurve"))
			{
				GString *gs;
				gs = g_string_new(filename);
				g_string_append(gs, ".rscurve");
				g_free(filename);
				filename = gs->str;
				g_string_free(gs, FALSE);
			}
			rs_curve_widget_save(curve, filename);
			g_free(filename);
		}
	}
	gtk_widget_destroy(fc);
}

static void
curve_context_callback_open(GtkMenuItem *menuitem, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(user_data);
	GtkWidget *fc;
	gchar *dir;

	fc = gtk_file_chooser_dialog_new (_("Open curve ..."), NULL,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(fc), GTK_RESPONSE_ACCEPT);

	/* Set default directory */
	dir = g_build_filename(rs_confdir_get(), "curves", NULL);
	g_mkdir_with_parents(dir, 00755);
	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (fc), dir);
	g_free(dir);

	if (gtk_dialog_run (GTK_DIALOG (fc)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (fc));
		if (filename)
		{
			rs_curve_widget_load(curve, filename);
			g_free(filename);
		}
	}
	gtk_widget_destroy(fc);
}

static void
curve_context_callback_reset(GtkMenuItem *menuitem, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(user_data);

	gulong handler = g_signal_handler_find(curve, G_SIGNAL_MATCH_FUNC, 0, 0, NULL, curve_changed, NULL);
	g_signal_handler_block(curve, handler);

	rs_curve_widget_reset(curve);
	rs_curve_widget_add_knot(curve, 0.0,0.0);
	g_signal_handler_unblock(curve, handler);
	rs_curve_widget_add_knot(curve, 1.0,1.0);
}

static void
curve_context_callback_white_black_point(GtkMenuItem *menuitem, gpointer user_data)
{
  rs_curve_auto_adjust_ends(GTK_WIDGET(user_data));
}

static void
curve_context_callback_preset(GtkMenuItem *menuitem, gpointer user_data)
{
	RSCurveWidget *curve = RS_CURVE_WIDGET(user_data);

	gchar *filename;
	filename = g_object_get_data(G_OBJECT(menuitem), "filename");
	if (filename)
	{
		gchar *fullname = g_build_filename(rs_confdir_get(), "curves", filename, NULL);
		rs_curve_widget_load(curve, fullname);
		g_free(fullname);
	}
}

static void
rs_gtk_menu_item_set_label(GtkMenuItem *menu_item, const gchar *label)
{
	gtk_menu_item_set_label(menu_item, label);
}

static void
curve_context_callback(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *i, *menu = gtk_menu_new();
	gint n=0;

	const gchar *filename;
	GList *list = NULL;
	gchar *dirpath = g_build_filename(rs_confdir_get(), "curves", NULL);
	GDir *dir = g_dir_open(dirpath, 0, NULL);
	if (dir)
	{
		while((filename = g_dir_read_name(dir)))
			if (g_str_has_suffix(filename, ".rscurve"))
				list = g_list_prepend(list, g_strdup(filename));
		g_dir_close(dir);
	}
	g_free(dirpath);

	list = g_list_sort(list, (GCompareFunc) g_strcmp0);

	GList *p = list;
	while (p)
	{
		gchar *name = (gchar *) p->data;

		gchar *ext = g_strrstr(name, ".rscurve");
		if (ext)
		{
			ext[0] = '\0';

			if (n == 0)
			{
				i = gtk_image_menu_item_new_with_label(_("Select Saved Curve"));
				gtk_widget_show (i);
				gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
			}

			i = gtk_image_menu_item_new_from_stock(GTK_STOCK_REVERT_TO_SAVED, NULL);
			rs_gtk_menu_item_set_label(GTK_MENU_ITEM(i), name);
			gtk_widget_show (i);
			gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
			g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_preset), widget);

			ext[0] = '.';
			g_object_set_data_full(G_OBJECT(i), "filename", name, g_free);
		}
		else
			g_free(name);

		p = g_list_next(p);
	}

	g_list_free(list);

	/* If any files were added before this, add a seperator */
	if (n > 0)
	{
		i = gtk_separator_menu_item_new();
		gtk_widget_show (i);
		gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	}

	i = gtk_image_menu_item_new_with_label (_("Select Action"));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;

	i = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
	rs_gtk_menu_item_set_label(GTK_MENU_ITEM(i), _("Open curve ..."));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_open), widget);

	i = gtk_image_menu_item_new_from_stock(GTK_STOCK_SAVE_AS, NULL);
	rs_gtk_menu_item_set_label(GTK_MENU_ITEM(i), _("Save curve as ..."));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_save), widget);

	i = gtk_image_menu_item_new_from_stock(GTK_STOCK_REFRESH, NULL);
	rs_gtk_menu_item_set_label(GTK_MENU_ITEM(i), _("Reset curve"));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_reset), widget);

	i = gtk_menu_item_new_with_label (_("Auto adjust curve ends"));
	gtk_widget_show (i);
	gtk_menu_attach (GTK_MENU (menu), i, 0, 1, n, n+1); n++;
	g_signal_connect (i, "activate", G_CALLBACK (curve_context_callback_white_black_point), widget);
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, 0, GDK_CURRENT_TIME);
}

static GtkWidget*
basic_label(RSToolbox *toolbox, const gint snapshot, GtkTable *table, const gint row, GtkWidget *widget)
{
	GtkWidget *label = gtk_label_new(NULL);
	if (widget)
	{
		GtkWidget *hbox = gtk_hbox_new(FALSE, 2);

		gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, TRUE, 2);
		gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 2);
		gtk_table_attach(table, hbox, 0, 5, 0, 1, GTK_EXPAND, GTK_FILL, 0, 0);     
	}
	else
	{
		gtk_table_attach(table, label, 0, 5, 0, 1, GTK_EXPAND, GTK_FILL, 0, 0);
	}

	return label;
}

void 
toolbox_edit_lens_clicked(GtkButton *button, gpointer user_data)
{
	gint i;
	RSToolbox *toolbox = user_data;
	if (toolbox->rs_lens)
	{
		gtk_dialog_run(rs_lens_db_editor_single_lens(toolbox->rs_lens));
		/* Make sure we set to all 3 snapshots */
		for(i=0; i<3; i++) toolbox_lens_set_label(toolbox, i);
		RSLensDb *lens_db = rs_lens_db_get_default();
		rs_lens_db_save(lens_db);
		rs_photo_lens_updated(toolbox->photo);
	}
}

static GtkWidget *
new_snapshot_page(RSToolbox *toolbox, const gint snapshot)
{
	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	GtkTable *table, *channelmixertable, *lenstable;
	gint row;

	table = GTK_TABLE(gtk_table_new(NBASICS, 5, FALSE));
	channelmixertable = GTK_TABLE(gtk_table_new(NCHANNELMIXER, 5, FALSE));
	lenstable = GTK_TABLE(gtk_table_new(NLENS, 5, FALSE));

	/* Add basic sliders */
	for(row=0;row<NBASICS;row++)
		toolbox->ranges[snapshot][row] = basic_slider(toolbox, snapshot, table, row, &basic[row]);
	for(row=0;row<NCHANNELMIXER;row++)
		toolbox->channelmixer[snapshot][row] = basic_slider(toolbox, snapshot, channelmixertable, row, &channelmixer[row]);

	/* ROW HARDCODED TO 0 */
	toolbox->lensbutton[snapshot] = gtk_button_new_with_label(_("Edit Lens"));
	toolbox->lenslabel[snapshot] = basic_label(toolbox, snapshot, lenstable, row, toolbox->lensbutton[snapshot]);
	toolbox_lens_set_label(toolbox, snapshot);

	g_signal_connect(toolbox->lensbutton[snapshot], "clicked", G_CALLBACK(toolbox_edit_lens_clicked), toolbox);
	
	/* We already used one row in the table for the label, so we'll add 1 to the row argument */
	for(row=0;row<NLENS;row++)
		toolbox->lens[snapshot][row] = basic_slider(toolbox, snapshot, lenstable, row+1, &lens[row]);

	/* Add curve editor */
	toolbox->curve[snapshot] = rs_curve_widget_new();
	g_object_set_data(G_OBJECT(toolbox->curve[snapshot]), "rs-snapshot", GINT_TO_POINTER(snapshot));
	g_signal_connect(toolbox->curve[snapshot], "changed", G_CALLBACK(curve_changed), toolbox);
	g_signal_connect(toolbox->curve[snapshot], "right-click", G_CALLBACK(curve_context_callback), NULL);

	/* Pack everything nice */
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Basic"), GTK_WIDGET(table), "show_basic", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Channel Mixer"), GTK_WIDGET(channelmixertable), "show_channelmixer", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Lens Correction"), GTK_WIDGET(lenstable), "show_lens", TRUE), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), gui_box(_("Curve"), toolbox->curve[snapshot], "show_curve", TRUE), FALSE, FALSE, 0);

	return vbox;
}

static void
gui_transform_rot90_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("RotateClockwise");
}

static void
gui_transform_rot270_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("RotateCounterClockwise");
}

static void
gui_transform_mirror_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("Mirror");
}

static void
gui_transform_flip_clicked(GtkWidget *w, RS_BLOB *rs)
{
	rs_core_action_group_activate("Flip");
}

static GtkWidget *
new_transform(RSToolbox *toolbox, gboolean show)
{
	GtkWidget *hbox;
	GtkWidget *flip;
	GtkWidget *mirror;
	GtkWidget *rot90;
	GtkWidget *rot270;

	hbox = gtk_hbox_new(FALSE, 0);
	flip = GTK_WIDGET(gtk_tool_button_new_from_stock(RS_STOCK_FLIP));
	mirror = GTK_WIDGET(gtk_tool_button_new_from_stock(RS_STOCK_MIRROR));
	rot90 = GTK_WIDGET(gtk_tool_button_new_from_stock(RS_STOCK_ROTATE_CLOCKWISE));
	rot270 = GTK_WIDGET(gtk_tool_button_new_from_stock(RS_STOCK_ROTATE_COUNTER_CLOCKWISE));

	gtk_widget_set_tooltip_text(flip, _("Flip the photo over the x-axis"));
	gtk_widget_set_tooltip_text(mirror, _("Mirror the photo over the y-axis"));
	gtk_widget_set_tooltip_text(rot90, _("Rotate the photo 90 degrees clockwise"));
	gtk_widget_set_tooltip_text(rot270, _("Rotate the photo 90 degrees counter clockwise"));

	g_signal_connect(flip, "clicked", G_CALLBACK (gui_transform_flip_clicked), NULL);
	g_signal_connect(mirror, "clicked", G_CALLBACK (gui_transform_mirror_clicked), NULL);
	g_signal_connect(rot90, "clicked", G_CALLBACK (gui_transform_rot90_clicked), NULL);
	g_signal_connect(rot270, "clicked", G_CALLBACK (gui_transform_rot270_clicked), NULL);

	gtk_box_pack_start(GTK_BOX (hbox), flip, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), mirror, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), rot270, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX (hbox), rot90, FALSE, FALSE, 0);

	return gui_box(_("Transforms"), hbox, "show_transforms", show);
}

GtkWidget *
rs_toolbox_new(void)
{
	return g_object_new (RS_TYPE_TOOLBOX, NULL);
}

static void photo_profile_changed(RS_PHOTO *photo, gpointer profile, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (toolbox->mute_from_sliders)
		return;

	/* Update histogram */
	rs_histogram_redraw(RS_HISTOGRAM_WIDGET(toolbox->histogram));
	
	/* Update histogram in curve editor */
	rs_curve_draw_histogram(RS_CURVE_WIDGET(toolbox->curve[toolbox->selected_snapshot]));

	/* Update GUI */
	if (rs_photo_get_dcp_profile(photo))
		rs_profile_selector_select_profile(toolbox->selector, rs_photo_get_dcp_profile(photo));
	if (rs_photo_get_icc_profile(photo))
		rs_profile_selector_select_profile(toolbox->selector, rs_photo_get_icc_profile(photo));
}

static void
photo_settings_changed(RS_PHOTO *photo, RSSettingsMask mask, gpointer user_data)
{
	const gint snapshot = mask>>24;
	mask &= 0x00ffffff;
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	if (!toolbox->mute_from_photo)
		toolbox_copy_from_photo(toolbox, snapshot, mask, photo);

	
	if (mask)
	{
		rs_filter_set_recursive(toolbox->histogram_input,
			"bounding-box", TRUE,
			"orientation", photo->orientation,
			"rectangle", rs_photo_get_crop(photo),
			"angle", rs_photo_get_angle(photo),
			"settings", photo->settings[toolbox->selected_snapshot],
		   NULL);
	}
	/* Update histogram */
	rs_histogram_redraw(RS_HISTOGRAM_WIDGET(toolbox->histogram));
	
	/* Update histogram in curve editor */
	rs_curve_draw_histogram(RS_CURVE_WIDGET(toolbox->curve[toolbox->selected_snapshot]));
}

static void 
photo_wb_changed(RSSettings *settings, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);
	if (toolbox->mute_from_photo)
		return;

	gint snapshot;
	for(snapshot=0;snapshot<3;snapshot++)
	{
		if (settings == toolbox->photo->settings[snapshot])
		{
			photo_settings_changed(toolbox->photo, MASK_WB|(snapshot<<24), toolbox);
		}
	}
}

static void
photo_spatial_changed(RS_PHOTO *photo, gpointer user_data)
{
	RSToolbox *toolbox = RS_TOOLBOX(user_data);

	/* Force update of histograms */
	photo_settings_changed(photo, MASK_ALL, toolbox);

	/* FIXME: Deal with curve */
}

static void
photo_finalized(gpointer data, GObject *where_the_object_was)
{
	gint snapshot,i;
	RSToolbox *toolbox = RS_TOOLBOX(data);

	toolbox->photo = NULL;

	/* Reset all sliders and make them insensitive */
	for(snapshot=0;snapshot<3;snapshot++)
	{
		for(i=0;i<NBASICS;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->ranges[snapshot][i]), FALSE);
		}
		for(i=0;i<NCHANNELMIXER;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->channelmixer[snapshot][i]), FALSE);
		}
		for(i=0;i<NLENS;i++)
		{
			gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lens[snapshot][i]), FALSE);
		}
		rs_curve_widget_reset(RS_CURVE_WIDGET(toolbox->curve[snapshot]));
		rs_curve_widget_add_knot(RS_CURVE_WIDGET(toolbox->curve[snapshot]), 0.0,0.0);
		rs_curve_widget_add_knot(RS_CURVE_WIDGET(toolbox->curve[snapshot]), 1.0,1.0);
	}
}

static void
toolbox_copy_from_photo(RSToolbox *toolbox, const gint snapshot, const RSSettingsMask mask, RS_PHOTO *photo)
{
	gint i;

	if (mask)
	{
		toolbox->mute_from_sliders = TRUE;

		/* Update basic sliders */
		for(i=0;i<NBASICS;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], basic[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->ranges[snapshot][i], value);
			}

		/* Update channel mixer */
		for(i=0;i<NCHANNELMIXER;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], channelmixer[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->channelmixer[snapshot][i], value);
			}

		/* Update lens */
		for(i=0;i<NLENS;i++)
			if (mask)
			{
				gfloat value;
				g_object_get(toolbox->photo->settings[snapshot], lens[i].property_name, &value, NULL);
				gtk_range_set_value(toolbox->lens[snapshot][i], value);
			}

		/* Update curve */
		if(mask & MASK_CURVE)
		{
			gfloat *knots = rs_settings_get_curve_knots(toolbox->photo->settings[snapshot]);
			gint nknots = rs_settings_get_curve_nknots(toolbox->photo->settings[snapshot]);
			rs_curve_widget_reset(RS_CURVE_WIDGET(toolbox->curve[snapshot]));
			rs_curve_widget_set_knots(RS_CURVE_WIDGET(toolbox->curve[snapshot]), knots, nknots);
			g_free(knots);
		}
		toolbox->mute_from_sliders = FALSE;
	}
}

void
toolbox_lens_set_label(RSToolbox *toolbox, gint snapshot)
{
	const gchar *lens_text = NULL;

	if(toolbox->rs_lens)
	{
		if (!rs_lens_get_lensfun_model(toolbox->rs_lens))
			lens_text = _("Lens Unknown");
		else if (!rs_lens_get_lensfun_enabled(toolbox->rs_lens))			
			lens_text = _("Lens Disabled");
		else
			lens_text = rs_lens_get_lensfun_model(toolbox->rs_lens);
		gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lensbutton[snapshot]), TRUE);
	} 
	else if(toolbox->photo)
	{
		if (!toolbox->photo->metadata->lens_identifier)
			lens_text = _("No Lens Information");
		else
			lens_text = _("Camera Unknown");
		gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lensbutton[snapshot]), FALSE);
	} 
	else
	{
		lens_text = _("No Photo");
		gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lensbutton[snapshot]), FALSE);
	}

	GString *temp = g_string_new(lens_text);
	if (temp->len > 25)
	{
		temp = g_string_set_size(temp, 22);
		temp = g_string_append(temp, "...");
	}

	gtk_label_set_markup(GTK_LABEL(toolbox->lenslabel[snapshot]), g_strdup_printf("<small>%s</small>", temp->str));
	gtk_widget_set_tooltip_text(toolbox->lenslabel[snapshot], lens_text);
}

void
rs_toolbox_set_photo(RSToolbox *toolbox, RS_PHOTO *photo)
{
	gint snapshot;
	gint i;

	g_assert (RS_IS_TOOLBOX(toolbox));
	g_assert (RS_IS_PHOTO(photo) || (photo == NULL));

	if (toolbox->photo)
		g_object_weak_unref(G_OBJECT(toolbox->photo), (GWeakNotify) photo_finalized, toolbox);

	toolbox->photo = photo;

	toolbox->mute_from_sliders = TRUE;
	if (toolbox->photo)
	{
		g_object_weak_ref(G_OBJECT(toolbox->photo), (GWeakNotify) photo_finalized, toolbox);
		g_signal_connect(G_OBJECT(toolbox->photo), "settings-changed", G_CALLBACK(photo_settings_changed), toolbox);
		g_signal_connect(G_OBJECT(toolbox->photo), "spatial-changed", G_CALLBACK(photo_spatial_changed), toolbox);
		g_signal_connect(G_OBJECT(toolbox->photo), "profile-changed", G_CALLBACK(photo_profile_changed), toolbox);

		for(snapshot=0;snapshot<3;snapshot++)
		{
			/* Copy all settings */
			g_signal_connect(G_OBJECT(toolbox->photo->settings[snapshot]), "wb-recalculated", G_CALLBACK(photo_wb_changed), toolbox);
			toolbox_copy_from_photo(toolbox, snapshot, MASK_ALL, toolbox->photo);
			toolbox->mute_from_sliders = TRUE;

			/* Set the basic types sensitive */
			for(i=0;i<NBASICS;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->ranges[snapshot][i]), TRUE);
			for(i=0;i<NCHANNELMIXER;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->channelmixer[snapshot][i]), TRUE);

			if (photo->metadata->lens_identifier) {
				RSLensDb *lens_db = rs_lens_db_get_default();
				toolbox->rs_lens = rs_lens_db_get_from_identifier(lens_db, photo->metadata->lens_identifier);
			} else {
				toolbox->rs_lens = NULL;
			}
			toolbox_lens_set_label(toolbox, snapshot);

			for(i=0;i<NLENS;i++)
				gtk_widget_set_sensitive(GTK_WIDGET(toolbox->lens[snapshot][i]), TRUE);
		}
	}
	else
		/* This will reset everything */
		photo_finalized(toolbox, NULL);

	/* Enable Embedded Profile, if present */
	gboolean embedded_present = photo && (!!photo->embedded_profile);
	RSProfileFactory *factory = rs_profile_factory_new_default();
	if (embedded_present && photo->input_response)
	{
		RSColorSpace *input_space = rs_filter_param_get_object_with_type(RS_FILTER_PARAM(photo->input_response), "embedded-colorspace", RS_TYPE_COLOR_SPACE);

		if (input_space)
		{
			const RSIccProfile *icc_profile;
			icc_profile = rs_color_space_get_icc_profile(input_space, TRUE);

			rs_profile_factory_set_embedded_profile(factory, icc_profile);
			embedded_present = TRUE;
		} 
	}
	else
	{
		rs_profile_factory_set_embedded_profile(factory, NULL);
	}

	/* Update profile selector */
	if (photo && photo->metadata)
	{
		RSProfileFactory *factory = rs_profile_factory_new_default();
		GtkTreeModelFilter *filter;

		if (g_strcmp0(photo->metadata->make_ascii, toolbox->last_camera.make) != 0 || 
		    g_strcmp0(photo->metadata->model_ascii, toolbox->last_camera.model) != 0)
		{
			g_free(toolbox->last_camera.make);
			g_free(toolbox->last_camera.model);

			toolbox->last_camera.make = g_strdup(photo->metadata->make_ascii);
			toolbox->last_camera.model = g_strdup(photo->metadata->model_ascii);
			toolbox->last_camera.unique_id = rs_profile_camera_find(photo->metadata->make_ascii, photo->metadata->model_ascii);
		}

		if (embedded_present)
			filter = rs_dcp_factory_get_compatible_as_model(factory, "Embedded");
		else if (toolbox->last_camera.unique_id)
			filter = rs_dcp_factory_get_compatible_as_model(factory, toolbox->last_camera.unique_id);
		else
			filter = rs_dcp_factory_get_compatible_as_model(factory, photo->metadata->model_ascii);
		rs_profile_selector_set_model_filter(toolbox->selector, filter);
	}

	/* Find current profile and mark it active */
	if (photo)
	{
		RSDcpFile *dcp_profile = rs_photo_get_dcp_profile(photo);
		RSIccProfile *icc_profile = rs_photo_get_icc_profile(photo);

		if (embedded_present)
			gtk_combo_box_set_active(GTK_COMBO_BOX(toolbox->selector), 0);
		else if (dcp_profile)
			rs_profile_selector_select_profile(toolbox->selector, dcp_profile);
		else if (icc_profile)
			rs_profile_selector_select_profile(toolbox->selector, icc_profile);
	}
	toolbox->mute_from_sliders = FALSE;

	/* Update histogram in curve editor */
	rs_curve_draw_histogram(RS_CURVE_WIDGET(toolbox->curve[toolbox->selected_snapshot]));
	/* Update histogram */
	rs_histogram_redraw(RS_HISTOGRAM_WIDGET(toolbox->histogram));
	gtk_widget_set_sensitive(toolbox->transforms, !!(toolbox->photo));
}

GtkWidget *
rs_toolbox_add_widget(RSToolbox *toolbox, GtkWidget *widget, const gchar *title)
{
	GtkWidget *ret = widget;

	g_assert(RS_IS_TOOLBOX(toolbox));
	g_assert(GTK_IS_WIDGET(widget));

	if (title)
	{
		ret = gtk_frame_new(title);
		gtk_container_set_border_width(GTK_CONTAINER(ret), 3);
		gtk_container_add(GTK_CONTAINER(ret), widget);
	}

	gtk_box_pack_start(toolbox->toolbox, ret, FALSE, FALSE, 1);

	return ret;
}

gint
rs_toolbox_get_selected_snapshot(RSToolbox *toolbox)
{
	g_assert(RS_IS_TOOLBOX(toolbox));

	return toolbox->selected_snapshot;
}

void
rs_toolbox_set_selected_snapshot(RSToolbox *toolbox, const gint snapshot)
{
	gtk_notebook_set_current_page(GTK_NOTEBOOK(toolbox->notebook), snapshot);
}

void rs_toolbox_set_histogram_input(RSToolbox * toolbox, RSFilter *input, RSColorSpace *display_color_space)
{
	g_assert(RS_IS_TOOLBOX(toolbox));
	g_assert(RS_IS_FILTER(input));
	gint i;

	toolbox->histogram_input = input;
	toolbox->histogram_colorspace = display_color_space;
	for( i = 0 ; i < 3 ; i++)
		rs_curve_set_input(RS_CURVE_WIDGET(toolbox->curve[i]), input, display_color_space);
	rs_histogram_set_input(RS_HISTOGRAM_WIDGET(toolbox->histogram), input, display_color_space);
}

static void
action_changed(GtkRadioAction *radioaction, GtkRadioAction *current, RSToolbox *toolbox)
{
	gtk_notebook_set_current_page(GTK_NOTEBOOK(toolbox->notebook), gtk_radio_action_get_current_value(radioaction));
}

static void
action_previous(GtkAction *action, RSToolbox *toolbox)
{
	gtk_notebook_prev_page(GTK_NOTEBOOK(toolbox->notebook));
}

static void
action_next(GtkAction *action, RSToolbox *toolbox)
{
	gtk_notebook_next_page(GTK_NOTEBOOK(toolbox->notebook));
}

extern void
rs_toolbox_register_actions(RSToolbox *toolbox)
{
	g_assert(RS_IS_TOOLBOX(toolbox));

	GtkRadioActionEntry select_snapshot[] = {
	{ "SnapshotA", NULL, _(" A "), "<alt>1", NULL, 0 },
	{ "SnapshotB", NULL, _(" B "), "<alt>2", NULL, 1 },
	{ "SnapshotC", NULL, _(" C "), "<alt>3", NULL, 2 },
	};
	static guint n_select_snapshot = G_N_ELEMENTS (select_snapshot);

	GtkActionEntry actionentries[] = {
	{ "SnapshotPrevious", GTK_STOCK_GO_BACK, _("_Previous"), "<control>Page_Up", NULL, G_CALLBACK(action_previous) },
	{ "SnapshotNext", GTK_STOCK_GO_FORWARD, _("_Next"), "<control>Page_Down", NULL, G_CALLBACK(action_next) },
	};
	static guint n_actionentries = G_N_ELEMENTS (actionentries);

	rs_core_action_group_add_radio_actions(select_snapshot, n_select_snapshot, 0, G_CALLBACK(action_changed), toolbox);
	rs_core_action_group_add_actions(actionentries, n_actionentries, toolbox);
}

extern void
rs_toolbox_hover_value_updated(RSToolbox *toolbox, const guchar *rgb_value)
{
	gint i;
	g_assert(RS_IS_TOOLBOX(toolbox));
	rs_histogram_set_highlight(RS_HISTOGRAM_WIDGET(toolbox->histogram), rgb_value);
	for( i = 0 ; i < 3 ; i++)
		rs_curve_set_highlight(RS_CURVE_WIDGET(toolbox->curve[i]), rgb_value);
}

extern GtkWidget *
rs_toolbox_get_curve(RSToolbox *toolbox, gint setting)
{
  return toolbox->curve[setting];
}
