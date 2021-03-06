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

#include <glib-2.0/glib.h>
#include "config.h"
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>
#include "rs-utils.h"

gchar *
rs_profile_camera_find(const gchar *make, const gchar *model)
{
	static gchar *last_make = NULL;
	static gchar *last_model = NULL;
	static gchar *last_id = NULL;

	if (NULL == make ||  NULL == model)
		return NULL;

	if (last_make && last_model)
	{
		if (g_str_equal(make, last_make) && g_str_equal(model, last_model))
			return last_id ? g_strdup(last_id) : NULL;

		g_free(last_make);
		g_free(last_model);
		if (last_id)
			g_free(last_id);

		last_make = g_strdup(make);
		last_model = g_strdup(model);
		last_id = NULL;
	}
	static gchar *filename = NULL;
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlNodePtr camera = NULL;
	xmlNodePtr exif = NULL;
	xmlChar *xml_unique_id, *xml_make, *xml_model;

	if (!filename)
		filename = g_build_filename(rs_confdir_get(), G_DIR_SEPARATOR_S, "profiles" G_DIR_SEPARATOR_S "rawstudio-cameras.xml", NULL);

	if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR))
	{
		g_free(filename);
		filename = NULL;
	}

	if (!filename)
		filename = g_build_filename(PACKAGE_DATA_DIR, PACKAGE, "profiles" G_DIR_SEPARATOR_S "rawstudio-cameras.xml", NULL);

	if (!g_file_test(filename, G_FILE_TEST_IS_REGULAR))
		return NULL;

	doc = xmlParseFile(filename);
	if (!doc)
		return NULL;

	cur = xmlDocGetRootElement(doc);

	camera = cur->xmlChildrenNode;
	while(camera)
	{
		if (!xmlStrcmp(camera->name, BAD_CAST "camera"))
		{
			xml_unique_id = xmlGetProp(camera, BAD_CAST "unique_id");

			exif = camera->xmlChildrenNode;
			while(exif)
			{
				if (!xmlStrcmp(exif->name, BAD_CAST "exif"))
				{
					xml_make = xmlGetProp(exif, BAD_CAST "make");
					if (g_strcmp0((gchar *) xml_make, make) == 0)
					{
						xml_model = xmlGetProp(exif, BAD_CAST "model");
						if (g_strcmp0((gchar *) xml_model, model) == 0)
						{
							xmlFree(xml_make);
							xmlFree(xml_model);
							gchar *unique_id = g_strdup((gchar *) xml_unique_id);
							xmlFree(xml_unique_id);
							xmlFree(doc);
							last_id = g_strdup(unique_id);
							return  unique_id;
						}
						xmlFree(xml_model);
					}
					xmlFree(xml_make);
				}
				exif = exif->next;

			}
			xmlFree(xml_unique_id);
		}
		camera = camera->next;
	}
	xmlFree(doc);
	g_warning("Could not find unique camera: Make:'%s'. Model:'%s'", make, model);
	return NULL;
}
