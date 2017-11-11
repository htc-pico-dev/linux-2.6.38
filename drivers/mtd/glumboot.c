/*
 * author: thewisenerd <thewisenerd@protonmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

#include <linux/crc32.h>

/* error message prefix */
#define ERRP "glumboot: "

/* debug macro */
#if 0
#define dbg(...) do { printk( "DEBUG-GLUMBOOT: " __VA_ARGS__); } while(0)
#else
#define dbg(...)
#endif

#define GLUMBOOT_MAX_NR_PARTS 10

struct glumboot_partition_type {
	char name[20];
	u32 offset;
	u32 size;
	u32 flags;
};

struct glumboot_partition_table {
	char magic[8];				/* "glumboot" */
	u8 reserved__1[4];			/*  reserved */
	u32 partition_count;
	u32 crc32;
	u8 reserved__2[12];			/*  reserved */
	struct glumboot_partition_type partitions[0];
};

static int offset = CONFIG_MTD_GLUMBOOT_OFFSET;
static const char magic[] = "glumboot";

static struct mtd_partition glumboot_partitions[GLUMBOOT_MAX_NR_PARTS];
static char glumboot_names[GLUMBOOT_MAX_NR_PARTS * 20];

static int parse_glumboot_partitions(struct mtd_info *master,
				     struct mtd_partition **pparts,
				     unsigned long origin)
{
	int ret = 0, i, n;
	size_t retlen;
	char *readbuf;
	uint32_t search_addr;
	uint32_t search_addr_limit;
	struct glumboot_partition_table *table;
	struct mtd_partition *ptn;
	char *name = glumboot_names;
	unsigned long alloc;
	u32 checksum;

	dbg( "%s\n", __func__);

	if ( offset < 0 ) {
		search_addr = master->size - (uint32_t)(-offset * master->erasesize);
		search_addr_limit = search_addr - (CONFIG_MTD_GLUMBOOT_SEARCH_DEPTH * master->erasesize);
		while ( master->block_isbad &&
		       master->block_isbad(master, search_addr)) {
			if (!search_addr || search_addr == search_addr_limit) {
			nogood:
				printk(KERN_NOTICE "Failed to find a non-bad block to check for GlumBoot partition table\n");
				return -EIO;
			}
			search_addr -= master->erasesize;
		}
	} else {
		search_addr = (uint32_t)(offset * master->erasesize);
		search_addr_limit = CONFIG_MTD_GLUMBOOT_SEARCH_DEPTH * master->erasesize;
		while ( master->block_isbad &&
		       master->block_isbad(master, offset)) {
			search_addr += master->erasesize;
			if (search_addr == master->size || search_addr == search_addr_limit)
				goto nogood;
		}
	}

	alloc = sizeof(struct glumboot_partition_table) + (GLUMBOOT_MAX_NR_PARTS * sizeof(struct glumboot_partition_type));
	alloc = ((alloc / master->writesize) + 1) * master->writesize;
	dbg("allocating %lu bytes for cache read\n", alloc);

	readbuf = vmalloc( alloc );
	if (!readbuf) {
		dbg( "vmalloc failed\n" );
		return -ENOMEM;
	}

	dbg("searchaddr: 0x%08x; search_addr_limit: 0x%08x\n", search_addr, search_addr_limit);
	dbg("magic: %s, len: %d\n", magic, strlen(magic));

	while (search_addr && search_addr != master->size && search_addr != search_addr_limit) {
		ret = master->read(master, search_addr, alloc, &retlen, (void *)readbuf);
		if (ret || retlen < 8) {
			dbg("error reading addr: 0x%08x\n", search_addr);
			goto search_next_block;
		}

		if (!memcmp(readbuf, magic, strlen(magic))) {
			dbg("found match at: %08x\n", search_addr);
			for(i = 0; i < 8; i++) {
				dbg("    buf[%03x] = %x\n", i, readbuf[i]);
			}
			break;
		}

	search_next_block:
		if (offset < 0)
			search_addr -= master->erasesize;
		else
			search_addr += master->erasesize;
	}

	if (!search_addr || search_addr == master->size || search_addr == search_addr_limit) {
		printk(KERN_NOTICE "Failed to find GlumBoot partition table\n");
		ret = -ENOENT;
		goto out;
	}

	table = (struct glumboot_partition_table *) ( (void *)(readbuf) );
	dbg("partition count: 0x%08x\n", table->partition_count);

	if (table->partition_count > GLUMBOOT_MAX_NR_PARTS) {
		printk(KERN_NOTICE "more partitions than supported. not using GlumBoot.\n");
		ret = -EFAULT;
		goto out;
	}

	checksum = crc32(0xFFFFFFFF, table->partitions, (table->partition_count * sizeof(struct glumboot_partition_type))) ^ 0xFFFFFFFF;
	dbg("crc: calc: %08x\n", checksum);
	dbg("crc: read: %08x\n", table->crc32);

	if (checksum != table->crc32) {
		printk(KERN_NOTICE "checksum failed. not using GlumBoot.\n");
		ret = -EFAULT;
		goto out;
	}

	n = 0;
	ptn = glumboot_partitions;
	for (n = 0; n < table->partition_count; n++) {
		memcpy(name, table->partitions[n].name, 19);
		name[20] = 0;

		ptn->name = name;
		ptn->offset = table->partitions[n].offset;
		ptn->size = table->partitions[n].size;

		dbg("0x%012llx-0x%012llx : \"%s\"\n",
			ptn->offset, ptn->offset + ptn->size, ptn->name);

		name += 16;
		ptn++;
	}

	*pparts = glumboot_partitions;
	ret = table->partition_count;

out:
	vfree(readbuf);
	return ret;
}

static struct mtd_part_parser glumboot_parser = {
	.owner = THIS_MODULE,
	.parse_fn = parse_glumboot_partitions,
	.name = "GlumBoot",
};

static int __init glumboot_parser_init(void)
{
	return register_mtd_parser(&glumboot_parser);
}

module_init(glumboot_parser_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("thewisenerd <thewisenerd@protonmail.com>");
MODULE_DESCRIPTION("Parsing code for GlumBoot partition table");
