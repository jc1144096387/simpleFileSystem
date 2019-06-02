#include <stdio.h>
#include <time.h>
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
#define MAXTEXT 10000   //文件内容的最大长度

//文件控制块FCB
typedef struct FCB {
  char filename[8];//文件名
  char exname[3];//文件扩展名
  unsigned char attribute; // 文件属性字段：值为 0 时表示目录文件，值为 1 时表示数据文件
  unsigned short time;//文件创建时间
  unsigned short date;//文件创建日期
  unsigned short first; // 文件起始盘块号
  unsigned long length; // 文件长度（字节数）
  char free; //只对目录文件有效，表示目录项是否为空，若值为0，则表示空；值为1，表示已分配
} fcb;

//文件分配表
typedef struct FAT {
  unsigned short id;//FREE表示空闲，END表示是某文件的最后一个磁盘块，其他值表示下一个磁盘块块号
} fat;

//用户打开文件表
typedef struct USEROPEN {
  char filename[8];//文件名
  char exname[3];//文件扩展名
  unsigned char attribute; // 文件属性字段：值为 0 时表示目录文件，值为 1 时表示数据文件
  unsigned short time;//文件创建时间
  unsigned short date;//文件创建日期
  unsigned short first; // 文件起始盘块号
  unsigned long length; // 文件长度（字节数）
  char free;
//以上内容是文件的FCB中的内容，下面是文件使用中的动态信息
  int dirno;//打开文件的目录项在父目录文件中的盘块号
  int diroff;//打开文件的目录项在父目录文件的dirno盘块中的目录项序号
  char dir[80]; // 打开文件所在路径，方便快速检查出指定文件是否已经打开
  int count; // 读写指针在文件中的位置
  char fcbstate; // 是否修改了文件的 FCB 的内容，如果修改了置为 1，否则为 0
  char topenfile; // 表示该用户打开文件表表项是否为空，若值为 0，表示为空，否则表示已被某打开文件占据
  int father; //父目录在用户打开文件表的位置
} useropen;

//引导块
typedef struct BLOCK0 {
  char magic[10]; //文件系统魔数
  char information[200];//存储一些描述信息，如磁盘块大小、磁盘块数量等
  unsigned short root;//根目录文件的起始盘块号
  unsigned char *startblock;//虚拟磁盘上的数据区开始位置
} block0;

//全局变量定义
unsigned char *myvhard;//指向虚拟磁盘的起始地址
useropen openfilelist[MAXOPENFILE];//用户打开文件表数组
useropen *ptrcurdir;//当前目录内容所在缓冲区地址
char currentdir[80];//记录当前目录的目录名（包括目录的路径）
unsigned char* startp;//记录虚拟磁盘上数据区开始位置
unsigned int curdirID; //当前目录在打开文件表项的序号


//函数声明
void startsys();
void my_format();
void my_cd(char *dirname);
void my_mkdir(char *dirname);
void my_rmdir(char *dirname);
void my_ls();
int my_create(char *filename);
void my_rm(char *filename);
int my_open(char *filename);
int my_close(int fd);
int my_write(int fd);
int do_write(int fd, char *text, int len, char wstyle);
int my_read(int fd, int len);
int do_read(int fd, int len, char *text);
void my_exitsys();

unsigned short findblock();//寻找空闲盘块
int findopenfile();//寻找空闲文件表项

//主函数
int main(){
  int fd;
  //调用startsys()在虚拟磁盘上恢复或建立文件系统 并 初始化相关的全局变量
  startsys();
  printf("%s ",openfilelist[curdirID].dir);
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
      my_open(command);
    }
    else if (!strcmp(command, "my_cd")) {
      scanf("%s", command);
      my_cd(command);
    }
    else if (!strcmp(command, "my_create")) {
      scanf("%s", command);
      my_create(command);
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
      int len;
      scanf("%d %d", &fd, &len);
      my_read(fd,len);
    }
    else if (!strcmp(command, "my_write")) {
      scanf("%d", &fd);
      my_write(fd);
    }
    else if (!strcmp(command, "my_format")) {
      my_format();
    }
    else {
      printf("command %s : no such command\n", command);
    }
    printf("%s ",openfilelist[curdirID].dir);
    scanf("%s",command);
  }

  my_exitsys();
  return 0;
}

//进入文件系统函数:在虚拟磁盘上建立文件系统
void startsys(){
  FILE *fp;
  unsigned char buf[SIZE];
  fcb *root;
  //申请虚拟磁盘空间
  myvhard = (unsigned char*)malloc(SIZE);
  memset(myvhard, 0, SIZE);
  //调用c的fopen()打开mysys文件，rb表示以只读方式打开一个二进制文件
  fp = fopen("mysys", "rb");

  //如果文件存在
  if(fp != NULL){
    //将mysys文件读到缓冲区中
    fread(buf, 1, SIZE, fp);
    //void *memcpy(void *dest, const void *src, size_t n);
    //从源src所指的内存地址的起始位置开始拷贝n个字节到目标dest所指的内存地址的起始位置中
    //判断文件系统魔数是否正确
    if (strcmp(((block0 *)buf)->magic, "10101010") != 0){
      // printf("文件系统魔数出错,重新创建文件系统\n");
      // my_format();
      printf("文件系统魔数出错,出错返回\n");
      return -1;
    }else{
      //将缓冲区内容复制到虚拟磁盘中
      memcpy(myvhard, buf, SIZE);
    }
  }else{
    //文件不存在
    //调用C的creat()创建mysys文件
    creat("mysys",0777);
    //调用my_format()格式化虚拟磁盘
    printf("mysys文件系统不存在，现在开始创建文件系统\n");
    my_format();
    //将虚拟硬盘的内容写入mysys文件
    fp = fopen("mysys","wb");
    fwrite(myvhard,SIZE,1,fp);
  }

  //关闭mysys文件
  //注意：如果fp == NULL，不需要关闭，否则会出错
  fclose(fp);

  //初始化用户打开文件表,将表项 0 分配给根目录文件使用，并填写根目录文件的相关信息
  root = (fcb *)(myvhard + 5*BLOCKSIZE);
  strcpy(openfilelist[0].filename, root->filename);
  strcpy(openfilelist[0].exname,root->exname);
  openfilelist[0].attribute = root->attribute;
  openfilelist[0].time = root->time;
  openfilelist[0].date = root->date;
  openfilelist[0].first = root->first;
  openfilelist[0].length = root->length;
  openfilelist[0].free = root->free;
  openfilelist[0].dirno = 5;
  openfilelist[0].diroff = 0;
  strcpy(openfilelist[0].dir, "~");
  openfilelist[0].father = 0;
  openfilelist[0].count = 0;
  openfilelist[0].fcbstate = 0;
  openfilelist[0].topenfile = 1;  
  for (int i = 1; i < MAXOPENFILE; i++){
    openfilelist[i].topenfile = 0;
  }
  //将当前目录设置为根目录
  curdirID = 0;
  strcpy(currentdir,"~");
  
  startp = ((block0 *)myvhard)->startblock;
  ptrcurdir=&openfilelist[0];
}

//退出文件系统函数
void my_exitsys(){
  //关闭打开的文件,撤销用户打开文件表，释放其内存空间
  for(int i = 0; i < 10; i ++){
    if(openfilelist[i].topenfile){
      my_close(i);
    }
  }
  //调用C的fopen()打开mysys文件
  FILE *fp = fopen("mysys", "wb");
  //调用C的fwrite()将虚拟磁盘内容写入mysys文件
  fwrite(myvhard, BLOCKSIZE, BLOCKNUM, fp);
  //调用C的fclose()关闭mysys文件
  fclose(fp);
  //释放虚拟磁盘内存空间
  free(myvhard);
}

//磁盘格式化函数
void my_format(){
  FILE *fp;
  fat *fat1, *fat2;
  block0 *blk0;
  time_t now;
  struct tm *nowtime;
  fcb *root;

  blk0 = (block0 *)myvhard;
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  fat2 = (fat *)(myvhard + 3*BLOCKSIZE);
  root = (fcb *)(myvhard + 5*BLOCKSIZE);
  //初始化虚拟硬盘保留区(引导块)内容
  strcpy(blk0->magic, "10101010");//设置文件系统魔数
  strcpy(blk0->information, "My Simple FileSystem\nBLOCKSIZE=1KB\nSIZE=1000KB\nBLOCKNUM=1000\nROOTBLOCKNUM=2\n");
  blk0->root = 5;//根目录文件的起始盘块号
  blk0->startblock = (unsigned char *)root;
  //在保留区后面建立两张FAT表，每个FAT占两个块
  for(int i = 0; i < 5; i ++){
    fat1->id = END;
    fat2->id = END;
    fat1++;
    fat2++;
  }
  //将数据区前两个数据块分配给根目录文件，并初始化根目录
  fat1->id = 6;
  fat2->id = 6;
  fat1++;
  fat2++;
  fat1->id = END;
  fat2->id = END;
  fat1++;
  fat2++;

  for(int i = 7; i < BLOCKNUM; i ++){
    fat1->id = FREE;
    fat2->id = FREE;
    fat1 ++;
    fat2 ++;
  }

  now = time(NULL);
  nowtime = localtime(&now);
  strcpy(root->filename,".");
  strcpy(root->exname,"");
  root->attribute = 0;
  root->time = nowtime->tm_hour*2048 + nowtime->tm_min*32 + nowtime->tm_sec/2;
  root->date = (nowtime->tm_year-80)*512 + (nowtime->tm_mon+1)*32 + nowtime->tm_mday;
  root->first = 5;
  root->length = 2*sizeof(fcb);
  root->free = 1;

  root ++;

  now = time(NULL);
  nowtime = localtime(&now);
  strcpy(root->filename,"..");
  strcpy(root->exname,"");
  root->attribute = 0;
  root->time = nowtime->tm_hour*2048 + nowtime->tm_min*32 + nowtime->tm_sec/2;
  root->date = (nowtime->tm_year-80)*512 + (nowtime->tm_mon+1)*32 + nowtime->tm_mday;
  root->first = 5;
  root->length = 2*sizeof(fcb);
  root->free = 1;

  // fp = fopen("mysys","wb");
  // fwrite(myvhard,SIZE,1,fp);
  // fclose(fp);
  
  printf("my_format completed\n");
}

/*
**  目录管理函数
*/

//更改当前目录函数
void my_cd(char* dirname){
  //test: my_cd jc/jc
  char *dir;
  int fd;
  dir = strtok(dirname, "/");
  if(strcmp(dir, ".") == 0){
    return;
  }else if(strcmp(dir, "..") == 0){
    if(curdirID){
      curdirID = my_close(curdirID);
      ptrcurdir=&openfilelist[curdirID];
      printf("当前目录文件的fd为%d\n",curdirID);
    }
    return;
  }else if(strcmp(dir, "~") == 0){
    while(curdirID){
      curdirID = my_close(curdirID);
    }
    ptrcurdir=&openfilelist[0];
    dir = strtok(NULL, "/");
  }

  while(dir){
    // printf("%s\n",dir);
    fd = my_open(dir);
    if(fd != -1){
      ptrcurdir=&openfilelist[fd];
      curdirID = fd;
    }else{
      return;
    }
    dir = strtok(NULL, "/");
  }   
}

//创建子目录函数
void my_mkdir(char* dirname){
  fcb *fcbptr;
  fat *fat1, *fat2;
  time_t now;
  struct tm *nowtime;
  char text[MAXTEXT];
  unsigned short blkno;
  int rbn, fd, i;
  
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  fat2 = (fat *)(myvhard + 3*BLOCKSIZE);
  openfilelist[curdirID].count = 0;
  rbn = do_read(curdirID, openfilelist[curdirID].length, text);
  fcbptr = (fcb *)text;

  //在当前目录下创建?如果dirname[0] == '/'表示不是在当前目录下创建
  // 如 /xx/xx/xx
  if(dirname[0] == '/'){
    //分割最后一个目录名，用前面的路径检索用户文件打开表，如果已经打开则

  }else{
    //使用dirname检索当前目录文件
    for(i = 0; i < rbn/sizeof(fcb); i ++){
      //如果目录已存在，关闭父目录文件，出错返回
      if(strcmp(fcbptr->filename,dirname) == 0 && strcmp(fcbptr->exname, "")==0){
        printf("my_mkdir: 目录已存在\n");
        return;
      }
      fcbptr ++;
    }
    fcbptr = (fcb *)text;
    for(i = 0; i < rbn/sizeof(fcb); i ++){
      if(fcbptr->free == 0){
        break;
      }
      fcbptr ++;
    }
    blkno = findblock();
    if(blkno == -1){
      return;
    }
    (fat1+blkno)->id = END;
    (fat2+blkno)->id = END;
    now = time(NULL);
    nowtime = localtime(&now);
    strcpy(fcbptr->filename, dirname);
    strcpy(fcbptr->exname, "");
    fcbptr->attribute = 0;
    fcbptr->time = nowtime->tm_hour * 2048 + nowtime->tm_min * 32 + nowtime->tm_sec / 2;
    fcbptr->date = (nowtime->tm_year - 80) * 512 + (nowtime->tm_mon + 1) * 32 + nowtime->tm_mday;
    fcbptr->first = blkno;
    fcbptr->length = 2 * sizeof(fcb);
    fcbptr->free = 1;
    //把当前目录的文件读写指针定位到文件末尾
    openfilelist[curdirID].count = i * sizeof(fcb);
    //从指针fcbptr开始写一个fcb大小的内容到当前目录文件末尾
    do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2);

    //返回新建立的目录文件在用户打开文件数组的下标
    fd = my_open(dirname);
    if(fd == -1){
      return;
    }
    //建立新目录的'.','..'目录
    fcbptr = (fcb *)malloc(sizeof(fcb));
    now = time(NULL);
    nowtime = localtime(&now);
    strcpy(fcbptr->filename,".");
    strcpy(fcbptr->exname,"");
    fcbptr->attribute = 0;
    fcbptr->time = nowtime->tm_hour*2048 + nowtime->tm_min*32 + nowtime->tm_sec/2;
    fcbptr->date = (nowtime->tm_year-80)*512 + (nowtime->tm_mon+1)*32 + nowtime->tm_mday;
    fcbptr->first = blkno;
    fcbptr->length = 2*sizeof(fcb);
    fcbptr->free =1;
    do_write(fd, (char *)fcbptr, sizeof(fcb), 2);

    now = time(NULL);
    nowtime = localtime(&now);
    strcpy(fcbptr->filename,"..");
    strcpy(fcbptr->exname,"");
    fcbptr->attribute = 0;
    fcbptr->time = nowtime->tm_hour*2048 + nowtime->tm_min*32 + nowtime->tm_sec/2;
    fcbptr->date = (nowtime->tm_year-80)*512 + (nowtime->tm_mon+1)*32 + nowtime->tm_mday;
    fcbptr->first = blkno;
    fcbptr->length = 2*sizeof(fcb);
    fcbptr->free = 1;
    do_write(fd, (char *)fcbptr, sizeof(fcb), 2);
    
    free(fcbptr);
    my_close(fd);

    fcbptr = (fcb *)text;
    fcbptr->length = openfilelist[curdirID].length;
    openfilelist[curdirID].count = 0;
    do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2);
    openfilelist[curdirID].fcbstate = 1;
  }

}

//删除子目录函数
void my_rmdir(char* dirname){
  fcb *fcbptr1, *fcbptr2;
  fat *fat1, *fat2, *fatptr1, *fatptr2;
  char text1[MAXTEXT], text2[MAXTEXT];
  unsigned short blkno;
  int rbn1, rbn2, fd, i, j;
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  fat2 = (fat *)(myvhard + 3 * BLOCKSIZE);
  if(strcmp(dirname, ".") == 0 || strcmp(dirname, "..") == 0){
    printf("不能删除这个目录\n");
    return;
  }
  openfilelist[curdirID].count = 0;
  rbn1 = do_read(curdirID, openfilelist[curdirID].length, text1);
  fcbptr1 = (fcb *)text1;
  for(i = 0; i < rbn1/sizeof(fcb); i ++){
    if(strcmp(fcbptr1->filename, dirname) == 0 && strcmp(fcbptr1->exname, "") == 0){
      break;
    }
    fcbptr1 ++;
  }
  if(i == rbn1/sizeof(fcb)){
    printf("my_rmdir: 文件不存在\n");
    return;
  }
  fd = my_open(dirname);
  rbn2 = do_read(fd, openfilelist[fd].length, text2);
  fcbptr2 = (fcb *)text2;
  for(j = 0; j < rbn2/sizeof(fcb); j ++){
    if(strcmp(fcbptr2->filename, ".") && strcmp(fcbptr2->filename, "..") && strcmp(fcbptr2->filename, "")){
      my_close(fd);
      printf("my_rmdir: 该目录不为空\n");
      return;
    }
    fcbptr2 ++;
  }
  blkno = openfilelist[fd].first;
  while(blkno != END){
    fatptr1 = fat1 + blkno;
    fatptr2 = fat2 + blkno;
    blkno = fatptr1->id;
    fatptr1->id = FREE;
    fatptr2->id = FREE;
  }
  my_close(fd);
  strcpy(fcbptr1->filename, "");
  fcbptr1->free = 0;
  openfilelist[curdirID].count = i * sizeof(fcb);
  do_write(curdirID, (char *)fcbptr1, sizeof(fcb), 2);
  openfilelist[curdirID].fcbstate = 1;
}

//显示目录函数
void my_ls(){
  fcb *fcbptr;
  char text[MAXTEXT];
  int rbn, i;
  openfilelist[curdirID].count = 0;
  rbn = do_read(curdirID, openfilelist[curdirID].length, text);
  fcbptr = (fcb *)text;
  //从头开始遍历当前目录文件内容
  for(i = 0; i < rbn/sizeof(fcb); i ++){
    //按一定格式显示目录文件的内容
    if(fcbptr->free){
      if(fcbptr->attribute == 0){
        printf("%s\t<DIR>\t%d/%d/%d\t%02d:%02d:%02d\n",
                fcbptr->filename,
                (fcbptr->date>>9)+1980,
                (fcbptr->date>>5)&0x000f,
                fcbptr->date&0x001f,
                fcbptr->time>>11,
                (fcbptr->time>>5)&0x003f,
                fcbptr->time&0x001f*2
              );
      }else{
        printf("%s.%s\t%dB\t%d/%d/%d\t%02d:%02d:%02d\n",
                fcbptr->filename,
                fcbptr->exname,
                (int)fcbptr->length,
                (fcbptr->date>>9)+1980,
                (fcbptr->date>>5)&0x000f,
                fcbptr->date&0x001f,
                fcbptr->time>>11,
                (fcbptr->time>>5)&0x003f,
                fcbptr->time&0x001f*2
              );
      }
    }
    fcbptr ++;
  }
}

/*
**  文件管理函数
*/

//创建文件函数
int my_create(char* filename){
  fcb *fcbptr;
  fat *fat1, *fat2;
  char *fname, *exname, text[MAXTEXT];
  unsigned short blkno;
  int rbn, i;
  time_t now;
  struct tm *nowtime;
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  fat2 = (fat *)(myvhard + BLOCKSIZE);
  fname = strtok(filename, ".");
  exname = strtok(NULL, ".");
  if(strcmp(fname, "") == 0){
    printf("my_create: 文件名不符合格式\n");
    return -1;
  }
  if(!exname){
    printf("my_create: 必须输入要创建文件的拓展名\n");
    return;
  }
  //修改当前目录的读写指针
  openfilelist[curdirID].count = 0;
  //读入当前目录文件内容
  rbn = do_read(curdirID, openfilelist[curdirID].length, text);
  //检查要创建的文件是否重名
  fcbptr = (fcb *)text;
  for(i = 0; i < rbn/sizeof(fcb); i ++){
    if(strcmp(fcbptr->filename, fname) == 0 && strcmp(fcbptr->exname, exname) == 0){
      printf("my_create: 文件名已存在\n");
      return -1;
    }
    fcbptr ++;
  }
  //找到父目录未分配的目录项
  fcbptr = (fcb *)text;
  for(i = 0; i < rbn/sizeof(fcb); i ++){
    if(fcbptr->free == 0){
      break;
    }
    fcbptr ++;
  }
  //分配空闲磁盘块
  blkno = findblock();
  if(blkno == -1){
    printf("mycreate: 空闲磁盘块分配失败\n");
    return -1;
  }
  (fat1 + blkno)->id = END;
  (fat2 + blkno)->id = END;


  //创建FCB(在分配到的目录项中填写新创建文件的信息)
  now = time(NULL);
  nowtime = localtime(&now);
  strcpy(fcbptr->filename, fname);
  strcpy(fcbptr->exname, exname);
  fcbptr->attribute = 1;
  fcbptr->time = nowtime->tm_hour*2048 + nowtime->tm_min*32 + nowtime->tm_sec/2;
  fcbptr->date = (nowtime->tm_year-80)*512 + (nowtime->tm_mon+1)*32 + nowtime->tm_mday;
  fcbptr->first = blkno;
  fcbptr->length = 0;
  fcbptr->free = 1;
  //将FCB写入父目录中
  openfilelist[curdirID].count = i * sizeof(fcb);
  do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2);
  //修改父目录长度
  fcbptr = (fcb *)text;
  fcbptr->length = openfilelist[curdirID].length;
  openfilelist[curdirID].count = 0;
  do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2);
  openfilelist[curdirID].fcbstate = 1;
}

//删除文件函数
void my_rm(char* filename){
  fcb *fcbptr;
  fat *fat1, *fat2, *fatptr1, *fatptr2;
  char *fname, *exname, text[MAXTEXT];
  unsigned short blkno;
  int rbn, i;
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  fat2 = (fat *)(myvhard + 3 * BLOCKSIZE);
  fname = strtok(filename, ".");
  exname = strtok(NULL,".");
  if(strcmp(fname, "") == 0){
    printf("my_rm: 文件名不符合格式\n");
    return -1;
  }
  if(!exname){
    printf("my_rm: 必须输入要删除文件的拓展名\n");
    return;
  }
  //读入当前目录文件内容
  openfilelist[curdirID].count = 0;
  rbn = do_read(curdirID, openfilelist[curdirID].length, text);
  fcbptr = (fcb *)text;
  //找到要删除的文件
  for(i = 0; i < rbn/sizeof(fcb); i ++){
    if(strcmp(fcbptr->filename,fname) == 0 && strcmp(fcbptr->exname, exname) == 0){
      break;
    }
    fcbptr ++;
  }
  if(i == rbn/sizeof(fcb)){
    printf("my_rm: 文件不存在\n");
    return;
  }
  //如果要删除的文件已被打开，则调用my_close()关闭它
  for(int j = 0; j < MAXOPENFILE; j ++){
    if(strcmp(openfilelist[j].filename, fname)==0 & strcmp(openfilelist[j].exname,exname)==0 && j != curdirID){
      my_close(j);
    }
  }
  //回收磁盘块
  blkno = fcbptr->first;
  while(blkno != END){
    fatptr1 = fat1 + blkno;
    fatptr2 = fat2 + blkno;
    blkno = fatptr1->id;
    fatptr1->id = FREE;
    fatptr2->id = FREE;
  }

  //从当前目录中删除该文件的目录项
  strcpy(fcbptr->filename, "");
  fcbptr->free = 0;
  openfilelist[curdirID].count = i * sizeof(fcb);
  do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2);
  //修改父目录长度
  fcbptr = (fcb *)text;
  fcbptr->length = openfilelist[curdirID].length;
  openfilelist[curdirID].count = 0;
  do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2);
  openfilelist[curdirID].fcbstate = 1;
}

//打开文件函数
//如果是打开当前目录下的文件，filename形如"dir2/dir3/file"相对路径
//如果不是当前目录，filename形如"/dir1/dir2/dir3/file"绝对路径
int my_open(char* filename){
  // printf("test: 格式化前的filename为%s\n",filename);
  // //先对filename进行格式化
  // char newdir[80];
  // //如果传入的是绝对路径
  // if(filename[0] == '/'){
  //   strcpy(newdir, "~");
  // }else{
  //   strcpy(newdir, openfilelist[curdirID].dir);
  //   strcat(newdir, "/");
  // }
  // strcat(newdir, filename);
  // strcpy(filename, newdir);
  // printf("test: 格式化后的filename为%s\n",filename);
  // //使用格式化后的filename检索用户打开文件表

  // char dirs[80][80];
  // int count = spiltDir(dirs, filename);


  fcb *fcbptr;
  char *fname, exname[3], *str, text[MAXTEXT];
  int rbn, fd, i;

  //获取文件名和文件拓展名
  fname = strtok(filename, ".");
  str = strtok(NULL, ".");
  if(str){
    strcpy(exname, str);
  }else{
    strcpy(exname, "");
  }

  //检索用户打开文件表
  for(i = 0; i < MAXOPENFILE; i ++){
    //文件已打开，返回fd
    if(strcmp(openfilelist[i].filename, fname)==0 & strcmp(openfilelist[i].exname,exname)==0 && i != curdirID){
      printf("my_open: 文件已打开\n");
      return i;
    }
  }

  //检索当前目录文件
  openfilelist[curdirID].count = 0;
  rbn = do_read(curdirID, openfilelist[curdirID].length, text);
  fcbptr = (fcb *)text;
  //在当前目录下找要打开的文件是否存在
  for(i = 0; i < rbn/sizeof(fcb); i ++){
    if(strcmp(fcbptr->filename,fname)==0 && strcmp(fcbptr->exname, exname)==0){
      break;
    }
    fcbptr ++;
  }
  if(i == rbn/sizeof(fcb)){
    printf("my_open: 文件不存在\n");
    return -1;
  }
  //寻找空闲的用户打开文件表项
  fd = findopenfile();
  if(fd == -1){
    return -1;
  }
  //为文件建立用户打开文件表表项
  strcpy(openfilelist[fd].filename, fcbptr->filename);
  strcpy(openfilelist[fd].exname, fcbptr->exname);
  openfilelist[fd].attribute = fcbptr->attribute;
  openfilelist[fd].time = fcbptr->time;
  openfilelist[fd].date = fcbptr->date;
  openfilelist[fd].first = fcbptr->first;
  openfilelist[fd].length = fcbptr->length;
  openfilelist[fd].free = fcbptr->free;
  openfilelist[fd].dirno = openfilelist[curdirID].first;
  openfilelist[fd].diroff = i;
  strcpy(openfilelist[fd].dir, openfilelist[curdirID].dir);
  strcat(openfilelist[fd].dir, "/");
  strcat(openfilelist[fd].dir, filename);
  openfilelist[fd].father = curdirID;
  printf("打开文件的父目录的fd为%d\n",curdirID);
  openfilelist[fd].count = 0;
  openfilelist[fd].fcbstate = 0;
  openfilelist[fd].topenfile = 1;

  printf("打开文件的fd为%d\n",fd);
  return fd;
}

//关闭文件函数
int my_close(int fd){
  fcb *fcbptr;
  int father;
  father = openfilelist[fd].father;
  //检查fd有效性
  if(fd < 0 || fd >= MAXOPENFILE){
    printf("my_close: fd无效\n");
    return -1;
  }
  //fcbstate==1即文件的fcb内容有修改，需要调用do_write写回父目录文件
  if(openfilelist[fd].fcbstate){
    fcbptr = (fcb *)malloc(sizeof(fcb));
    strcpy(fcbptr->filename, openfilelist[fd].filename);
    strcpy(fcbptr->exname,openfilelist[fd].exname);
    fcbptr->attribute = openfilelist[fd].attribute;
    fcbptr->time = openfilelist[fd].time;
    fcbptr->date = openfilelist[fd].date;
    fcbptr->first = openfilelist[fd].first;
    fcbptr->length = openfilelist[fd].length;
    fcbptr->free = openfilelist[fd].free;
    openfilelist[father].count = openfilelist[fd].diroff * sizeof(fcb);
    do_write(father, (char *)fcbptr, sizeof(fcb), 2);
    free(fcbptr);
    openfilelist[fd].fcbstate = 0;
  }
  //回收该文件占据的用户打开文件表表项
  strcpy(openfilelist[fd].filename, "");
  strcpy(openfilelist[fd].exname, "");
  openfilelist[fd].topenfile = 0;

  printf("关闭文件的fd为%d\n",fd);
  printf("关闭文件的父目录的fd为%d\n",father);
  return father;
}

/*
**  文件读写函数
*/

//写文件函数
int my_write(int fd){
  fat *fat1,*fat2,*fatptr1,*fatptr2;
  int wstyle, len, k, tmp;
  char text[MAXTEXT];
  unsigned short blkno;
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  fat2 = (fat *)(myvhard + 3 * BLOCKSIZE);

  //检查fd的有效性
  if(fd<0 || fd>= MAXOPENFILE){
    printf("my_write: fd无效\n");
    return -1;
  }
  while(1){
    printf("请输入对应数字来选择写入方式:\n1.截断写\n2.覆盖写\n3.追加写\n");
    scanf("%d",&wstyle);
    if(wstyle >=1 && wstyle <=3){
      break;
    }
    printf("输入错误\n");
  }
  getchar();
  switch (wstyle){
    //截断写
    case 1:
      blkno = openfilelist[fd].first;
      fatptr1 = fat1 + blkno;
      fatptr2 = fat2 + blkno;
      blkno = fatptr1->id;
      fatptr1->id = END;
      fatptr2->id = END;
      while(blkno != END){
        fatptr1 = fat1 + blkno;
        fatptr2 = fat2 + blkno;
        blkno = fatptr1->id;
        fatptr1->id = FREE;
        fatptr2->id = FREE;
      }
      openfilelist[fd].count = 0;
      openfilelist[fd].length = 0;
      break;
    
    case 2:
      openfilelist[fd].count = 0;
      break;

    case 3:
      openfilelist[fd].count = openfilelist[fd].length;
      break;

    default:
      break;
  }
  k = 0;
  printf("请输入要写入的数据(输入Ctrl+Z结束):\n");
  while (gets(text)){
    if(strcmp(text,"q") == 0){
      break;
    }
    len = strlen(text);
    text[len++] = '\n';
    text[len] = '\0';
    tmp = do_write(fd, text, len, wstyle);
    if(tmp != -1){
      k += tmp;
    }
    if(tmp < len){
      printf("输入错误\n");
      break;
    }
  }
  //返回实际写入的字节数
  return k;
}

//实际写文件函数
int do_write(int fd, char *text, int len, char wstyle){
  fat *fat1, *fat2, *fatptr1, *fatptr2;
  unsigned char *buf, *blkptr;
  unsigned short blkno, blkoff;
  int i, k;
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  fat2 = (fat *)(myvhard + 3 * BLOCKSIZE);
  buf = (unsigned char *)malloc(BLOCKSIZE);
  if(buf == NULL){
    printf("do_write: 内存分配失败");
    return -1;
  }
  blkno = openfilelist[fd].first;
  blkoff = openfilelist[fd].count;
  fatptr1 = fat1 + blkno;
  fatptr2 = fat2 + blkno;
  while(blkoff >= BLOCKSIZE){
    blkno = fatptr1->id;
    if(blkno == END){
      blkno = findblock();
      if(blkno == -1){
        free(buf);
        return -1;
      }
      fatptr1->id = blkno;
      fatptr2->id = blkno;
      fatptr1 = fat1 + blkno;
      fatptr2 = fat2 + blkno;
      fatptr1->id = END;
      fatptr2->id = END;
    }else{
      fatptr1 = fat1 + blkno;
      fatptr2 = fat2 + blkno;
    }
    blkoff = blkoff - BLOCKSIZE;
  }
  k = 0;
  while(k < len){
    blkptr = (unsigned char *)(myvhard + blkno * BLOCKSIZE);
    for(i = 0; i < BLOCKSIZE; i ++){
      buf[i] = blkptr[i];
    }
    for(blkoff; blkoff < BLOCKSIZE; blkoff ++){
      buf[blkoff] = text[k++];
      openfilelist[fd].count ++;
      if(k == len){
        break;
      }
    }
    for (i = 0; i < BLOCKSIZE; i ++){
      blkptr[i] = buf[i];
    }
    if(k < len){
      blkno = fatptr1->id;
      if(blkno == END){
        blkno = findblock();
        if(blkno == -1){
          break;
        }
        fatptr1->id = blkno;
        fatptr2->id = blkno;
        fatptr1 = fat1 + blkno;
        fatptr2 = fat2 + blkno;
        fatptr1->id = END;
        fatptr2->id = END;
      }else{
        fatptr1 = fat1 + blkno;
        fatptr2 = fat2 + blkno;
      }
      blkoff = 0;
    }
  }
  if(openfilelist[fd].count > openfilelist[fd].length){
    openfilelist[fd].length = openfilelist[fd].count;
  }
  openfilelist[fd].fcbstate = 1;
  free(buf);
  return k;
}

//读文件函数
int my_read (int fd, int len){
  char text[MAXTEXT];
  int k;
  //fd有效？
  //否，出错返回
  if(fd < 0 || fd >= MAXOPENFILE){
    printf("my_read: fd无效\n");
    return -1;
  }
  openfilelist[fd].count = 0;
  //是,调用do_read()读出文件的len字节到text[]中
  k = do_read(fd,len,text);
  //do_read()返回值>0?
  if(k > 0){
    //是，将text[]中内容显示到终端
    printf("%s\n", text);
  }else{
    //否，出错返回
    printf("my_read: 读文件失败\n");
  }
  return k;
}

//实际读文件函数
int do_read (int fd, int len, char *text){
  fat *fat1, *fatptr;
  unsigned char *buf, *blkptr;
  unsigned short blkno, blkoff;
  int i, k;
  
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  //用malloc()申请缓冲区buf,大小与磁盘块相同
  buf = (unsigned char *)malloc(BLOCKSIZE);
  //分配成功？
  if(buf == NULL){
    //否
    //出错返回
    printf("do_read: 内存分配失败\n");
    return -1;
  }
  //是
  //将读写指针转化为逻辑快块号和块内偏移off
  blkno = openfilelist[fd].first;
  blkoff = openfilelist[fd].count; 
  if(blkoff >= openfilelist[fd].length){
    printf("do_read: 超出范围\n");
    free(buf);
    return -1;
  }
  //将逻辑块号转换成磁盘块号blkno，再转化成虚拟磁盘上的内存位置ptr
  fatptr = fat1 + blkno;
  //如果块内偏移量比磁盘块大小还要大，重新计算磁盘块号和fatptr
  while(blkoff >= BLOCKSIZE){
    blkno = fatptr->id;
    blkoff = blkoff - BLOCKSIZE;
    fatptr = fat1 + blkno;
  }
  k = 0;
  //k<len代表已读出的字节数还没有达到要求
  while(k < len){
    blkptr = (unsigned char *)(myvhard + blkno * BLOCKSIZE);
    //将ptr开始的一整块内容读入缓冲区buf
    for(i = 0; i < BLOCKSIZE; i ++){
      buf[i] = blkptr[i];
    }
    //从块内偏移量处开始读取文件内容
    for(blkoff; blkoff < BLOCKSIZE; blkoff ++){
      text[k++] = buf[blkoff];
      openfilelist[fd].count ++;
      if(k == len || openfilelist[fd].count == openfilelist[fd].length){
        break;
      }
    }
    //当前磁盘块的内容已读完，但是还没有达到要求字节数,继续读下一个磁盘块的内容
    if(k < len && openfilelist[fd].count != openfilelist[fd].length){
      blkno = fatptr->id;
      if(blkno == END){
        break;
      }
      blkoff = 0;
      fatptr = fat1 + blkno;
    }
  }
  text[k] = '\0';
  //释放buf，返回已读字节数
  free(buf);
  return k;

}

//寻找空闲的用户文件打开表项
int findopenfile(){
  int i;
  for(i = 0; i < MAXOPENFILE; i ++){
    if(openfilelist[i].topenfile == 0){
      return i;
    }
  }
  printf("findopenfile: 打开文件数已满\n");
  return -1;
}

unsigned short findblock(){
  unsigned short i;
  fat *fat1, *fatptr;
  fat1 = (fat *)(myvhard + BLOCKSIZE);
  for(i = 7; i < BLOCKNUM; i ++){
    fatptr = fat1 + i;
    if(fatptr->id == FREE){
      return i;
    }
  }
  printf("findblock: 找不到空闲磁盘块\n");
  return -1; 
}

//按'/'分割路径得到目录名数组
int spiltDir(char dirs[80][80], char *filename) {
  //得到起始和末尾下标（去掉首尾的'/'）
  int bg = 0; int ed = strlen(filename);
  if (filename[0] == '/') ++bg;
  if (filename[ed - 1] == '/') --ed;

  //假设filename 为 abc/def/gh
  //dirs[0][0] = a,dirs[0][1] = b,dirs[0][2] = c,dirs[0][3] = '\0'
  //dirs[1][0] = d,dirs[1][1] = e,dirs[1][2] = f,dirs[1][3] = '\0'
  //dirs[2][0] = g,dirs[2][1] = h,dirs[2][2] = '\0'
  //即将filename分解为多个目录名
  int ret = 0, tlen = 0;
  for (int i = bg; i < ed; ++i) {
    if (filename[i] == '/') {
      dirs[ret][tlen] = '\0';
      tlen = 0;
      ++ret;
    }
    else {
      dirs[ret][tlen++] = filename[i];
    }
  }
  dirs[ret][tlen] = '\0';

  //返回目录数
  return ret+1;
}