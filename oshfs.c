/*/*
* Copyright © 2018 Zitian Li <ztlizitian@gmail.com>
* This program is free software. It comes without any warranty, to
* the extent permitted by applicable law. You can redistribute it
* and/or modify it under the terms of the Do What The Fuck You Want
* To Public License, Version 2, as published by Sam Hocevar. See
* http://www.wtfpl.net/ for more details.
*/
#define FUSE_USE_VERSION 26
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fuse.h>
#include <sys/mman.h>
typedef struct filenode {
	char *filename;
	void *content;
	int fblock;//��� 
	int hsize;//�����ô�С 
	struct stat *st;
	struct filenode *next;
	struct filenode *last;
}node;//��˫������

static const size_t size = 4 * 1024 * 1024 * (size_t)1024;//����32λǿ��ת��
static void *mem[8192];
size_t blocknr = 8192;//2^13
size_t blocksize = 524288;//������˵õ�4G,size/blocknr
static int const head = 16; //�ڿ�ͷ����������,һ��Ϊ������ţ�һ��Ϊ���ӵ���һ�������û��Ϊ0

static int initializeblock(int block_num) {

	if (mem[block_num]) {
		return -1;
	}
	mem[block_num] = mmap(NULL, blocksize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	memset(mem[block_num], 0, blocksize);
	*(int*)mem[block_num] = head;
	return 0;
}

static void *allocate(int block_num, int size) {
	if (mem[block_num] == NULL) {
		return NULL;
	}
	int offset = *(int*)mem[block_num];
	if (size + offset>blocksize) size = blocksize - offset;
	*(int*)mem[block_num] += size;
	return (void*)((char*)mem[block_num] + offset);
}
int findnew() {
	int *a = (int*)((char*)mem[0] + 8);
	int blocknum = *(int*)((char*)mem[0] + 8);
	int i = blocknum;
	for (i = (blocknum + 1) % blocknr;i != blocknum;i = (i + 1) % blocknr) {
		if (!mem[i]) {
			blocknum = i;
			*a = blocknum;
			return i;
		}
	}
	return -1;
}

static struct filenode *get_filenode(const char *name)
{
	node *node1 = *(node**)((char *)mem[0] + head);//
	while (node1) {
		if (strcmp(node1->filename, name + 1) != 0)
			node1 = node1->next;
		else
			return node1;
	}
	return NULL;
}

static int create_filenode(const char *filename, const struct stat *st)
{
	int i;
	i = findnew();
	if (i == -1)
		return -1;
	int sig;
	sig=initializeblock(i);
	if (sig == -1)
		return -1;
	node *root = *(node**)((char*)mem[0] + head);
	node *newnode = (node *)allocate(i, sizeof(node));
	newnode->filename = (char *)allocate(i, strlen(filename) + 1);
	newnode->st = (struct stat *)allocate(i, sizeof(struct stat));
	if (newnode->st == NULL | newnode->filename == NULL | newnode == NULL)
		return -1;
	memcpy(newnode->filename, filename, strlen(filename) + 1);
	memcpy(newnode->st, st, sizeof(struct stat));
	newnode->next = root;
	newnode->last = NULL;
	if (root) root->last = newnode;
	newnode->hsize = *(int*)mem[i];
	newnode->content = (void*)((char*)mem[i] + newnode->hsize);
	newnode->fblock = i;
	root = newnode;
	node **rootadr = (node**)((char*)mem[0] + head);
	*rootadr = root;
	return 0;
}
static void *oshfs_init(struct fuse_conn_info *conn)
{
	initializeblock(0);
    node **rootadr = (node**)allocate(0, sizeof(node*));
	*rootadr = NULL;
	return NULL;
}
static int oshfs_getattr(const char *path, struct stat *stbuf)
{
	int ret = 0;
	struct filenode *node = get_filenode(path);
	if (strcmp(path, "/") == 0) {
		memset(stbuf, 0, sizeof(struct stat));
		stbuf->st_mode = S_IFDIR | 0755;
	}
	else if (node) {
		memcpy(stbuf, node->st, sizeof(struct stat));
	}
	else {
		ret = -ENOENT;
	}
	return ret;
}

static int oshfs_mknod(const char *path, mode_t mode, dev_t dev)
{
	struct stat st;
	int a;
	st.st_mode = S_IFREG | 0644;
	st.st_uid = fuse_get_context()->uid;
	st.st_gid = fuse_get_context()->gid;
	st.st_nlink = 1;
	st.st_size = 0;
	a = create_filenode(path + 1, &st);
	if (a == -1)
		return -ENOMEM;
	return 0;
}

static int oshfs_open(const char *path, struct fuse_file_info *fi)
{
	return 0;
}
static int oshfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	struct filenode *node1 = *(node**)((char*)mem[0] + head);
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	while (node1) {
		filler(buf, node1->filename, node1->st, 0);
		node1 = node1->next;
	}
	return 0;
}
static int oshfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	node *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	if (offset + size > node->st->st_size)
		node->st->st_size = offset + size;
	//offsetΪд���ļ����ݵ�ƫ������sizeΪд���ļ��Ĵ�С��st_sizeΪ�ļ����ݴ�С
	int  complete = 0;//����ɵ�
	int space = blocksize - node->hsize;//���ÿռ��С
	int fblock = node->fblock;
	int i;
	int sig;
	while (space<offset) {//��ʣ��ռ䲻��ƫ����
		if (!mem[fblock]) return -1;
		if (*((int*)mem[fblock] + 1) == 0) {//����ÿ�Ϊβʱ
			int nextblock = findnew();
			if (nextblock == -1)
				return -ENOMEM;
			sig=initializeblock(nextblock);
			if (sig == -1)
				return -EEXIST;
			*(int*)mem[fblock] = blocksize;//����ǰ����Ϊ����
			*((int*)mem[fblock] + 1) = nextblock;//����ǰ�����һ����Ϊnextblock
		}
		space += blocksize - head;
		fblock = *((int*)mem[fblock] + 1);
	}
	int c = size<(space - offset) ? size : (space - offset);
	memcpy((char*)mem[fblock] + blocksize - (space - offset), buf, c);
	complete += space - offset;
	while (complete<size) {//��δ���д��ʱ
		if (!mem[fblock]) return -1;
		if (*((int*)mem[fblock] + 1) == 0) {//����ÿ�Ϊβʱ
			int nextblock = findnew();
			if (nextblock == -1)
				return -ENOMEM;
			sig=initializeblock(nextblock);
			if (sig == -1)
				return -EEXIST;
			*(int*)mem[fblock] = blocksize;//����ǰ����Ϊ����
			*((int*)mem[fblock] + 1) = nextblock;//����ǰ�����һ����Ϊnextblock
		}
		fblock = *((int*)mem[fblock] + 1);
		c = (size - complete) < (blocksize - head) ? (size - complete) : (blocksize - head);
		memcpy((char*)mem[fblock] + head, buf + complete, c);
		complete += blocksize - head;
	}
	*(int*)mem[fblock] = blocksize - (complete - size);
	return size;
}

static int oshfs_truncate(const char *path, off_t size)
{
	node *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	node->st->st_size = size;
	int sig;
	int space = blocksize - node->hsize, fblock = node->fblock;
	int a;
	while (space<size) {
		if (!mem[fblock]) return -1;
		a = *((int*)mem[fblock] + 1);
		if (a == 0) {//ͬ��write
			int nextblock = findnew();
			if (nextblock == -1)
				return -ENOMEM;
			sig=initializeblock(nextblock);
			if (sig == -1)
				return -EEXIST;
			*(int*)mem[fblock] = blocksize;//����ǰ����Ϊ����
			*((int*)mem[fblock] + 1) = nextblock;//����ǰ�����һ����Ϊnextblock
		}
		fblock = *((int*)mem[fblock] + 1);
		space += blocksize - head;
	}
	*(int*)mem[fblock] = blocksize - (space - size);
	int next;
	int stay = fblock;
	fblock = *((int*)mem[fblock] + 1);//��һ�����								  //�ͷ�֮��Ŀ�
	if (fblock != 0) {
		next = *((int*)mem[fblock] + 1);
		munmap((char*)mem[fblock], blocksize);
		mem[fblock] = NULL;
		fblock = next;
	}
	//���ÿ���һ������
	*((int*)mem[stay] + 1) = 0;
	return 0;
}

static int oshfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	node *node = get_filenode(path);
	if (node == NULL)
		return -ENOENT;
	int ret = size;
	int space = blocksize - node->hsize, fblock = node->fblock;
	//ͬwrite��offset��size
	if (offset + size > node->st->st_size)
		ret = node->st->st_size - offset;
	while (space<offset) {//ͬwrite�в���
		fblock = *((int*)mem[fblock] + 1);
		space += blocksize - head;
	}
	int c = ret < (space - offset) ? ret : (space - offset);
	memcpy(buf, (char*)mem[fblock] + blocksize - (space - offset), c);

	int complete = space - offset;
	while (complete<ret) {
		fblock = *((int*)mem[fblock] + 1);
		if (fblock == 0) break;
		c = (ret - complete)< (blocksize - head) ? (ret - complete) : (blocksize - head);
		memcpy(buf + complete, (char*)mem[fblock] + head, c);
		complete += blocksize - head;
	}
	return ret;
}


static int oshfs_unlink(const char *path)
{
	node *root = *(node**)((char*)mem[0] + head);
	node *node1 = get_filenode(path);
	if (node1 == NULL)
		return -ENOENT;
	int fblock = node1->fblock, next;
	if (!node1->last) {
		root = node1->next;
		if (root)
			root->last = NULL;
	}
	else {
		node1->last->next = node1->next;
	}
	if (node1->next)
		node1->next->last = node1->last;
	while (fblock) {
		next = *((int*)mem[fblock] + 1);
		munmap((char*)mem[fblock], blocksize);
		mem[fblock] = NULL;
		fblock = next;
	}
	node **rootadr = (node**)((char*)mem[0] + head);
	*rootadr = root;
	return 0;
}
static const struct fuse_operations op = {
	.init = oshfs_init,
	.getattr = oshfs_getattr,
	.readdir = oshfs_readdir,
	.mknod = oshfs_mknod,
	.open = oshfs_open,
	.write = oshfs_write,
	.truncate = oshfs_truncate,
	.read = oshfs_read,
	.unlink = oshfs_unlink,
};
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &op, NULL);
}