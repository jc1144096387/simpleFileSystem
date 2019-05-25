## 操作系统实验五：简单文件系统



### 已完成的工作
- 第一部分基本完成


### 工作进度
- 正在写my_open，进入do_read分析并编写
- 基本完成了my_open,my_read,do_read,正在写my_mkdir
- my_mkdir写了在当前目录下建立目录,通过路径来建立还没写
- my_ls显示格式还没完善，并且创建的根目录下的"."和".."有问题
- my_mkdir出现问题，大概是因为没有实现do_write(),现在开始实现do_write()和my_write()
- do_write()和my_write()还需要分析，另外其中的gets()之后应该优化为fgets()
- 打开文件和关闭文件时应该打印出对应的fd
- my_close()的返回值与在线平台有出入
- 当前目录的内容是否应该存在内存中？
- 实现了my_ls()，现在只剩下my_create()和my_rm()以及一些问题
- my_create()中是否需要将新建立的文件打开
- 基本完成了所有函数，但是有很多与在线平台不符合，以及一些错误，需要修改

### 剩余要做的事
- my_open(),my_create(),my_rm(),my_cd(),my_mkdir(),my_rmdir()，需要改为支持 输入路径来打开对应文件
- 全局变量ptrcurdir记录当前目录内容 需要加上去
- 打开文件和关闭文件时应该打印出对应的fd
- gets()应该优化为fgets()


### linux实现
#### cd
cd命令用来切换工作目录至dirname。 其中dirName表示法可为绝对路径或相对路径
cd    进入用户主目录；
cd ~  进入用户主目录；
cd -  返回进入此目录之前所在的目录；
cd ..  返回上级目录（若当前目录为“/“，则执行完后还在“/"；".."为上级目录的意思）；
cd ../..  返回上两级目录；
cd !$  把上个命令的参数作为cd参数使用。


### 借鉴代码分析
//将path/filename分别存储为path和filename
void splitLastDir(char *dir, char new_dir[2][DIRLEN]) {
  int len = strlen(dir);
  int flag = -1;
  //找到最后一个/的下标
  for (int i = 0; i < len; ++i) if (dir[i] == '/') flag = i;
  //如果不存在/则不能分割
  if (flag == -1) {
    SAYERROR;
    printf("splitLastDir: can\'t split %s\n", dir);
    return;
  }

  //设dir为path/filename,则执行以下代码后，new_dir[0]为path,new_dir[1]为filename
  int tlen = 0;
  for (int i = 0; i < flag; ++i) {
    new_dir[0][tlen++] = dir[i];
  }
  new_dir[0][tlen] = '\0';
  tlen = 0;
  for (int i = flag + 1; i < len; ++i) {
    new_dir[1][tlen++] = dir[i];
  }
  new_dir[1][tlen] = '\0';
}


int spiltDir(char dirs[DIRLEN][DIRLEN], char *filename) {
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

//调用do_read读入文件信息
int read_ls(int fd, unsigned char *text, int len) {
  int tcount = openfilelist[fd].count;
  openfilelist[fd].count = 0;
  int ret = do_read(fd, text, len);
  openfilelist[fd].count = tcount;
  return ret;
}

void getPos(int *id, int *offset, unsigned short first, int length) {
  int blockorder = length >> 10;
  *offset = length % 1024;
  *id = first;
  while (blockorder) {
    --blockorder;
    *id = fat1[*id].id;
  }
}


//file是新开的用户打开文件表表项
//getFcb(&file->open_fcb, &file->dirno, &file->diroff, -1, ".") 读入根目录的fcb
//getFcb(&file->open_fcb, &file->dirno, &file->diroff, fd, org_dir)  fd为要打开文件的父目录文件的fd org_dir要打开文件的文件名
int getFcb(fcb* fcbp, int *dirno, int *diroff, int fd, const char *dir) {
  //如果是根目录，将根目录区的内容复制即可
  if (fd == -1) {
    memcpy(fcbp, blockaddr[5], sizeof(fcb));
    *dirno = 5;
    *diroff = 0;
    return 1;
  }
  
  //在用户打开文件表中找到父目录文件
  useropen *file = &openfilelist[fd];

  // 从磁盘中读出当前目录的信息
  unsigned char *buf = (unsigned char *)malloc(SIZE);
  //将当前目录的目录项信息即当前目录下的文件fcb读入buf中
  int read_size = read_ls(fd, buf, file->open_fcb.length);
  if (read_size == -1) {
    SAYERROR;
    printf("getFcb: read_ls error\n");
    return -1;
  }
  fcb dirfcb;
  int flag = -1;
  //遍历buf中的文件fcb，如果有文件的文件名与要打开文件的文件名相同，则记录下标flag
  for (int i = 0; i < read_size; i += sizeof(fcb)) {
    memcpy(&dirfcb, buf + i, sizeof(fcb));
    if (dirfcb.free) continue;
    if (!strcmp(dirfcb.filename, dir)) {
      flag = i;
      break;
    }
  }

  free(buf);

  // 没有找到需要的文件
  if (flag == -1) return -1;

  // 找到的话就开始计算相关信息，改变对应打开文件项的值
  getPos(dirno, diroff, file->open_fcb.first, flag);
  memcpy(fcbp, &dirfcb, sizeof(fcb));

  return 1;
}

int getOpenlist(int fd, const char *org_dir) {
  // 把路径名处理成绝对路径
  char dir[DIRLEN];
  if (fd == -1) {
    strcpy(dir, "~/");
  }
  else {
    strcpy(dir, openfilelist[fd].dir);
    strcat(dir, org_dir);
  }

  // 如果有打开的目录和想打开的目录重名，必须把原目录的内容写回磁盘
  for (int i = 0; i < MAXOPENFILE; ++i) if (i != fd) {
    if (openfilelist[i].topenfile && !strcmp(openfilelist[i].dir, dir)) {
      my_save(i);
    }
  }

  int fileid = getFreeOpenlist();
  if (fileid == -1) {
    SAYERROR;
    printf("getOpenlist: openlist is full\n");
    return -1;
  }

  fcb dirfcb;
  useropen *file = &openfilelist[fileid];
  int ret;
  if (fd == -1) {
    ret = getFcb(&file->open_fcb, &file->dirno, &file->diroff, -1, ".");
  }
  else {
    ret = getFcb(&file->open_fcb, &file->dirno, &file->diroff, fd, org_dir);
  }
  strcpy(file->dir, dir);
  file->fcbstate = 0;
  file->topenfile = 1;

  //如果打开的是一个文件夹，就在路径后面加上'/'
  if (!file->open_fcb.attribute) {
    int len = strlen(file->dir);
    if (file->dir[len-1] != '/') strcat(file->dir, "/");
  }

  if (ret == -1) {
    file->topenfile = 0;
    return -1;
  }
  return fileid;
}

int my_open(char *filename) {
  //filename形式为~/xx/xxx
  //将传入的路径分为多个目录名，保存在数组中
  char dirs[DIRLEN][DIRLEN];
  int count = spiltDir(dirs, filename);

  char realdirs[DIRLEN][DIRLEN];
  int tot = 0;
  //因为filename形式为~/aa/bbb/ccc，所以dirs[0]为'~',因此从dirs[1]开始遍历
  for (int i = 1; i < count; ++i) {
    if (!strcmp(dirs[i], ".")) continue;
    if (!strcmp(dirs[i], "..")) {
      if (tot) --tot;
      continue;
    }
    strcpy(realdirs[tot++], dirs[i]);
  }
  //此时得到的realdirs[0]=aa,[1]= bbb,[2] = ccc

  // 生成根目录的副本
  int fd = getOpenlist(-1, "");

  // 利用当前目录的副本不断找到下一个目录
  //getOpenlist(fd, realdirs[0])得到~/aa目录文件的fd，
  //getOpenlist(fd, realdirs[1])得到~/aa/bbb目录文件的fd，
  //getOpenlist(fd, realdirs[2])得到~/aa/bbb/ccc目录文件的fd
  int flag = 0;
  for (int i = 0; i < tot; ++i) {
    int newfd = getOpenlist(fd, realdirs[i]);
    if (newfd == -1) {
      flag = 1;
      break;
    }
    my_close(fd);
    fd = newfd;
  }
  if (flag) {
    printf("my_open: %s no such file or directory\n", filename);
    openfilelist[fd].topenfile = 0;
    return -1;
  }

  if (openfilelist[fd].open_fcb.attribute) openfilelist[fd].count = 0;
  else openfilelist[fd].count = openfilelist[fd].open_fcb.length;
  return fd;
}

int my_touch(char *filename, int attribute, int *rpafd) {
  //filename形式为~/xx/xxx
  // 先打开file的上级目录，如果上级目录不存在就报错（至少自己电脑上的Ubuntu是这个逻辑）
  char split_dir[2][DIRLEN];
  //设filename为path/file，得到split_dir[0]为path,split_dir[1]为file
  splitLastDir(filename, split_dir);
  //先打开file的上级目录即path
  int pafd = my_open(split_dir[0]);
  if (!(0 <= pafd && pafd < MAXOPENFILE)) {
    SAYERROR;
    printf("my_creat: my_open error\n");
    return -1;
  }

  // 从磁盘中读出当前目录的信息，进行检查
  unsigned char *buf = (unsigned char*)malloc(SIZE);
  int read_size = read_ls(pafd, buf, openfilelist[pafd].open_fcb.length);
  if (read_size == -1) {
    SAYERROR;
    printf("my_touch: read_ls error\n");
    return -1;
  }
  fcb dirfcb;
  for (int i = 0; i < read_size; i += sizeof(fcb)) {
    memcpy(&dirfcb, buf + i, sizeof(fcb));
    if (dirfcb.free) continue;
    if (!strcmp(dirfcb.filename, split_dir[1])) {
      printf("%s is already exit\n", split_dir[1]);
      return -1;
    }
  }

  // 利用空闲磁盘块创建文件
  int fatid = getFreeFatid();
  if (fatid == -1) {
    SAYERROR;
    printf("my_touch: no free fat\n");
    return -1;
  }
  fat1[fatid].id = END;
  fcb_init(&dirfcb, split_dir[1], fatid, attribute);

  // 写入父亲目录内存
  memcpy(buf, &dirfcb, sizeof(fcb));
  int write_size = do_write(pafd, buf, sizeof(fcb));
  if (write_size == -1) {
    SAYERROR;
    printf("my_touch: do_write error\n");
    return -1;
  }
  openfilelist[pafd].count += write_size;

  // 创建自己的打开文件项
  int fd = getFreeOpenlist();
  if (!(0 <= fd && fd < MAXOPENFILE)) {
    SAYERROR;
    printf("my_touch: no free fat\n");
    return -1;
  }
  getPos(&openfilelist[fd].dirno, &openfilelist[fd].diroff, openfilelist[pafd].open_fcb.first, openfilelist[pafd].count - write_size);
  memcpy(&openfilelist[fd].open_fcb, &dirfcb, sizeof(fcb));
  if (attribute) openfilelist[fd].count = 0;
  else openfilelist[fd].count = openfilelist[fd].open_fcb.length;
  openfilelist[fd].fcbstate = 1;
  openfilelist[fd].topenfile = 1;
  strcpy(openfilelist[fd].dir, openfilelist[pafd].dir);
  strcat(openfilelist[fd].dir, split_dir[1]);

  free(buf);
  *rpafd = pafd;
  return fd;
}

int my_create(char *filename) {
  int pafd;
  //传入带根目录符号的绝对路径，如~/xx/xxx
  //传入pafd得到父目录的fd，返回的是创建的文件的fd
  int fd = my_touch(filename, 1, &pafd);
  if (!check_fd(fd)) return -1;
  my_close(pafd);
  return fd;
}

void my_mkdir(char *dirname) {
  int pafd;
  int fd = my_touch(dirname, 0, &pafd);
  if (!check_fd(fd)) return;
  unsigned char *buf = (unsigned char*)malloc(SIZE);

  // 把"."和".."装入自己的磁盘
  fcb dirfcb;
  memcpy(&dirfcb, &openfilelist[fd].open_fcb, sizeof(fcb));
  int fatid = dirfcb.first;
  strcpy(dirfcb.filename, ".");
  memcpy(blockaddr[fatid], &dirfcb, sizeof(fcb));
  memcpy(&dirfcb, &openfilelist[pafd].open_fcb, sizeof(fcb));
  strcpy(dirfcb.filename, "..");
  memcpy(blockaddr[fatid] + sizeof(fcb), &dirfcb, sizeof(fcb));

  my_close(pafd);
  my_close(fd);
  free(buf);
}

//调用命令之前对filename进行处理
//先检验传入的dir是否不合法（即是否存在连续'/'的情况），
//再判断传入的dir为绝对路径还是相对路径，并对其进行格式化，统一为'~/xxx/xxx'的形式即带根目录符号的绝对路径
int rewrite_dir(char *dir) {
  int len = strlen(dir);
  if (dir[len-1] == '/') --len;
  int pre = -1;
  for (int i = 0; i < len; ++i) if (dir[len] == '/') {
    if (pre != -1) {
      if (pre + 1 == i) {
        printf("rewrite_dir: %s is invaild, please check!\n", dir);
        return 0;
      }
    }
    pre = i;
  }
  //如果dir[0]为'/'则表示绝对路径，则在前面加上根目录符号'~'，即~/xxx
  //如果dir[0]不是'/'则表示相对路径，则在前面加上当前目录openfilelist[curdirid].dir
  char newdir[len];
  if (dir[0] == '/') {
    strcpy(newdir, "~");
  }
  else {
    strcpy(newdir, openfilelist[curdirid].dir);
  }
  strcat(newdir, dir);
  strcpy(dir, newdir);
  return 1;
}


### 函数调用关系
#### main()
- startsys()
- my_ls()
- ....
- my_exitsys()

#### startsys()
- my_format()

#### my_exitsys()
- my_close(i)

#### my_format()

#### my_cd(char* dirname)
- my_close(curdirID)???
- my_open(dir)

#### my_mkdir(char* dirname)
- do_read(curdirID, openfilelist[curdirID].length, text)
- findblock()
- do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2)
- my_open(dirname)
- my_close(fd)

#### my_rmdir(char* dirname)
- do_read(curdirID, openfilelist[curdirID].length, text1)
- my_open(dirname)
- my_close(fd)
- do_write(curdirID, (char *)fcbptr1, sizeof(fcb), 2)

#### my_ls()
- do_read(curdirID, openfilelist[curdirID].length, text)

#### my_create(char* filename)
- do_read(curdirID, openfilelist[curdirID].length, text)
- findblock()
- do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2)

#### my_rm(char* filename)
- do_read(curdirID, openfilelist[curdirID].length, text)
- do_write(curdirID, (char *)fcbptr, sizeof(fcb), 2)

#### my_open(char* filename)
- do_read(curdirID, openfilelist[curdirID].length, text)
- findopenfile()

#### my_close(int fd)
- do_write(father, (char *)fcbptr, sizeof(fcb), 2)

#### my_write(int fd)
- do_write(fd, text, len, wstyle)

#### do_write(int fd, char *text, int len, char wstyle)
- findblock()

#### my_read (int fd, int len)
- do_read(fd,len,text)

#### do_read (int fd, int len, char *text)


### 函数关系分析
my_open(),my_close(),do_read(),do_write()会被其他函数调用
其中my_cd,my_mkdir,my_rmdir,(my_create,my_rm)调用了my_open()