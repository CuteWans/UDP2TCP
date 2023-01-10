#include <cstring>
#include <string>
#include "defs.hpp"

//计算校验和函数
void checkSum(DataPackage* package) {
  u_long sum = 0;
  for (int i = 0; i < CHECK_COUNT; ++i) {
    sum += package->useToCkeck.checkContent[i];
    if (sum & 0xffff0000) sum = (sum & 0xffff) + 1;
  }
  package->useToCkeck.checkSum = (u_short) ~(sum & 0xffff);
}

void mysendto(SOCKET s, const char* buf, int len, int flags, const sockaddr* to,
  int tolen) {
  static int i = 0;
  ++i;
  if (i >= 500) {
    i = 0;
    return;
  }
  sendto(s, buf, len, flags, to, tolen);
}

DataPackage cacheBuffer[MAX_WINDOW];  //缓冲区

signed main(int argc, char* argv[]) {
  init_rand(-1);
  SOCKET sclient;  //客户端套接字
  int iResult;     //返回的结果

  //初始化SOCKET DLL
  WSADATA WsaData;
  iResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
  if (iResult != 0) {
    cout << "Init Windows Socket Failed:" << iResult << endl;
    return -1;
  }

  //创建SOCKET
  sclient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sclient == INVALID_SOCKET) {
    cout << "Create Socket Failed:" << WSAGetLastError() << endl;
    WSACleanup();
    return -1;
  }

  memset(cacheBuffer, 0x00, sizeof(cacheBuffer));

  char IP[16];
  cout << "Please enter the IP address:" << endl;
  cin.getline(IP, 16);

  sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_port = htons(PORT);
  sin.sin_addr.S_un.S_addr = inet_addr(IP);
  int len = sizeof(sin);

  DataPackage dataPackage;
  SignPackage signPackage;

  char SendBuffer[MAX_BUFFER] = {0};
  char RecvBuffer[MAX_BUFFER];
  int ret;

  clock_t start, end;

  //设置超时时间（非阻塞）
  timeval tv = {10, 0};
  setsockopt(sclient, SOL_SOCKET, SO_RCVTIMEO, (char*) &tv, sizeof(tv));

  //第一次握手

  signPackage.pac.lable = FIRST_HAND_SHAKE;
  signPackage.pac.serialNumber = MAX_WINDOW;
  mysendto(sclient, signPackage.str, sizeof(signPackage.str), 0,
    (sockaddr*) &sin, len);
  cout << "The first handshake has been sent." << endl;
  int testNum = 1;
  start = clock();
  //第二次握手

  for (;;) {
    end = clock();
    recvfrom(sclient, signPackage.str, sizeof(signPackage.str), 0,
      (sockaddr*) &sin, &len);
    if (signPackage.pac.lable == SECOND_HAND_SHAKE) {
      cout << "The second handshake has been received." << endl;
      break;
    }
    if (testNum > MAX_TEST_NUM) {
      cout << "Transmission error." << endl;
      signPackage.pac.lable = SEND_ERROR;
      rep3(mysendto(sclient, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &sin, len));

      return -1;
    }
    if ((end - start) > MAX_DELAY_TIME) {
      signPackage.pac.lable = FIRST_HAND_SHAKE;
      signPackage.pac.serialNumber = MAX_WINDOW;
      mysendto(sclient, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &sin, len);
      cout << "The first handshake has been retransmitted." << endl;
      ++testNum;
      start = clock();
    }
  }

  //第三次握手
  signPackage.pac.lable = THIRD_HAND_SHAKE;
  rep3(mysendto(sclient, signPackage.str, sizeof(signPackage.str), 0,
    (sockaddr*) &sin, len));
  cout << "The third handshake has been sent." << endl;

  cout << "Client OK!" << endl;

  cout << "---File Transfer---" << endl;

  FILE* fp;
  char file_name[MAX_PATH];

  cout << "Please Input The Filename:" << endl;
  cin.getline(file_name, MAX_PATH);
  //传送文件名
  if (!(fp = fopen(file_name, "rb"))) {
    cout << "File " << file_name << " Can't Open" << endl;
    return -1;
  }

  memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
  dataPackage.pac.lable = FILE_NAME;
  dataPackage.pac.length = strlen(file_name);
  memcpy(dataPackage.pac.data, file_name, sizeof(file_name));
  checkSum(&dataPackage);
  testNum = 0;  //记录失败次数

  mysendto(sclient, dataPackage.str, sizeof(dataPackage.str), 0,
    (sockaddr*) &sin, len);
  ++testNum;
  cout << "File name has been sent!" << endl;
  start = clock();

  for (;;) {
    ret = recvfrom(
      sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*) &sin, &len);
    if (/*testNum == MAX_TEST_NUM ||*/ signPackage.pac.lable == SEND_ERROR) {
      cout << "Transmission error." << endl;
      signPackage.pac.lable = SEND_ERROR;
      rep3(mysendto(sclient, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &sin, len));

      return -1;
    }
    if (ret != SOCKET_ERROR && signPackage.pac.lable == FILE_NAME) {
      cout << "server has confirmed to receive the file name." << endl;
      break;
    }
    if ((clock() - start) > MAX_DELAY_TIME) {
      rep3(mysendto(sclient, dataPackage.str, sizeof(dataPackage.str), 0,
        (sockaddr*) &sin, len));
      ++testNum;
      cout << "File name has been sent!" << endl;
      start = clock();
    }
  }

  //传送文件
  int serialNum = 1;             //序列号
  int lastACK = 1;               //上次收到的ACK
  int thisACK = MAX_WINDOW + 1;  //本次收到的ACK
  int board = MAX_WINDOW - 1;    //缓冲区界限
  int length;                    //数据长度
  bool isReadDone = 0;           //是否读完文件
  bool isCompleted = 0;          //文件是否发送完毕
  int lastSerialNum = -1;        //最后一个包的序列号

  map<int, int> helper;  //map保存序列号和缓冲区下标的映射关系
  helper[thisACK] = 0;
  helper[lastACK] = 0;
  for (;;) {  //在此收发
    memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
    testNum = 0;
    int nextPosition =
      helper[lastACK];  //下一次读出的数据包放到缓冲区哪一个位置
    int temp = (helper[thisACK] > helper[lastACK]) ?
      (helper[thisACK] - helper[lastACK]) :
      (helper[thisACK] + MAX_WINDOW - helper[lastACK]);
    for (int i = 0; !isReadDone && i < temp; ++i) {  //构建并缓存包
      memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
      length = fread(dataPackage.pac.data, sizeof(char),
        sizeof(dataPackage.pac.data), fp);  //读文件
      dataPackage.pac.length = length;
      dataPackage.pac.lable = SEND_DATA;
      dataPackage.pac.serialNumber = serialNum;
      helper[serialNum] = nextPosition;
      checkSum(&dataPackage);  //构建包
      if (ferror(fp)) {        //读文件出错
        dataPackage.pac.lable = SEND_ERROR;
        rep3(mysendto(sclient, dataPackage.str, sizeof(dataPackage.str), 0,
          (sockaddr*) &sin, len));
        cout << "Transmission error." << endl;

        return -1;
      }
      if (length == 0) {  //文件已读完
        isReadDone = 1;
        lastSerialNum = serialNum;
        dataPackage.pac.lable = SEND_FINISH;
        checkSum(&dataPackage);
        memcpy(cacheBuffer[nextPosition].str, dataPackage.str,
          sizeof(dataPackage.str));  //缓存包
        break;
      }
      memcpy(cacheBuffer[nextPosition].str, dataPackage.str,
        sizeof(dataPackage.str));  //缓存包
      serialNum =
        (serialNum == MAX_SERIAL_NUM) ? 1 : (serialNum + 1);  //更新序列号
      nextPosition = (nextPosition == (MAX_WINDOW - 1)) ?
        0 :
        (nextPosition + 1);  //更新缓冲区中下一次接收位置
    }

    int nextSend = helper[thisACK];
    for (int i = 0; i < MAX_WINDOW; ++i) {  //发送缓存区的包
      mysendto(sclient, cacheBuffer[nextSend].str, sizeof(DataPackage), 0,
        (sockaddr*) &sin, len);
      if (cacheBuffer[nextSend].pac.lable == SEND_FINISH) {  //最后一个包发送
        break;
      }
      nextSend = (nextSend == (MAX_WINDOW - 1)) ?
        0 :
        (nextSend + 1);  //缓冲区中下一次发送位置
    }
    start = clock();
    cout << "head: " << helper[thisACK] << endl;
    cout << "tail:" << helper[thisACK] + MAX_WINDOW - 1 << endl;
    for (;;) {  //接收
      memset(signPackage.str, 0, sizeof(signPackage.str));
      ret = recvfrom(sclient, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &sin, &len);
      if (ret > 0 && signPackage.pac.lable == SEND_DATA /*&&
          signPackage.pac.serialNumber == serialNum*/) {  //接收ack成功

        lastACK = thisACK;
        thisACK = signPackage.pac.serialNumber;
        if (signPackage.pac.serialNumber != serialNum) {
          //重发 GOBACKN
          cout << "Resend reached." << endl;
          int nextSend = helper[signPackage.pac.serialNumber];
          for (int i = 0; i < MAX_WINDOW; ++i) {  //发送缓存区的包
            mysendto(sclient, cacheBuffer[nextSend].str, sizeof(DataPackage), 0,
              (sockaddr*) &sin, len);
            if (cacheBuffer[nextSend].pac.lable ==
              SEND_FINISH) {  //最后一个包发送
              break;
            }
            nextSend = (nextSend == (MAX_WINDOW - 1)) ?
              0 :
              (nextSend + 1);  //缓冲区中下一次发送位置
          }
        }

        break;
      }
      if (lastSerialNum > 0 && ret > 0 &&
        signPackage.pac.lable == SEND_FINISH) {
        cout << "server has confirmed to receive the file." << endl;
        isCompleted = 1;
        break;
      }
      if (/*testNum > MAX_TEST_NUM || */ signPackage.pac.lable == SEND_ERROR) {
        cout << "Transmission error." << endl;
        dataPackage.pac.lable = SEND_ERROR;
        rep3(mysendto(sclient, dataPackage.str, sizeof(dataPackage.str), 0,
          (sockaddr*) &sin, len));
        // cout << testNum;

        return -1;
      }
      if (clock() - start > MAX_DELAY_TIME) {  //超时重传
        ++testNum;
        break;
      }
    }
    if (isCompleted) {  //文件传输完毕
      break;
    }
  }

  fclose(fp);
  fp = NULL;

  closesocket(sclient);
  WSACleanup();
}