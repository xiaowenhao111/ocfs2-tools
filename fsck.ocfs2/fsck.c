/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * fsck.c
 *
 * file system checker for OCFS2
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Authors: Zach Brown
 */
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ocfs2.h"

#include "fsck.h"
#include "pass1.h"

static void print_usage(void)
{
	fprintf(stderr,
		"Usage: fsck.ocfs2 [-s <superblock>] [-B <blksize>]\n"
	       	"               <filename>\n");
}

static uint64_t read_number(const char *num)
{
	uint64_t val;
	char *ptr;

	val = strtoull(num, &ptr, 0);
	if (!ptr || *ptr)
		return 0;

	return val;
}

extern int opterr, optind;
extern char *optarg;

static errcode_t o2fsck_state_init(ocfs2_filesys *fs, char *whoami, 
				    o2fsck_state *ost)
{
	errcode_t ret;

	memset(ost, 0, sizeof(*ost));

	ret = ocfs2_block_bitmap_new(fs, "inodes in use", 
				     &ost->ost_used_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating used inodes bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "inodes with bad fields", 
				     &ost->ost_bad_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating bad inodes bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "directory inodes", 
				     &ost->ost_dir_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating dir inodes bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "regular file inodes", 
				     &ost->ost_reg_inodes);
	if (ret) {
		com_err(whoami, ret, "while allocating reg inodes bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "blocks off inodes",
				     &ost->ost_found_blocks);
	if (ret) {
		com_err(whoami, ret, "while allocating found blocks bitmap");
		return ret;
	}

	ret = ocfs2_block_bitmap_new(fs, "duplicate blocks",
				     &ost->ost_dup_blocks);
	if (ret) {
		com_err(whoami, ret, "while allocating duplicate block bitmap");
		return ret;
	}

	return 0;
}

int main(int argc, char **argv)
{
	char *filename;
	ocfs2_filesys *fs;
	int64_t blkno, blksize;
	o2fsck_state ost;
	int c, ret;


	/* These mean "autodetect" */
	blksize = 0;
	blkno = 0;

	initialize_ocfs_error_table();

	while((c = getopt(argc, argv, "s:B:")) != EOF) {
		switch (c) {
			case 's':
				blkno = read_number(optarg);
				if (blkno < OCFS2_SUPER_BLOCK_BLKNO) {
					fprintf(stderr,
						"Invalid blkno: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			case 'B':
				blksize = read_number(optarg);
				if (blksize < OCFS2_MIN_BLOCKSIZE) {
					fprintf(stderr, 
						"Invalid blksize: %s\n",
						optarg);
					print_usage();
					return 1;
				}
				break;

			default:
				print_usage();
				return 1;
				break;
		}
	}

	if (blksize % OCFS2_MIN_BLOCKSIZE) {
		fprintf(stderr, "Invalid blocksize: %lld\n", blksize);
		print_usage();
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing filename\n");
		print_usage();
		return 1;
	}

	filename = argv[optind];

	/* XXX we'll decide on a policy for using o_direct in the future.
	 * for now we want to test against loopback files in ext3, say. */
	ret = ocfs2_open(filename, OCFS2_FLAG_RO | OCFS2_FLAG_BUFFERED, blkno,
			 blksize, &fs);
	if (ret) {
		com_err(argv[0], ret,
			"while opening file \"%s\"", filename);
		goto out;
	}

	if (o2fsck_state_init(fs, argv[0], &ost)) {
		fprintf(stderr, "error allocating run-time state, exiting..\n");
		return 1;
	}

	fprintf(stdout, "OCFS2 filesystem on \"%s\":\n", filename);
	fprintf(stdout,
		"\tblocksize = %d\n"
 		"\tclustersize = %d\n"
		"\tclusters = %u\n"
		"\tblocks = %llu\n",
 		fs->fs_blocksize,
		fs->fs_clustersize,
		fs->fs_clusters,
		fs->fs_blocks);

	ret = o2fsck_pass1(fs, &ost);
	if (ret)
		com_err(argv[0], ret, "pass1 failed");

	ret = ocfs2_close(fs);
	if (ret) {
		com_err(argv[0], ret,
			"while closing file \"%s\"", filename);
	}

out:
	return 0;
}
