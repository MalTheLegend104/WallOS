#ifndef WALLOS_VFS_FAT32_H
#define WALLOS_VFS_FAT32_H

#include <filesystem/vfs.h>
#include <filesystem/fat/fat32.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef VFS_FAT32_OPEN_MAX
/** Maximum simultaneously open files *and* directories (each). */
#  define VFS_FAT32_OPEN_MAX 32
#endif
    /* Per file state */
    typedef struct {
        bool used;
        VFS_OpenFlags flags;
        fat_resolved_dirent_t entry;  //<  Resolved dirent at open time.
        uint32_t parent_cluster;      //<  Starting cluster of the parent dir.
        uint8_t* buf;                 //<  In-memory file contents (malloc'd).
        uint32_t size;                //<  Current logical file size in bytes.
        uint32_t pos;                 //<  Read/write cursor.
    } vfs_fat32_file_t;

    /* Per-open-directory state */
    typedef struct {
        bool used;
        fat_dirent_list_t listing; //< Snapshot of the directory taken at opendir. 
        size_t index; //< Next entry to return from read_dir.          
    } vfs_fat32_dir_t;

     /* Driver context (passed as fs_ctx to VFS_Mount) */
    typedef struct {
        WDM_DriveHandle drive;
        fat32_ebr_t ebr; //< Parsed boot sector / EBR.     
        fat32_fsinfo_t fsinfo; //< Parsed FSInfo sector.          
        bool fsinfo_valid; //< True when fsinfo signatures OK. 

        vfs_fat32_file_t files[VFS_FAT32_OPEN_MAX];
        vfs_fat32_dir_t dirs[VFS_FAT32_OPEN_MAX];
    } vfs_fat32_ctx_t;

    /**
     * @brief Heap-allocate and zero-init a fresh driver context.
     * @return Pointer on success, NULL on OOM.
     */
    vfs_fat32_ctx_t* vfs_fat32_alloc(void);

    /**
     * @brief Free a driver context that is no longer mounted.
     */
    void vfs_fat32_free(vfs_fat32_ctx_t* ctx);

    // meant to be bound as the v_ops when binding the drive to the VFS
    extern const VFS_FSOps vfs_fat32_ops;

#ifdef __cplusplus
}
#endif
#endif /* WALLOS_VFS_FAT32_H */