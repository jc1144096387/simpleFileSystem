#include <stdio.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

//定义常量
#define BLOCKSIZE 1024  //磁盘块大小
#define SIZE 1024000    //虚拟磁盘空间大小
#define BLOCKNUM 1000   //磁盘块数量
#define END 65535       //FAT中的文件结束标志
#define FREE 0          //FAT中盘块空闲标志
#define ROOTBLOCKNUM 2  //根目录初始所占盘块总数
#define MAXOPENFILE 10  //最多同时打开文件个数

//文件控制块FCB
typedef struct FCB {
  char filename[8];//文件名
  char exname[3];//文件扩展名
  unsigned char attribute; // 文件属性字段：值为 0 时表示目录文件，值为 1 时表示数据文件
  unsigned short time;//文件创建时间
  unsigned short data;//文件创建日期
  unsigned short first; // 文件起始盘块号
  unsigned long length; // 文件长度（字节数）
  char free; //只对目录文件有效，表示目录项是否为空，若值为0，则表示空；值为1，表示已分配
} fcb;

//文件分配表
typedef struct FAT {
  unsigned short id;
} fat;

//用户打开文件表
typedef struct USEROPEN {
  // char filename[80];//文件名
  // char exname[3];//文件扩展名
  // unsigned char attribute; // 文件属性字段：值为 0 时表示目录文件，值为 1 时表示数据文件
  // unsigned short time;//文件创建时间
  // unsigned short data;//文件创建日期
  // unsigned short first; // 文件起始盘块号
  // unsigned long length; // 文件长度（字节数）
  fcb open_fcb; // 文件的 FCB 中的内容
//以上内容是文件的FCB中的内容，下面是文件使用中的动态信息
  int dirno;//打开文件的目录项在父目录文件中的盘块号
  int diroff;//打开文件的目录项在父目录文件的dirno盘块中的目录项序号
  char dir[80]; // 打开文件所在路径，方便快速检查出指定文件是否已经打开
  int count; // 读写指针在文件中的位置
  char fcbstate; // 是否修改了文件的 FCB 的内容，如果修改了置为 1，否则为 0
  char topenfile; // 表示该用户打开表项是否为空，若值为 0，表示为空，否则表示已被某打开文件占据
} useropen;

//引导块
typedef struct BLOCK0 {
  char information[200];//存储一些描述信息，如磁盘块大小、磁盘块数量等
  unsigned short root;//根目录文件的起始盘块号
  unsigned char *startblock;//虚拟磁盘上的数据区开始位置
} block0;

//函数声明
//进入文件系统函数
void startsys();
//退出文件系统函数
void my_exitsys();

//磁盘格式化函数
void my_format();

/*
**  目录管理函数
*/

//更改当前目录函数
void my_cd(char* dirname);

//创建子目录函数
void my_mkdir(char* dirname);

//删除子目录函数
void my_rmdir(char* dirname);

//显示目录函数
void my_ls();

/*
**  文件管理函数
*/

//创建文件函数
int my_create(char* filename);

//删除文件函数
void my_rm(char* filename);

//打开文件函数
int my_open(char* filename);

//关闭文件函数
int my_close(int fd);

/*
**  文件读写函数
*/

//写文件函数
int my_write(int fd);

//实际写文件函数
int do_write(int fd, char *text, int len, char wstyle);
//读文件函数
int my_read (int fd, int len);

//实际读文件函数
int do_read (int fd, int len, char *text);


//初始化fcb
void fcb_init(fcb *new_fcb, const char* filename, unsigned short first, unsigned char attribute);

//初始化用户打开文件表项
void useropen_init(useropen *openfile, int dirno, int diroff, const char* dir);



//全局变量定义
unsigned char *myvhard;//指向虚拟磁盘的起始地址
useropen openfilelist[MAXOPENFILE];//用户打开文件表数组
useropen *ptrcurdir;//当前目录内容所在缓冲区地址
char currentdir[80];//记录当前目录的目录名（包括目录的路径）
unsigned char* startp;//记录虚拟磁盘上数据区开始位置
unsigned int curdirID; //当前目录在打开文件表项的序号

unsigned char *blockaddr[BLOCKNUM];//磁盘块地址
block0 initblock;//引导块
fat fat1[BLOCKNUM], fat2[BLOCKNUM];//fat表



//主函数
int main(){
  int fd;
  //调用startsys()在虚拟磁盘上恢复或建立文件系统 并 初始化相关的全局变量
  startsys();
  //等待用户输入文件操作命令
  char command[20];
  scanf("%s",command);
  while(strcmp(command,"my_exitsys") != 0){
    if (!strcmp(command, "my_ls")) {
      my_ls();
    }
    else if (!strcmp(command, "my_mkdir")) {
      scanf("%s", command);
      my_mkdir(command);
    }
    else if (!strcmp(command, "my_close")) {
      scanf("%d", &fd);
      my_close(fd);
    }
    else if (!strcmp(command, "my_open")) {
      scanf("%s", command);
      if (!rewrite_dir(command)) continue;
      fd = my_open(command);
      if (0 <= fd && fd < MAXOPENFILE) {
        if (!openfilelist[fd].open_fcb.attribute) {
          my_close(fd);
          printf("%s is dirictory, please use cd command\n", command);
        }
        else {
          printf("%s is open, it\'s id is %d\n", openfilelist[fd].dir, fd);
        }
      }
    }
    else if (!strcmp(command, "my_cd")) {
      scanf("%s", command);
      my_cd(command);
    }
    else if (!strcmp(command, "my_create")) {
      scanf("%s", command);
      if (!rewrite_dir(command)) continue;
      fd = my_create(command);
      if (0 <= fd && fd < MAXOPENFILE) {
        printf("%s is created, it\'s id is %d\n", openfilelist[fd].dir, fd);
      }
    }
    else if (!strcmp(command, "my_rm")) {
      scanf("%s", command);
      my_rm(command);
    }
    else if (!strcmp(command, "my_rmdir")) {
      scanf("%s", command);
      my_rmdir(command);
    }
    else if (!strcmp(command, "my_read")) {
      scanf("%d", &fd);
      my_read(fd);
    }
    else if (!strcmp(command, "my_write")) {
      scanf("%d", &fd);
      my_write(fd);
    }
    else {
      printf("command %s : no such command\n", command);
    }

    scanf("%s",command);
  }

  my_exitsys();
  return 0;
}

//进入文件系统函数
void startsys(){
  //申请虚拟磁盘空间
  myvhard = (unsigned char*)malloc(SIZE);
  //设置磁盘块地址，方便之后操作
  for (int i = 0; i < BLOCKNUM; i++) blockaddr[i] = i * BLOCKSIZE + myvhard;
  //调用c的fopen()打开mysys文件，rb表示以只读方式打开一个二进制文件
  FILE *fp = fopen("mysys", "rb");

  //如果文件存在
  if(fp != NULL){
    //将mysys文件读到缓冲区中
    unsigned char *buf = (unsigned char*)malloc(SIZE);
    fread(buf, 1, SIZE, fp);
    //void *memcpy(void *dest, const void *src, size_t n);
    //从源src所指的内存地址的起始位置开始拷贝n个字节到目标dest所指的内存地址的起始位置中
    //将第一个磁盘块（保留区/引导块）的内容拷贝到initblock中
    memcpy(&initblock, blockaddr[0], sizeof(block0));
    //判断文件系统魔数是否正确
    if (strcmp(initblock.information, "10101010") != 0){
      printf("文件系统魔数出错,重新创建文件系统\n");
      my_format();
    }
    //将缓冲区内容复制到虚拟磁盘中
    memcpy(myvhard, buf, SIZE);
    //读入fat信息
    memcpy(fat1, blockaddr[1], sizeof(fat1));
    memcpy(fat2, blockaddr[3], sizeof(fat2));
    //释放缓冲区
    free(buf);
    //关闭mysys文件
    fclose(fp);
  }else{
    //文件不存在
    printf("mysys 文件系统不存在，现在开始创建文件系统\n");
    //调用my_format()格式化虚拟磁盘
    my_format();
  }

  
  //初始化用户打开文件表
  for (int i = 0; i < MAXOPENFILE; i++) openfilelist[i].topenfile = 0;
  //把根目录fcb放入打开文件表中
  memcpy(&openfilelist[curdirID].open_fcb, blockaddr[5], sizeof(fcb));
  useropen_init(&openfilelist[curdirID], 5, 0, "~/");
  //将当前目录设置为根目录
  curdirID = 0;
  strcpy(currentdir,"~/");
}

//退出文件系统函数
void my_exitsys(){
  // 先关闭所有打开文件项
  // for (int i = 0; i < MAXOPENFILE; i++) my_close(i);
  
  memcpy(blockaddr[0], &initblock, sizeof(initblock));
  memcpy(blockaddr[1], fat1, sizeof(fat1));
  memcpy(blockaddr[3], fat2, sizeof(fat2));

  //调用C的fopen()打开mysys文件
  FILE *fp = fopen("myfsys", "wb");
  //调用C的fwrite()将虚拟磁盘内容写入mysys文件
  fwrite(myvhard, BLOCKSIZE, BLOCKNUM, fp);
  //调用C的fclose()关闭mysys文件
  fclose(fp);
  //释放虚拟磁盘内存空间
  free(myvhard);
}

//磁盘格式化函数
void my_format(){
  //初始化虚拟硬盘保留区(引导块)内容
  strcpy(initblock.information, "10101010");//设置文件系统魔数
  initblock.root = 5;
  initblock.startblock = blockaddr[5];
  //在保留区后面建立两张FAT表，每个FAT占两个块
  for (int i = 0; i < 5; i++) fat1[i].id = END;
  for (int i = 5; i < BLOCKNUM; i++) fat1[i].id = FREE;
  for (int i = 0; i < BLOCKNUM; i++) fat2[i].id = fat1[i].id;
  //将数据区第一块分配给根目录文件，并初始化根目录
  fat1[5].id = END;
  fat2[5].id = END;
  fcb root;
  fcb_init(&root, ".", 5, 0);
  memcpy(blockaddr[5], &root, sizeof(fcb));
  strcpy(root.filename, "..");
  memcpy(blockaddr[5] + sizeof(fcb), &root, sizeof(fcb));

  printf("my_format completed\n");
}

/*
**  目录管理函数
*/

//更改当前目录函数
void my_cd(char* dirname){
  //调用my_open()打开指定目录文件dirname的父目录文件

  //调用my_read()读入父目录文件到内存缓冲区

  //在父目录文件中检索dirname文件

  //dirname文件是否存在？若不存在则出错返回，否则继续执行

    //调用my_close()关闭父目录文件

    //调用my_close()关闭原当前目录文件

    //dirname文件是否已经打开？

    //若没有打开，则调用my_open()打开dirname文件,否则跳过这一段

    //调用my_read()读入dirname文件内容

    //设置当前目录为dirname
      //useropen *ptrcurdir:当前目录内容所在缓冲区地址

      //char currentdir[80]:记录当前目录的目录名（包括目录的路径）
      
      //unsigned int curdirID:当前目录在打开文件表项的序号
      curdirID = fd;
}

//创建子目录函数
void my_mkdir(char* dirname){
  //在当前目录下创建?如果dirname[0] == '/'表示不是在当前目录下创建
  if(dirname[0] == '/'){
    //打开并读入父目录文件内容

    //使用dirname检索父目录文件

  }else{
    //使用dirname检索当前目录文件

  }

}

//删除子目录函数
void my_rmdir(char* dirname){

}

//显示目录函数
void my_ls(){
  //从头开始遍历当前目录文件内容

  //按一定格式显示目录文件的内容


}

/*
**  文件管理函数
*/

//创建文件函数
int my_create(char* filename){
  //在当前目录下创建?如果filename[0] == '/'表示不是在当前目录下创建
  if(filename[0] == '/'){
    //打开并读入父目录文件内容

    //使用filename检索父目录文件

  }else{
    //使用filename检索当前目录文件

  }
}

//删除文件函数
void my_rm(char* filename){

}

//打开文件函数
int my_open(char* filename){
  //使用filename检索用户打开文件表

  //文件已打开？

}

//关闭文件函数
int my_close(int fd){
  
}

/*
**  文件读写函数
*/

//写文件函数
int my_write(int fd){

}

//实际写文件函数
int do_write(int fd, char *text, int len, char wstyle){

}

//读文件函数
int my_read (int fd, int len){
  
}

//实际读文件函数
int do_read (int fd, int len, char *text){
  //用malloc()申请缓冲区buf,大小与磁盘块相同

  //分配成功？

    //否
    //出错返回

    //是
    //将读写指针转化为逻辑快块号和块内偏移off
    
    //将逻辑块号转换成磁盘块号blkno，再转化成虚拟磁盘上的内存位置ptr

    //将ptr开始的一整块内容读入缓冲区buf

    //(盘块长度-off)>=len？

      //是
      //从buf读出len字节到text[]中

      //已读字节数=len

      //否
      //读出(盘块长度-off)字节到text[]

      //读写指针值 +=本次已读字节数

      //len -= 本次已读字节数

      //已读字节数 += 本次已读字节数

      //len>0且未到文件尾

        //是，返回 将读写指针转化为逻辑快块号和块内偏移off


        //否，退出判断，执行下面的代码
    //释放buf，返回已读字节数

}


//初始化fcb
void fcb_init(fcb *new_fcb, const char* filename, unsigned short first, unsigned char attribute) {
  strcpy(new_fcb->filename, filename);
  new_fcb->first = first;
  new_fcb->attribute = attribute;
  new_fcb->free = 0;
  if (attribute) new_fcb->length = 0;
  else new_fcb->length = 2 * sizeof(fcb);
}

//初始化用户打开文件表项
void useropen_init(useropen *openfile, int dirno, int diroff, const char* dir) {
  openfile->dirno = dirno;
  openfile->diroff = diroff;
  strcpy(openfile->dir, dir);
  openfile->fcbstate = 0;
  openfile->topenfile = 1;
  openfile->count = openfile->open_fcb.length;
}



