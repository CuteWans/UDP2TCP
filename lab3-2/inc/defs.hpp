#include <time.h>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <map>

#include <winsock2.h>

#include <windows.h>

using namespace std;

#define FIRST_HAND_SHAKE  1     //第一次握手
#define SECOND_HAND_SHAKE 2     //第二次握手
#define THIRD_HAND_SHAKE  3     //第三次握手
#define FILE_NAME         4     //文件名传输
#define SEND_DATA         5     //文件传输
#define SEND_ERROR        6     //传输出错
#define SEND_FINISH       7     //传输结束
#define MAX_WINDOW        32    //滑动窗口大小
#define MAX_SERIAL_NUM    512   //最大序列号
#define MAX_TEST_NUM      50    //最大重发次数
#define MAX_DELAY_TIME    200   //最大超时时间
#define PORT              1256  //端口号
#define MAX_BUFFER        1024  //DataPackage中数据段长度
#define MAX_NAME          1024  //文件名最大长度

#define rep1(_stmt_) (_stmt_);

#define rep2(_stmt_) \
  rep1(_stmt_);      \
  (_stmt_);
#define rep3(_stmt_) \
  rep2(_stmt_);      \
  (_stmt_);
#define rep4(_stmt_) \
  rep3(_stmt_);      \
  (_stmt_);

//发送文件数据的UDP包
union DataPackage {
  struct {
    u_short lable;          //标志位，储存部分上面的宏定义
    u_short serialNumber;   //序列号
    u_short length;         //数据段长度
    char data[MAX_BUFFER];  //数据
    u_short checkSum;       //校验和
  } __attribute__((packed)) pac;

  char str[4 * sizeof(u_short) + MAX_BUFFER];  //用于sendto和recvfrom函数

  struct {
    u_short checkContent[3 + MAX_BUFFER / (sizeof(u_short))];  //用于校验
    u_short checkSum;                                          //校验和
  } __attribute__((packed)) useToCkeck;
};

//用于三次握手以及ACK的数据包
union SignPackage {
  struct {
    char lable;
    int serialNumber;
  } __attribute__((packed)) pac;

  char str[5];
};

//用于校验和检验
#define CHECK_COUNT (sizeof(DataPackage) / sizeof(u_short) - 1)

#define init_rand(x)              \
  ({                              \
    if (x < 0) srand(time(NULL)); \
    else srand(x);                \
    rand();                       \
    rand();                       \
    rand();                       \
  })
