# Lab03文件系统
## 文件结构
在我的程序中我将块大小设为2^19（524288）,将块个数设为2^13（8192），总大小为4G，具体可在函数开头更改各参数大小
每个块中开头留两个int大小（以防万一留了16字节，可在开头更改）用于存放块信息（块序号和该文件下一个块的序号）
在第一个块中存放了root指针
struct filenode {
	char *filename;
	void *content;
  struct filenode *next;
	struct filenode *previous;
	int fblock;//块号 
	int hsize;//块已用大小 
	struct stat *st;
};
节点形式为一简单双向链表，换成单链表亦可（具体名字和程序中略有不一样）
（其中文件名未指定大小，按需要分配）
## 函数
oshfs_init 用于创建文件系统，初始化第0块 oshfs_getattr用于获取文件信息

oshfs_reddir用于获取所有文件的filename

oshfs_mknod用于创建新的文件结点

oshfs_write用于写文件

oshfs_read用于读文件

oshfs_truncate用于改变文件大小

oshfs_unlink用于删除文件

get_filenode用于找到文件所在

create_filenode用于创建文件

initialblock用于初始化各块

allocate用于块内分配

finenew用于找到下一个可用块
## 实现过程
我重点在实现init,write,truncate,read,unlink

其中对oshfs_init，初始化第0块，将root指针放入

对oshfs_write,根据偏移量是否大于剩余空间和是否已完成写入，循环分配新的块，并建立联系

对oshfs_read,如果要读的内容超过已有的，要只读到已有的最后

对oshfs_truncate，要释放掉截停后多余的空间

对oshfs_unlink,即释放掉文件的空间并复原
## 结果
实验测试上的示例均可实现，并且具有一定的健壮性
!["示例"](https://raw.githubusercontent.com/wtx1999/pics/master/%E5%9B%BE%E7%89%871.png)
可实现大空间的写入，示例中的2G内容可以执行成功，但需要花费数分钟的时间，速度随占空间大小增大减慢速度较快。
