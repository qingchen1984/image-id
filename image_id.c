/*
 * Image ID - Calculate MusicBrainz disc TOC numbers from CD Images
 * Copyright (c) 2014 Calvin Walton
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of either version 2, or any later version of the GNU General
 * Public License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "config.h"

#ifdef LIBMIRAGE_AT_LEAST_3_0
#include <mirage/mirage.h>
#else /* libmirage 2.1 */
#include <mirage.h>
#endif

#include <discid/discid.h>

#include <stdbool.h>
#include <stdlib.h>

#define COPYRIGHT "Copyright (c) 2014 Calvin Walton, Released under GPLv2"

#define DEBUG 0

/* Print the version information for the program */
static void print_version();
/* Print the usage information for the program */
static void print_usage(char *progname);
/* Process information for a disc */
static bool process_disc(MirageDisc *disc, DiscId *discid);

int main(int argc, char *argv[]) {
	GError *error = NULL;

	print_version();
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	/* Initialize libraries */
	mirage_initialize(&error);
	if (error) {
		g_critical("Failed to initialize libmirage: %s\n",
		           error->message);
		exit(1);
	}
	MirageContext *context = g_object_new(MIRAGE_TYPE_CONTEXT, NULL);
	DiscId *discid = discid_new();

	int file_count = argc - 1;
	gchar **file_list = malloc((file_count + 1) * sizeof (gchar *));
	for (int i = 0; i < file_count; i++) {
		file_list[i] = argv[i+1];
	}
	file_list[file_count] = NULL;

	MirageDisc *disc = mirage_context_load_image(context, file_list, &error);
	if (error) {
		g_critical("Cannot open image: %s\n", error->message);
		exit(1);
	}

	process_disc(disc, discid);

	return 0;
}

static void print_version() {
	fprintf(stderr, "%s\n%s\n", PACKAGE_STRING, COPYRIGHT);
	fprintf(stderr, "Using libmirage %s (compiled with %s)\n",
			mirage_version_long, MIRAGE_VERSION_LONG);
}

static void print_usage(char *progname) {
	fprintf(stderr,	"Usage:\n"
			"\t%s <cd-image> ...\n",
			progname);
}

static bool process_disc(MirageDisc *disc, DiscId *discid) {
	int sessions;
	GError *error = NULL;

	int first = 1;
	int last = 0;
	int offsets[100] = {0};
	char isrcs[100][MIRAGE_ISRC_SIZE+1] = {{0}};
	char mcn[MIRAGE_MCN_SIZE+1] = {0};

#ifndef LIBMIRAGE_AT_LEAST_3_0
	if (mirage_disc_get_mcn(disc) != NULL) {
		strncpy(mcn, mirage_disc_get_mcn(disc), MIRAGE_MCN_SIZE);
	}
#endif

	sessions = mirage_disc_get_number_of_sessions(disc);
	fprintf(stderr, "Disc contains %d sessions\n", sessions);

	for (int i = 0; i < sessions; i++) {
		MirageSession *session;

		int leadout_length, tracks, type;
		int session_number, first_track, start_sector, length;
		int offset;

		session = mirage_disc_get_session_by_index(disc, i, &error);
		if (error) {
			fprintf(stderr, "Cannot get session %d: %s\n", i,
					error->message);
			continue;
		}

		session_number = mirage_session_layout_get_session_number(session);
		first_track = mirage_session_layout_get_first_track(session);
		start_sector = mirage_session_layout_get_start_sector(session);
		length = mirage_session_layout_get_length(session);

		if (DEBUG) {
			fprintf(stderr, "session %d: layout: number %d, first track %d, start sector %d, length %d\n",
				i, session_number, first_track, start_sector,
				length);
		}

		type = mirage_session_get_session_type(session);
		leadout_length = mirage_session_get_leadout_length(session);
		tracks = mirage_session_get_number_of_tracks(session);

		if (DEBUG) {
			fprintf(stderr, "session %d: %d tracks, type %d, leadout length %d\n",
				i, tracks, type, leadout_length);
		}

#ifdef LIBMIRAGE_AT_LEAST_3_0
		const gchar *session_mcn = mirage_session_get_mcn(session);
		if (session_mcn != NULL) {
			strncpy(mcn, session_mcn, sizeof mcn);
		}
#endif

		offset = 0 - start_sector;
		if (DEBUG) {
			fprintf(stderr, "session %d: calculated offset %d\n",
				i, offset);
		}

		offsets[0] = length;
		first = last = first_track;

		for (int j = 0; j < tracks; j++) {
			MirageTrack *track;

			int track_start, indices;
			int track_num, track_start_sector, track_length;

			track = mirage_session_get_track_by_index(session, j, &error);
			if (error) {
				fprintf(stderr, "Cannot get track %d: %s\n", j, error->message);
				continue;
			}

			track_num = mirage_track_layout_get_track_number(track);
			track_start_sector = mirage_track_layout_get_start_sector(track);
			track_length = mirage_track_layout_get_length(track);

			if (DEBUG) {
				fprintf(stderr, "session %d: track %d: layout: number %d, start sector %d, length %d\n",
					i, j, track_num,
					track_start_sector + offset,
					track_length);
			}

			track_start = mirage_track_get_track_start(track);
			indices = mirage_track_get_number_of_indices(track);

			if (DEBUG) {
				fprintf(stderr, "session %d: track %d: track start %d, %d indicies\n",
					i, j,
					track_start + track_start_sector
						    + offset,
					indices);
			}

			offsets[track_num] = track_start + track_start_sector + offset;
			if (mirage_track_get_isrc(track) != NULL) {
				strncpy(isrcs[track_num],
					mirage_track_get_isrc(track),
					MIRAGE_ISRC_SIZE);
			}

			if (track_num > last)
				last = track_num;

			g_object_unref(track);
		}

		g_object_unref(session);
	}

	fprintf(stderr, "Full TOC: %d %d %d", first, last, offsets[0]);
	for (int i = first; i <= last; i++) {
		fprintf(stderr, " %d", offsets[i]);
	}
	fprintf(stderr, "\n");

	discid_put(discid, first, last, offsets);
	printf("MusicBrainz Disc ID: %s\n", discid_get_id(discid));

	printf("FreeDB Disc ID: %s\n", discid_get_freedb_id(discid));
	printf("MusicBrainz Submission URL: %s\n", discid_get_submission_url(discid));
	for (int i = first; i <= last; i++) {
		if (strlen(isrcs[i]) > 0) {
			printf("ISRC Track %d: %s\n", i, isrcs[i]);
		}
	}
	if (strlen(mcn) > 0) {
		printf("MCN: %s\n", mcn);
	}

	return true;
}
