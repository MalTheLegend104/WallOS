/**
 * @file wallos_fat_config.h
 * @author Malcolm
 * @brief
 * @version
 * @date 6/21/2026
 */
#ifndef WALLOS_FAT_CONFIG_H
#define WALLOS_FAT_CONFIG_H

#ifndef FAT_TREE_MAX_DEPTH
	// This caps recursion depth for fat32_tree()
	// This prevents a potential corrupt volume from causing a problem with cycle entires
#define FAT_TREE_MAX_DEPTH 64
#endif // FAT_TREE_MAX_DEPTH

#ifndef FAT1216_TREE_MAX_DEPTH
#define FAT1216_TREE_MAX_DEPTH 64
#endif // FAT1216_TREE_MAX_DEPTH

	/** Maximum number of LFN entries in a single chain (covers names up to 255 chars + null). */
#define FAT_LFN_MAX_ENTRIES 20

#endif // WALLOS_FAT_CONFIG_H
