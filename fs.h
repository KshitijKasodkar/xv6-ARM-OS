// On-disk file system format.
// Both the kernel and user programs use this header file.

// Block 0 is unused.
// Block 1 is super block.
// Blocks 2 through sb.ninodes/IPB hold inodes.
// Then free bitmap blocks holding sb.size bits.
// Then sb.nblocks data blocks.
// Then sb.nlog log blocks.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// File system super block
struct superblock {
    uint    size;           // Size of file system image (blocks)
    uint    nblocks;        // Number of data blocks
    uint    ninodes;        // Number of inodes.
    uint    nlog;           // Number of log blocks
};

#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
// MAXFILE: direct + single-indirect + double-indirect
#define MAXFILE (NDIRECT + NINDIRECT + (NINDIRECT * NINDIRECT))

// On-disk inode structure
struct dinode {
    short   type;           // File type
    short   major;          // Major device number (T_DEV only)
    short   minor;          // Minor device number (T_DEV only)
    short   nlink;          // Number of links to inode in file system
    uint    size;           // Size of file (bytes)
    // Keep on-disk inode size unchanged: previous layout had
    // NDIRECT=12 and addrs[NDIRECT+1] (13 entries). To implement
    // double-indirect without changing the on-disk size, use
    // NDIRECT+2 entries so that when NDIRECT==11 we still have 13
    // addrs: 11 direct, 1 single-indirect, 1 double-indirect.
    uint    addrs[NDIRECT+2]; // Data block addresses
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i)     ((i) / IPB + 2)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block containing bit for block b
#define BBLOCK(b, ninodes) (b/BPB + (ninodes)/IPB + 3)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
    ushort  inum;
    char    name[DIRSIZ];
};

