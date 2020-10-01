/*
 *  Copyright (C) 2020 CS416 Rutgers CS
 *	Tiny File System
 *	File:	tfs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	unsigned char buff[BLOCK_SIZE];
	bitmap_t bitmap = buff;
	int r_stat = 0;
	
	r_stat = bio_read(1, (void *) bitmap);	
	if(r_stat<0)
	{
		perror("block reading failed");
		return -1;
	}
	// Step 2: Traverse inode bitmap to find an available slot
	int i = 0;
	int found = 0;
	for(; i<MAX_INUM; i++)
	{
		if (get_bitmap(bitmap, i) == 0)
		{
			found = 1;
			break;
		}
	}
	if (found == 0)
	{
		errno =  ENOMEM;
		return -ENOMEM;
	}
	// Step 3: Update inode bitmap and write to disk 
	set_bitmap(bitmap, i);
	int w_stat = bio_write(1, (void *) bitmap);
	if(w_stat<0)
	{
		perror("block writing failed");
		return -1;
	}
	return i;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {								
	// Step 1: Read data block bitmap from disk
	unsigned char buff[BLOCK_SIZE];
	bitmap_t bitmap = buff;
	int r_stat = 0;
	r_stat = bio_read(2, (void *) bitmap);
	if(r_stat<0)
	{
		perror("block reading failed");
		return -1;
	}
	// Step 2: Traverse data block bitmap to find an available slot
	int i = 0;
	int found = 0;
	for(; i<MAX_DNUM; i++)
	{
		if (get_bitmap(bitmap, i) == 0)
		{
			found = 1;
			break;
		}
	}
	if (found == 0)
	{
		errno =  ENOMEM;
		return -ENOMEM;
	}
	// Step 3: Update data block bitmap and write to disk 
	set_bitmap(bitmap, i);
	int w_stat = bio_write(2, (void *) bitmap);
	if(w_stat<0)
	{
		perror("block writing failed");
		return -1;
	}
	return i;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
	uint32_t inode_num = (uint32_t) ino;

  	// Step 1: Get the inode's on-disk block number
	unsigned char buff[BLOCK_SIZE];
	int r_stat = 0;
	r_stat = bio_read(0, (void *) buff);
	if(r_stat<0)
	{
		perror("block reading failed");
		return -1;
	}
	uint32_t inode_start_block = ((struct superblock *) buff)->i_start_blk;	
	uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
	uint32_t inode_block_num = inode_start_block + inode_num / inodes_per_block;
	// Step 2: Get offset of the inode in the inode on-disk block
	uint32_t offset = inode_num % inodes_per_block;
	// Step 3: Read the block from disk and then copy into inode structure
	r_stat = bio_read(inode_block_num, (void *) buff);
	if(r_stat<0)
	{
		perror("block reading failed");
		return -1;
	}
	memcpy(inode, ((struct inode *) buff) + offset , sizeof(struct inode));
	return 0;
}

int writei(uint16_t ino, struct inode *inode) {
	uint32_t inode_num = (uint32_t) ino;

	// Step 1: Get the block number where this inode resides on disk
	unsigned char buff[BLOCK_SIZE];
	int r_stat = 0;
	r_stat = bio_read(0, (void *) buff);
	if(r_stat<0)
	{
		perror("block reading failed");
		return -1;
	}
	uint32_t inode_start_block = ((struct superblock *) buff)->i_start_blk;
	uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
	uint32_t inode_block_num = inode_start_block + inode_num / inodes_per_block;
	// Step 2: Get the offset in the block where this inode resides on disk
	uint32_t offset = inode_num % inodes_per_block;
	// Step 3: Write inode to disk 
	r_stat = bio_read(inode_block_num, (void *) buff);
	if(r_stat<0)
	{
		perror("block reading failed");
		return -1;
	}
	memcpy(((struct inode *) buff) + offset, inode, sizeof(struct inode));
	int w_stat = 0;
	w_stat = bio_write(inode_block_num, (void *) buff);
	if(w_stat<0)
	{
		perror("block writng failed");
		return -1;
	}
	return 0;
}

int readdb(int block_num, void * buf){	
	unsigned char super_buff[BLOCK_SIZE];
	int r_stat = bio_read(0, super_buff);
	if(r_stat<0)
	{
		perror("super block reading failed");
		return -1;
	}
	int start_block = ((struct superblock *) super_buff)->d_start_blk;
	r_stat = bio_read(block_num + start_block, buf);
	if(r_stat<0)
	{
		perror("data block reading failed");
		return -1;
	}
	return 0;
}

int writedb(int block_num, void * buf){	
	unsigned char super_buff[BLOCK_SIZE];
	int r_stat = bio_read(0, super_buff);
	if(r_stat<0)
	{
		perror("super block reading failed");
		return -1;
	}
	int start_block = ((struct superblock *) super_buff)->d_start_blk;
	int w_stat = bio_write(block_num + start_block, buf);
	if(w_stat<0)
	{
		perror("data block writing failed");
		return -1;
	}
	return 0;
}
/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
	printf("%s is being called on %s\n", __FUNCTION__, fname);

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	// setting up inode buffer
	struct inode inode_buff;
	int ino_r_stat = readi(ino, &inode_buff);
	if(ino_r_stat == -1)
	{
		perror("error reading inode");
		return -1;
	}
	if(inode_buff.valid == 0)
	{
		errno =  EOWNERDEAD;
		return -1;
	}
	if(!S_ISDIR(inode_buff.type))
	{
		errno =  ENOTDIR;
		return -1;
	}
	
	// Step 2: Get data block of current directory from inode
	
	// Step 3: Read directory's data block and check each directory entry.
	//If the name matches, then copy directory entry to dirent structure
	uint32_t data_size = inode_buff.size;
	int i = 0;
	for(; i < data_size; i++)
	{
		int block_num = inode_buff.direct_ptr[i];
		unsigned char buff[BLOCK_SIZE];
		int r_stat = readdb(block_num, (void *) buff);
		if(r_stat < 0)
		{
			perror("block reading failed");
			return -1;
		}
		int dirent_per_block = BLOCK_SIZE / sizeof(struct dirent);
		int j = 0;
		struct dirent * dentries = (struct dirent *) buff; 
		for(; j < dirent_per_block; j++)
		{
			if (dentries[j].valid == 1)
			{		
				if (dentries[j].len == name_len)
				{
					if(strncmp(dentries[j].name, fname, name_len) == 0)
					{
						memcpy(dirent, dentries + j, sizeof(struct dirent));
						return 0;
					}
				}
			}
		}
	}
	errno = ENOENT;
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	printf("%s is being called\n", __FUNCTION__);

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries
	struct dirent dentry;
	int d_find_stat = dir_find(dir_inode.ino, fname, name_len, &dentry);
	if (d_find_stat == 0)
	{
		errno = EEXIST;
		return -1;
	}
	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	uint32_t data_size = dir_inode.size;
	int i = 0;
	for(; i < data_size; i++)
	{
		int block_num = dir_inode.direct_ptr[i];
		unsigned char buff[BLOCK_SIZE];
		int r_stat = readdb(block_num, (void *) buff);
		if(r_stat<0)
		{
			perror("data block reading failed");
			return -1;
		}
		int dirent_per_block = BLOCK_SIZE / sizeof(struct dirent);
		int j = 0;
		struct dirent * dentries = (struct dirent *) buff; 
		for(; j < dirent_per_block; j++)
		{
			if (dentries[j].valid == 0)
			{
				dentries[j].ino = f_ino;
				dentries[j].valid = 1;
				strcpy(dentries[j].name, fname);
				dentries[j].len = name_len;
				int db_w_stat = writedb(block_num, (void *) buff);
				if(db_w_stat<0)
				{
					perror("data block writing failed");
					return -1;
				}
				dir_inode.link++;
				int w_ino_stat = writei(dir_inode.ino, &dir_inode);
				return 0;
			}
		}
	}
	if (dir_inode.size >= 16)
	{
		errno = ENOSPC;
		return -1;
	}
	int new_block_num = get_avail_blkno();
	if (new_block_num == -1)
	{
		perror("error finding a data block:");
		return -1;
	}
	unsigned char buff[BLOCK_SIZE];
	int r_stat = readdb(new_block_num, (void *) buff);
	if(r_stat<0)
	{
		perror("block reading failed");
		return -1;
	}
	memset((void*) buff, 0, BLOCK_SIZE);
	struct dirent * direntry = (struct dirent *) buff; 
	direntry[0].ino = f_ino;
	direntry[0].valid = 1;
	strncpy(direntry[0].name, fname, name_len);
	direntry[0].len = name_len;
	
	dir_inode.direct_ptr[dir_inode.size] = new_block_num;
	dir_inode.link++;
	dir_inode.size++;
	int w_ino_stat = writei(dir_inode.ino, &dir_inode);
	int w_db_stat = writedb(new_block_num, (void *) buff);
	if(w_ino_stat < 0)
	{
		perror("inode writing failed");
		return -1;
	}
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
	printf("%s is being called on \"%s\"\n", __FUNCTION__, fname);
	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk
	int i = 0;
	for(; i < dir_inode.size; i++){
		unsigned char buff[BLOCK_SIZE];
		int block_num = dir_inode.direct_ptr[i];
		int r_db_stat = readdb(block_num, buff);
		uint32_t dentry_per_block = BLOCK_SIZE / sizeof(struct dirent);
		struct dirent * dentry = (struct dirent *) buff;
		int j = 0;
		for(; j < dentry_per_block; j++){
			if(dentry[j].valid == 1){
				if (dentry[j].len == name_len){
					if(strncmp(dentry[j].name, fname, name_len) == 0){
						printf("\033[1;33m deleting dentry# %d, ino%d : %s \033[0m\n", j, dentry[j].ino, dentry[j].name);
						memset(dentry + j, 0, sizeof(struct dirent));
						int block_is_empty = 1;
						int k = 0;
						for(; k < dentry_per_block; k++){
							if(dentry[k].valid == 1){
								block_is_empty = 0;
								break;
							}
						}
						if(block_is_empty == 1){
							memset(buff, 0, BLOCK_SIZE);
							unsigned char buffer[BLOCK_SIZE];
							int r_stat = bio_read(2, buffer);
							bitmap_t data_bitmap = buffer;
							unset_bitmap(data_bitmap, block_num);
							int w_stat = bio_write(2, buffer);
							k = j;
							dir_inode.size--;
							for(; k < dir_inode.size; k++){
								dir_inode.direct_ptr[k] = dir_inode.direct_ptr[k + 1];
							}
							dir_inode.direct_ptr[dir_inode.size] = 0;
						}
						int w_db_stat = writedb(block_num, buff);

						dir_inode.link--;
						int w_node_stat = writei(dir_inode.ino, &dir_inode);
						return 0;
					}
				}
			}
		}
	}
	errno = ENOENT;
	return -1;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	printf("%s is being called\n", __FUNCTION__);
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	char str[PATH_MAX + 1];			// Make sure end of path is null character
	memcpy(str, path, strlen(path));
	str[strlen(path)] = '\0';

	// prepare a dirent buffer
	struct dirent dir_buff;

	/* get the first token */
	const char delim[2] = "/";
	
	char *fname;
	fname = strtok(str, delim);
	
	/* walk through other tokens */
	while(fname != NULL) {
		int dentry_found = dir_find(ino, fname, strlen(fname), &dir_buff);
		if(dentry_found == -1)
		{
			return -1;
		}
		ino = dir_buff.ino;
		fname = strtok(NULL, delim);
	}	
	int ino_r_stat = readi(ino, inode);
	if(ino_r_stat == -1)
	{
		return -1;
	}

	return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {
	printf("%s is being called\n", __FUNCTION__);
	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
	int retstat = 0; 
	// write superblock information
	unsigned char buff[BLOCK_SIZE];
	struct superblock S;
	S.magic_num = MAGIC_NUM; 
	S.max_inum = MAX_INUM; 
	S.max_dnum = MAX_DNUM;  
	
	// initialize inode bitmap
	memset(buff, 0, BLOCK_SIZE);
	retstat = bio_write(1, (void*) buff);
	if(retstat < 0)
	{
		perror("initializing inode bitmap failed");
		return -1;
	}
	// Set our data_bitmap all to zero 
	retstat = bio_write(2, (void*) buff);
	if(retstat < 0)
	{
		perror("initializing data bitmap failed");
		return -1;
	}
	S.i_bitmap_blk = 1; 
	S.d_bitmap_blk = 2;
	S.i_start_blk = 3;
	// find start block of data block region
	uint32_t inodes_per_block = BLOCK_SIZE / sizeof(struct inode);
	S.d_start_blk = 3 + MAX_INUM / inodes_per_block;
	if (MAX_INUM % inodes_per_block != 0)
		S.d_start_blk++;

	memcpy(buff, &S, sizeof(struct superblock));
	retstat = bio_write(0, (void*)buff); // Write our SUPERBLOCK to block 0. 
	if(retstat < 0)
	{
		perror("writing superblock to disk failed");
		return -1;
	}
	//set all inode blocks to invalid
	int i = S.i_start_blk;
	for(; i < S.d_start_blk; i++)
	{
		memset(buff, 0, BLOCK_SIZE);
		retstat = bio_write(i, (void*) buff);
		if(retstat < 0)
		{
			perror("initializing inode blocks failed in mkfs()");
			return -1;
		}
	}
	// update bitmap information for root directory
	int ino = get_avail_ino();
	if(ino < 0)
	{
		perror("could not allocate inode for root");
		return -1;
	}

	// update inode for root directory
	// Do we need to use readi? To put information into a inode struct 
	// Then do we need to use writei? To write the inode struct into the disk
	struct inode iNode;
	
	retstat = readi(ino, &iNode); // Stores the inode number 0 into the struct iNode 
	if(retstat == -1)
	{
		perror("error reading root inode");
		return -1;
	}
	iNode.ino = ino;
	iNode.valid = 1;
	iNode.size = 0;
	iNode.type = S_IFDIR;
	iNode.link = 2;
	iNode.vstat.st_nlink = 2;
	iNode.vstat.st_atime = time( NULL ); 
	iNode.vstat.st_mtime = time( NULL ); 
	retstat = writei(ino, &iNode); // Writes the inode number 0 (root) into the disk 
	int dir_add_stat = dir_add(iNode, iNode.ino, ".", 1);
	if(dir_add_stat == -1)
	{
		perror("error adding \".\" to root directory");
		return -1;
	}
	return 0;
}

/* 
 * FUSE file operations
 */
static void * tfs_init(struct fuse_conn_info *conn) {
	printf("%s is being called\n", __FUNCTION__);

	// Step 1a: If disk file is not found, call mkfs
	int file = open(diskfile_path, O_RDWR, S_IRUSR | S_IWUSR);
	if (file < 0)	{   // Not found
		/* The file descriptor returned by a successful call will be the lowest-numbered
		file descriptor not currently open for the process. (open) from Linux max page 
		http://man7.org/linux/man-pages/man2/open.2.html */
		tfs_mkfs(); 

	}else{
		close(file);
		dev_open(diskfile_path);				
	}
	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	// Step 2: Close diskfile
	dev_close();

}

static int tfs_getattr(const char *path, struct stat *stbuf) {
	printf("%s is being called on %s\n", __FUNCTION__, path);
	if(strlen(path) > PATH_MAX){
		return ENAMETOOLONG; 
	}
	// Step 1: call get_node_by_path() to get inode from path
	struct inode node_buff;
	int node_stat = get_node_by_path(path, 0, &node_buff);
	if(node_stat < 0){
		perror("getting inode from path failed in getattr()");
		return -ENOENT;
	}
	// Step 2: fill attribute of file into stbuf from inode
	stbuf->st_mode   = node_buff.type | 0777;				//only owner with execute permission
	stbuf->st_nlink  = (nlink_t) node_buff.link;
	stbuf->st_size = (off_t) (node_buff.size * BLOCK_SIZE);
	stbuf->st_blocks = node_buff.size;
	stbuf->st_ino = node_buff.ino;
	stbuf->st_atime = node_buff.vstat.st_atime;
	stbuf->st_mtime = node_buff.vstat.st_mtime;
	stbuf->st_ino = (ino_t) node_buff.ino;
	stbuf->st_gid = getgid();
	stbuf->st_uid = getuid();

	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {
	printf("%s is being called\n", __FUNCTION__);
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode inode_buff; 
	int ino = get_node_by_path(path, 0, &inode_buff); // Gets put into the inode_buff if the path exists 
	// Step 2: If not find, return -1
	if(ino < 0){
		perror("directory not found to open");
		return -1; 
	}	

    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
	printf("%s is being called on %s\n", __FUNCTION__, path);
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode node_buff; 
	int inode_stat = get_node_by_path(path, 0, &node_buff);
	if (inode_stat < 0){
		perror("directory does not exist in path");
		return -ENOENT;
	}
	int w_node_stat = writei(node_buff.ino, &node_buff);
	// Step 2: Read directory entries from its data blocks, and copy them to filler
	int i = 0; 
	for(; i < node_buff.size; i++){
		printf("\033[1;31m reading dentries in data block# %d \033[0m\n", i);
		int block_num = node_buff.direct_ptr[i];
		unsigned char buff[BLOCK_SIZE];
		int r_stat = readdb(block_num, (void*) buff);
		int dirent_per_block = BLOCK_SIZE / sizeof(struct dirent); 

		// Going through all the dierctories. 
		struct dirent* dir_buff = (struct dirent*) buff;
		int j = 0;
		for(; j < dirent_per_block; j++)	{ // Reading 16 data blocks since direct_ptr[16] 
			if(dir_buff[j].valid == 1)	{
				printf("\033[1;31m dentry# %d, ino%d : %s is valid \033[0m\n", j, dir_buff[j].ino, dir_buff[j].name);
				filler(buffer, dir_buff[j].name, NULL, 0);  // Filling in our buffer. 
			}
		}
	}

	return 0;
}

static int tfs_mkdir(const char *path, mode_t mode) {
	printf("%s is being called on path \"%s\"\n", __FUNCTION__, path);
	// Before calling get_node_bypath for the parent directory, check if the current path exists 
	struct inode tmp_inode; 
	int current_path = get_node_by_path(path, 0, &tmp_inode);
	
	if ( current_path == 0 )	{
		errno = EEXIST;
		perror("Directory already exists\n");
		return -1; 
	}
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	// Split up path
	
	char copy[PATH_MAX];
	strcpy(copy, path);
	char copy2[PATH_MAX];
	strcpy(copy2, path);
	// Split up path
	char* directory = dirname(copy); 
	char* base = basename(copy2);
	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode inode_buff; 
	int node_stat = get_node_by_path(directory, 0, &inode_buff); // Buff now has the inode information of directory
	// Step 3: Call get_avail_ino() to get an available inode number
	int free_inode = get_avail_ino(); // At number free_inode, there is free spot
	if(free_inode < 0){
		return free_inode;
	}
	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	int dir_add_stat = dir_add(inode_buff, free_inode, base, strlen(base)); // This will add the target directory to parent directory
	// Step 5: Update inode for target directory
	uint16_t parent_ino = inode_buff.ino;
	inode_buff.ino = free_inode;
	inode_buff.valid = 1; 
	
	inode_buff.type = S_IFDIR;
	inode_buff.link = 2;
	inode_buff.size = 1;
	int db_block_num = get_avail_blkno();
	inode_buff.direct_ptr[0] = db_block_num;
	inode_buff.vstat.st_atime = time(NULL);
	inode_buff.vstat.st_mtime = time(NULL);
	dir_add_stat = dir_add(inode_buff, inode_buff.ino, ".", 1);
	dir_add_stat = dir_add(inode_buff, parent_ino, "..", 2);
	// Step 6: Call writei() to write inode to disk
	int w_stat = writei(free_inode, &inode_buff);

	return 0;
}

static int tfs_rmdir(const char *path) {
	printf("%s is being called on path \"%s\"\n", __FUNCTION__, path);
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char copy[PATH_MAX];
	strcpy(copy, path);
	char copy2[PATH_MAX];
	strcpy(copy2, path);	
	char* dname = dirname(copy); 
	char* bname = basename(copy2);
	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode tmp_inode; 
	int get_node_stat = get_node_by_path(path, 0, &tmp_inode);
	
	if (get_node_stat < 0)	{
		errno = ENOENT;
		perror("Directory does not exists\n");
		return -1; 
	}

	// Step 3: Clear data block bitmap of target directory
	unsigned char buff[BLOCK_SIZE];
	int r_stat = bio_read(2, (void *) buff);
	bitmap_t data_bitmap = buff;
	int i = 0;
	for(; i < tmp_inode.size; i++)
	{
		unset_bitmap(data_bitmap, tmp_inode.direct_ptr[i]);
	}
	bio_write(2, buff);

	// Step 4: Clear inode bitmap and its data block
	r_stat = bio_read(1, (void *) buff);
	bitmap_t ino_bitmap = buff;
	uint16_t dir_ino = tmp_inode.ino;
	unset_bitmap(ino_bitmap, dir_ino);
	bio_write(2, buff);

	memset(&tmp_inode, 0, sizeof(struct inode));
	writei(dir_ino, &tmp_inode);
	// Step 5: Call get_node_by_path() to get inode of parent directory
	get_node_stat = get_node_by_path(dname, 0, &tmp_inode);

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	int dir_rm_stat = dir_remove(tmp_inode, bname, strlen(bname));
	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	printf("%s is being called\n", __FUNCTION__);
	struct inode inode_buff;
	int node_stat = get_node_by_path(path, 0, &inode_buff);
	if(node_stat == 0)
	{
		errno = EEXIST;
		perror("file already exists");
		return -1;
	}

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char copy[PATH_MAX];
	strcpy(copy, path);
	char copy2[PATH_MAX];
	strcpy(copy2, path);
	// Split up path
	char* dname = dirname(copy); 
	char* bname = basename(copy2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	node_stat = get_node_by_path(dname, 0, &inode_buff);
	if(node_stat < 0)
	{
		perror("getting inode from path failed in create()");
		return -1;
	}
	// Step 3: Call get_avail_ino() to get an available inode number
	int new_ino_num = get_avail_ino();
	if(node_stat < -1)
	{
		perror("Error getting new inode number");
		return -1;
	}
	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	int dir_add_stat = dir_add(inode_buff, new_ino_num, bname, strlen(bname));
	if(dir_add_stat < -1)
	{
		perror("Error adding directory entry");
		return -1;
	}
	// Step 5: Update inode for target file
	int r_ino_stat = readi(new_ino_num, &inode_buff);
	if(r_ino_stat < 0)
	{
		perror("inode reading failed");
		return -1;
	}
	inode_buff.ino = new_ino_num;
	inode_buff.valid = 1;
	inode_buff.size = 0;
	inode_buff.type = S_IFREG;
	inode_buff.link = 1;
	inode_buff.vstat.st_atime = time(NULL);
	inode_buff.vstat.st_mtime = time(NULL);
	// Step 6: Call writei() to write inode to disk
	int w_ino_stat = writei(new_ino_num, &inode_buff);
	if(w_ino_stat < 0)
	{
		perror("inode writing failed");
		return -1;
	}
	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {
	printf("%s is being called\n", __FUNCTION__);

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode inode_buff;
	int inode_stat = get_node_by_path(path, 0, &inode_buff);
	// Step 2: If not find, return -1
	if(inode_stat < 0)
	{
		perror("path does not exist in open()");
		return -1;
	}

	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	printf("%s is being called with offset %d and size %d\n", __FUNCTION__, offset, size);
	
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode inode_buff;
	int node_stat = get_node_by_path(path, 0, &inode_buff);
	printf("\033[1;32m size of file is %d \033[0m\n", inode_buff.size);
	// Step 2: Based on size and offset, read its data blocks from disk
	// Step 3: copy the correct amount of data from offset to buffer
	int bytes_read = 0;
	int block_index = offset / BLOCK_SIZE; 
	int offset_in_block = offset % BLOCK_SIZE;
	if(block_index >= inode_buff.size){
		errno = EOF;
		return 0;
	}
	while((block_index < inode_buff.size) && (size > 0))
	{
		unsigned char buff[BLOCK_SIZE];
		int block_num = inode_buff.direct_ptr[block_index];
		int r_db_stat = readdb(block_num, buff);
		if((BLOCK_SIZE - offset_in_block) < size)
		{
			
			printf("\033[1;32m size left to read is %d and %d amount can be read from block %d \033[0m\n", size, (BLOCK_SIZE - offset_in_block), block_num);

			printf("\033[1;33m about to write %.*s into the read() buffer \033[0m\n", BLOCK_SIZE - offset_in_block, buff + offset_in_block);

			memcpy(buffer + bytes_read, buff + offset_in_block, BLOCK_SIZE - offset_in_block);
			inode_buff.vstat.st_atime = time(NULL);
			int w_inode_stat = writei(inode_buff.ino, &inode_buff);
			size -= (BLOCK_SIZE - offset_in_block);
			bytes_read += (BLOCK_SIZE - offset_in_block);
			block_index++;
		}else
		{
			printf("\033[1;32m size left to read is %d and %d amount can be read from direct ptr %d \033[0m\n", size, (BLOCK_SIZE - offset_in_block), block_index);

			printf("\033[1;33m about to write %.*s into the read() buffer \033[0m\n", size, buff + offset_in_block);

			memcpy(buffer + bytes_read, buff + offset_in_block, size);
			inode_buff.vstat.st_atime = time(NULL);
			int w_inode_stat = writei(inode_buff.ino, &inode_buff);
			bytes_read += size;
			size = 0;
		}
	}
	if(size > 0)
	{
		errno = EOF;
		perror("file location to be read outside of maximum file size");
	}
	// Note: this function should return the amount of bytes you copied to buffer
	return bytes_read;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode inode_buff;
	int node_stat = get_node_by_path(path, 0, &inode_buff);
	// Step 2: Based on size and offset, read its data blocks from disk
	// Step 3: Write the correct amount of data from offset to disk
	int bytes_read = 0;
	int block_index = 0; 
	int offset_in_block = offset;
	while(size > 0 && block_index < 16)
	{
		unsigned char buff[BLOCK_SIZE];
		int block_num;
		if(block_index < inode_buff.size)
		{
			block_num = inode_buff.direct_ptr[block_index];
			int r_db_stat = readdb(block_num, buff);
		}else{
			block_num = get_avail_blkno();
			int r_db_stat = readdb(block_num, buff);
			inode_buff.size++;
			printf("\033[1;33m file size increased to %d \033[0m\n", inode_buff.size);
			inode_buff.direct_ptr[block_index] = block_num;
			inode_buff.vstat.st_atime = time(NULL);
			inode_buff.vstat.st_mtime = time(NULL);
			int w_inode_stat = writei(inode_buff.ino, &inode_buff);
		}
		if(offset_in_block >= BLOCK_SIZE)
		{
			offset_in_block -= BLOCK_SIZE;
			block_index++;
			continue;
		}		
		if((BLOCK_SIZE - offset_in_block) < size)
		{
			printf("\033[1;33m about to write %.*s into the write() buffer \033[0m\n", BLOCK_SIZE - offset_in_block, buffer + bytes_read);
			memcpy(buff + offset_in_block, buffer + bytes_read, BLOCK_SIZE - offset_in_block);
			inode_buff.vstat.st_atime = time(NULL);
			inode_buff.vstat.st_mtime = time(NULL);
			int w_inode_stat = writei(inode_buff.ino, &inode_buff);
			int w_db_stat = writedb(block_num, buff);
			offset_in_block = 0;
			size -= (BLOCK_SIZE - offset_in_block);
			bytes_read += (BLOCK_SIZE - offset_in_block);
			block_index++;
		}else
		{
			printf("\033[1;33m about to write %.*s into the write() buffer \033[0m\n", size, buffer + bytes_read);
			memcpy(buff + offset_in_block, buffer + bytes_read, size);
			inode_buff.vstat.st_atime = time(NULL);
			inode_buff.vstat.st_mtime = time(NULL);
			int w_inode_stat = writei(inode_buff.ino, &inode_buff);
			int w_db_stat = writedb(block_num, buff);
			bytes_read += size;
			size = 0;
			break;	
		}
	}
	if(size > 0)
	{
		errno = EOVERFLOW;
		perror("file location to be read outside of maximum file size");
	}
	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return bytes_read;
}

static int tfs_unlink(const char *path) {
	printf("%s is being called on path \"%s\"\n", __FUNCTION__, path);
	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char copy[PATH_MAX];
	strcpy(copy, path);
	char copy2[PATH_MAX];
	strcpy(copy2, path);	
	char* dname = dirname(copy); 
	char* bname = basename(copy2);
	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode tmp_inode; 
	int get_node_stat = get_node_by_path(path, 0, &tmp_inode);
	
	if (get_node_stat < 0)	{
		errno = ENOENT;
		perror("Directory does not exists\n");
		return -1; 
	}

	// Step 3: Clear data block bitmap of target directory
	unsigned char buff[BLOCK_SIZE];
	int r_stat = bio_read(2, (void *) buff);
	bitmap_t data_bitmap = buff;
	int i = 0;
	for(; i < tmp_inode.size; i++)
	{
		unset_bitmap(data_bitmap, tmp_inode.direct_ptr[i]);
	}
	bio_write(2, buff);

	// Step 4: Clear inode bitmap and its data block
	r_stat = bio_read(1, (void *) buff);
	bitmap_t ino_bitmap = buff;
	uint16_t dir_ino = tmp_inode.ino;
	unset_bitmap(ino_bitmap, dir_ino);
	bio_write(2, buff);

	memset(&tmp_inode, 0, sizeof(struct inode));
	writei(dir_ino, &tmp_inode);
	// Step 5: Call get_node_by_path() to get inode of parent directory
	get_node_stat = get_node_by_path(dname, 0, &tmp_inode);

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	int dir_rm_stat = dir_remove(tmp_inode, bname, strlen(bname));
	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

	return fuse_stat;
}

