/*
 * Copyright (C) 2006-2008 Anders Brander <anders@brander.dk> and 
 * Anders Kvist <akv@lnxbx.dk>
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

#ifndef RS_METADATA_H
#define RS_METADATA_H

#include <glib-object.h>
#include "rawstudio.h" /* FIXME: This is so broken! */

G_BEGIN_DECLS

#define RS_TYPE_METADATA rs_metadata_get_type()
#define RS_METADATA(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RS_TYPE_METADATA, RSMetadata))
#define RS_METADATA_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), RS_TYPE_METADATA, RSMetadataClass))
#define RS_IS_METADATA(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RS_TYPE_METADATA))
#define RS_IS_METADATA_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RS_TYPE_METADATA))
#define RS_METADATA_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), RS_TYPE_METADATA, RSMetadataClass))

struct _RSMetadata {
	GObject parent;
	gboolean dispose_has_run;
	gint make;
	gchar *make_ascii;
	gchar *model_ascii;
	gchar *time_ascii;
	GTime timestamp;
	gushort orientation;
	gfloat aperture;
	gushort iso;
	gfloat shutterspeed;
	guint thumbnail_start;
	guint thumbnail_length;
	guint preview_start;
	guint preview_length;
	guint16 preview_planar_config;
	guint preview_width;
	guint preview_height;
	guint16 preview_bits [3];
	gdouble cam_mul[4];
	gdouble contrast;
	gdouble saturation;
	gdouble color_tone;
	gshort focallength;
	RS_MATRIX4 adobe_coeff;
	GdkPixbuf *thumbnail;
};

typedef struct {
  GObjectClass parent_class;
} RSMetadataClass;

GType rs_metadata_get_type (void);

extern RSMetadata *rs_metadata_new (void);
extern RSMetadata *rs_metadata_new_from_file(const gchar *filename);
extern gboolean rs_metadata_load_from_file(RSMetadata *metadata, const gchar *filename);
extern void rs_metadata_normalize_wb(RSMetadata *metadata);
extern gchar *rs_metadata_get_short_description(RSMetadata *metadata);
extern GdkPixbuf *rs_metadata_get_thumbnail(RSMetadata *metadata);

/**
 * Deletes the on-disk cache (if any) for a photo
 * @param filename The filename of the PHOTO - not the cache itself
 */
extern void rs_metadata_delete_cache(const gchar *filename);

G_END_DECLS

#endif /* RS_METADATA_H */

