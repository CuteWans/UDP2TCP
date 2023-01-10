#pragma once
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <winsock2.h>

#include <windows.h>

using namespace std;
using namespace std::chrono;

#define SYN         0x1
#define ACK         0x2
#define FIN         0x4
#define END         0x8
#define MSS         8192
#define MAX_SEQ     65535

#define min(a, b)     a > b ? b : a
#define max(a, b)     a < b ? b : a

#define OUTPUT_LOG

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

//UDP包头
struct PackageHead {
  u_int seq;
  u_int ack;
  u_short checkSum;  //校验和
  u_short bufferSize;
  u_char flag;
  u_char windows;

  PackageHead() {
    seq = 0;
    ack = 0;
    checkSum = 0;
    bufferSize = 0;
    flag = 0;
    windows = 0;
  }
};

struct DataPackage {
  PackageHead head;
  char data[MSS];
};

//校验和
static u_short CheckSum(u_short* packet, int len) {
  u_long sum = 0;
  int count = (len + 1) / 2;

  u_short* buf = new u_short[count]();
  memcpy(buf, packet, len);

  while (count--) {
    sum += *buf++;
    if (sum & 0xFFFF0000) sum = (sum & 0xFFFF) + 1;
  }
  return ~(sum & 0xFFFF);
}

#define init_rand(x)              \
  ({                              \
    if (x < 0) srand(time(NULL)); \
    else srand(x);                \
    rand();                       \
    rand();                       \
    rand();                       \
  })

static void PrintPackge(DataPackage* pkg) {
#ifdef OUTPUT_LOG
  printf("[SYN:%d\tACK:%d\tFIN:%d\tEND:%d]SEQ:%d\tACK:%d\tLEN:%d\n",
    pkg->head.flag & 0x1, pkg->head.flag >> 1 & 0x1, pkg->head.flag >> 2 & 0x1,
    pkg->head.flag >> 3 & 0x1, pkg->head.seq, pkg->head.ack,
    pkg->head.bufferSize);
#endif
}

enum { START_UP, AVOID, RECOVERY };

static string getRENOStageName(int stage) {
  switch (stage) {
    case START_UP : return "START_UP";
    case AVOID : return "AVOID";
    case RECOVERY : return "RECOVERY";
  }
  return "";
}
