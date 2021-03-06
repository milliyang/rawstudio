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

/* Plugin tmpl version 4 */

#include <rawstudio.h>

#define RS_TYPE_INPUT_IMAGE16 (rs_input_image16_type)
#define RS_INPUT_IMAGE16(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_INPUT_IMAGE16, RSInputImage16))
#define RS_INPUT_IMAGE16_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_INPUT_IMAGE16, RSInputImage16Class))
#define RS_IS_INPUT_IMAGE16(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_INPUT_IMAGE16))

typedef struct _RSInputImage16 RSInputImage16;
typedef struct _RSInputImage16Class RSInputImage16Class;

struct _RSInputImage16 {
	RSFilter parent;

	RSFilterResponse *image_response;
	RS_IMAGE16 *image;
	gchar *filename;
	RSColorSpace *colorspace;
};

struct _RSInputImage16Class {
	RSFilterClass parent_class;
};

RS_DEFINE_FILTER(rs_input_image16, RSInputImage16)

enum {
	PROP_0,
	PROP_IMAGE,
	PROP_FILENAME,
	PROP_COLOR_SPACE
};

static void get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);
static RSFilterResponse *get_image(RSFilter *filter, const RSFilterRequest *request);
static RSFilterResponse *get_size(RSFilter *filter, const RSFilterRequest *request);
static void dispose (GObject *object);

static RSFilterClass *rs_input_image16_parent_class = NULL;

G_MODULE_EXPORT void
rs_plugin_load(RSPlugin *plugin)
{
	/* Let the GType system register our type */
	rs_input_image16_get_type(G_TYPE_MODULE(plugin));
}

static void
rs_input_image16_class_init (RSInputImage16Class *klass)
{
	RSFilterClass *filter_class = RS_FILTER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);

	rs_input_image16_parent_class = g_type_class_peek_parent (klass);

	object_class->get_property = get_property;
	object_class->set_property = set_property;
	object_class->dispose = dispose;

	g_object_class_install_property(object_class,
		PROP_IMAGE, g_param_spec_object (
			"image",
			"image",
			"RSFilterResponse to use as input",
			RS_TYPE_FILTER_RESPONSE,
			G_PARAM_READWRITE)
	);
	g_object_class_install_property(object_class,
		PROP_FILENAME, g_param_spec_string(
			"filename", "filename", "filename",
			NULL, G_PARAM_READWRITE));
	g_object_class_install_property(object_class,
		PROP_COLOR_SPACE, g_param_spec_object(
			"color-space", "color-space", "A colorspace to assign input",
			RS_TYPE_COLOR_SPACE, G_PARAM_READWRITE));

	filter_class->name = "Import a RS_IMAGE16 into a RSFilter chain";
	filter_class->get_image = get_image;
	filter_class->get_size = get_size;
}

static void
rs_input_image16_init (RSInputImage16 *input_image16)
{
	input_image16->image = NULL;
	input_image16->image_response = NULL;
}

static void
get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	RSInputImage16 *input_image16 = RS_INPUT_IMAGE16(object);
	switch (property_id)
	{
		case PROP_IMAGE:
			g_value_set_object(value, input_image16->image_response);
			break;
		case PROP_FILENAME:
			g_value_set_string(value, input_image16->filename);
			break;
		case PROP_COLOR_SPACE:
			g_value_set_object(value, input_image16->colorspace);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	RSInputImage16 *input_image16 = RS_INPUT_IMAGE16(object);
	switch (property_id)
	{
		case PROP_IMAGE:
			/* Clean up */
			if (input_image16->image)
				g_object_unref(input_image16->image);
			input_image16->image = NULL;
			if (input_image16->image_response)
				g_object_unref(input_image16->image_response);
			
			input_image16->image_response = g_object_ref(g_value_get_object(value));
			input_image16->image = rs_filter_response_get_image(input_image16->image_response);
			rs_filter_changed(RS_FILTER(input_image16), RS_FILTER_CHANGED_DIMENSION);
			break;
		case PROP_FILENAME:
			g_free(input_image16->filename);
			input_image16->filename = g_value_dup_string(value);
			break;
		case PROP_COLOR_SPACE:
			if (input_image16->colorspace)
				g_object_unref(input_image16->colorspace);
			input_image16->colorspace = g_object_ref(g_value_get_object(value));
			rs_filter_changed(RS_FILTER(input_image16), RS_FILTER_CHANGED_DIMENSION);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
	}
}

static void
dispose (GObject *object)
{
	RSInputImage16 *input_image16 = RS_INPUT_IMAGE16(object);

	if (input_image16->image_response)
		g_object_unref(input_image16->image_response);
	if (input_image16->image)
		g_object_unref(input_image16->image);

	/* Chain up */
	G_OBJECT_CLASS (rs_input_image16_parent_class)->dispose (object);
}

static RSFilterResponse *
get_image(RSFilter *filter, const RSFilterRequest *request)
{
	RSInputImage16 *input_image16 = RS_INPUT_IMAGE16(filter);
	if (RS_IS_FILTER_RESPONSE(input_image16->image_response))
	{
		RSFilterResponse *response;
		response = rs_filter_response_clone(RS_FILTER_RESPONSE(input_image16->image_response));
		rs_filter_response_set_image(response, input_image16->image);

		if (RS_IS_COLOR_SPACE(input_image16->colorspace))
			rs_filter_param_set_object(RS_FILTER_PARAM(response), "colorspace", input_image16->colorspace);

		return response;
	}

	return rs_filter_response_new();
}

static RSFilterResponse *
get_size(RSFilter *filter, const RSFilterRequest *request)
{
	RSInputImage16 *input_image16 = RS_INPUT_IMAGE16(filter);

	RSFilterResponse *response = rs_filter_response_clone(RS_FILTER_RESPONSE(input_image16->image_response));

	if (input_image16->image)
	{
		rs_filter_response_set_width(response, input_image16->image->w);
		rs_filter_response_set_height(response, input_image16->image->h);
	}

	return response;
}
