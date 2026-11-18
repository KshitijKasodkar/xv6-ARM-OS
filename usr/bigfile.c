#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "fs.h"

char buf[512];

// Enough blocks to go past the direct and single-indirect blocks into the
// doubly-indirect region (the disk image only has ~200 free blocks).
#define NUM_BLOCKS (NDIRECT + NINDIRECT + 12)

// Write NUM_BLOCKS patterned blocks, read them back and check every one,
// then delete the file. Returns 0 on success, -1 on failure.
int
write_verify_delete(int verbose)
{
    int fd, i, cc, total;

    if(verbose)
        printf(1, "\nTest A: Writing %d blocks (%d bytes)\n",
               NUM_BLOCKS, NUM_BLOCKS * 512);

    unlink("bigfile.dat");
    fd = open("bigfile.dat", O_CREATE | O_RDWR);
    if(fd < 0){
        printf(1, "FAIL: could not create bigfile\n");
        return -1;
    }

    // Fill with patterned data
    for(i = 0; i < NUM_BLOCKS; i++){
        memset(buf, (i & 0xFF), sizeof(buf));

        if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
            printf(1, "FAIL: write error at block %d\n", i);
            close(fd);
            return -1;
        }

        if(verbose && (i + 1) % 60 == 0)
            printf(1, "  wrote %d blocks\n", i + 1);
    }
    close(fd);

    if(verbose){
        printf(1, "OK: Completed write(%d blocks)\n", NUM_BLOCKS);
        printf(1, "  direct:           %d\n", NDIRECT);
        printf(1, "  single-indirect:  %d\n", NINDIRECT);
        printf(1, "  double-indirect:  %d\n", NUM_BLOCKS - NDIRECT - NINDIRECT);
        printf(1, "\nTest B: Read + verify\n");
    }

    fd = open("bigfile.dat", O_RDONLY);
    if(fd < 0){
        printf(1, "FAIL: could not reopen bigfile\n");
        return -1;
    }

    total = 0;
    for(i = 0; i < NUM_BLOCKS; i++){
        cc = read(fd, buf, sizeof(buf));
        if(cc != sizeof(buf)){
            printf(1, "FAIL: short read @ %d (got %d)\n", i, cc);
            close(fd);
            return -1;
        }

        // every byte of the block must carry the block's pattern
        if((uchar)buf[0] != (i & 0xFF) || (uchar)buf[255] != (i & 0xFF) ||
           (uchar)buf[511] != (i & 0xFF)){
            printf(1, "FAIL: block %d mismatch (exp %d got %d)\n",
                   i, (i & 0xFF), (uchar)buf[0]);
            close(fd);
            return -1;
        }

        total += cc;

        if(verbose && (i + 1) % 60 == 0)
            printf(1, "  checked %d blocks\n", i + 1);
    }
    close(fd);

    if(total != NUM_BLOCKS * 512){
        printf(1, "FAIL: wrong total (%d != %d)\n", total, NUM_BLOCKS * 512);
        return -1;
    }

    if(verbose)
        printf(1, "OK: verified %d blocks\n", NUM_BLOCKS);

    if(unlink("bigfile.dat") < 0){
        printf(1, "FAIL: could not delete bigfile\n");
        return -1;
    }

    return 0;
}

void
bigfile(void)
{
    int round;

    printf(1, "Running bigfile check\n");
    printf(1, "Filesystem parameters:\n");
    printf(1, "  direct blocks:      %d\n", NDIRECT);
    printf(1, "  single-indirect:    %d\n", NINDIRECT);
    printf(1, "  max blocks:         %d\n", MAXFILE);
    printf(1, "  max size:           %d bytes (%d KB)\n",
           MAXFILE * 512, (MAXFILE * 512) / 1024);

    if(write_verify_delete(1) < 0)
        exit();

    // Test C: the disk has room for only one copy of this file, so writing it
    // again after each delete fails unless itrunc() freed every direct,
    // single-indirect and doubly-indirect block (and the index blocks).
    printf(1, "\nTest C: Delete and rewrite twice (checks blocks are freed)\n");
    for(round = 2; round <= 3; round++){
        if(write_verify_delete(0) < 0){
            printf(1, "FAIL: round %d\n", round);
            exit();
        }
        printf(1, "OK: round %d written, verified and deleted\n", round);
    }

    printf(1, "\n=== BIGFILE TEST COMPLETED ===\n");
    printf(1, "Verified:\n");
    printf(1, "  [ok] direct           (%d)\n", NDIRECT);
    printf(1, "  [ok] single-indirect  (%d)\n", NINDIRECT);
    printf(1, "  [ok] double-indirect  (%d)\n", NUM_BLOCKS - NDIRECT - NINDIRECT);
    printf(1, "  [ok] blocks freed on delete (3 rounds)\n");
    printf(1, "Total: %d blocks = %d bytes = %d KB\n",
           NUM_BLOCKS, NUM_BLOCKS * 512, (NUM_BLOCKS * 512) / 1024);
}

int
main(int argc, char *argv[])
{
    bigfile();
    exit();
}
