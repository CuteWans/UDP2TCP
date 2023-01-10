#include "defs.hpp"

//检测校验和函数
bool checkSum(DataPackage package) {
  u_long sum = 0;
  for (int i = 0; i < CHECK_COUNT; ++i) {
    sum += package.useToCkeck.checkContent[i];
    if (sum & 0xffff0000) sum = (sum & 0xffff) + 1;
  }
  return (package.useToCkeck.checkSum == (u_short) ~(sum & 0xffff));
}

DWORD WINAPI ClientThread(LPVOID lpParameter) {
  //服务端UDP套接字
  SOCKET ClientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in RemoteAddr;
  RemoteAddr = *((sockaddr_in*) lpParameter);
  int nAddrLen = sizeof(RemoteAddr);

  //服务端端口绑定
  u_short newport = 1256;
  sockaddr_in ServerAddr;
  ServerAddr.sin_family = AF_INET;
  ServerAddr.sin_port = htons(newport);
  ServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;

  //尝试绑定新的端口
  while (bind(ClientSocket, (sockaddr*) &ServerAddr, sizeof(ServerAddr)) ==
    SOCKET_ERROR) {
    ++newport;
    ServerAddr.sin_port = htons(newport);
  }

  DataPackage dataPackage;
  SignPackage signPackage;

  int ret;
  clock_t start, end;
  //设置超时时间（非阻塞）
  timeval tv = {10, 0};
  setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*) &tv, sizeof(tv));

  //第二次握手
  signPackage.pac.lable = SECOND_HAND_SHAKE;
  signPackage.pac.serialNumber = MAX_WINDOW;
  sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
    (sockaddr*) &RemoteAddr, nAddrLen);  //第二次握手
  int testNum = 1;
  cout << "The second handshake has been sent." << endl;

  //第三次握手
  start = clock();

  for (;;) {
    end = clock();
    ret = recvfrom(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
      (sockaddr*) &RemoteAddr, &nAddrLen);
    if (signPackage.pac.lable == THIRD_HAND_SHAKE) break;
    if (testNum > MAX_TEST_NUM) {
      cout << "Transmission error." << endl;
      signPackage.pac.lable = SEND_ERROR;
      rep3(sendto(ClientSocket, signPackage.str, sizeof(signPackage), 0,
        (sockaddr*) &RemoteAddr, nAddrLen));
      return -1;
    }
    if ((end - start) > 20) {
      signPackage.pac.lable = SECOND_HAND_SHAKE;
      signPackage.pac.serialNumber = MAX_WINDOW;
      sendto(ClientSocket, signPackage.str, sizeof(signPackage), 0,
        (sockaddr*) &RemoteAddr, nAddrLen);  //第二次握手
      ++testNum;
      cout << "The second handshake has been retransmitted." << endl;

      //第三次握手
      start = clock();
    }
  }

  cout << "receive third handshake." << endl;
  //连接建立成功
  cout << "Client Connect Success!" << endl
       << "Client:   ip:" << inet_ntoa(RemoteAddr.sin_addr)
       << " ; port:" << ntohs(RemoteAddr.sin_port) << endl;

  //接收数据
  char file_name[MAX_NAME];
  ret = 0;
  testNum = 0;
  memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
  //接收文件名
  for (;;) {
    ret = recvfrom(ClientSocket, dataPackage.str, sizeof(dataPackage), 0,
      (sockaddr*) &RemoteAddr, &nAddrLen);
    if (ret != SOCKET_ERROR && dataPackage.pac.lable == FILE_NAME &&
      checkSum(dataPackage)) {
      strcpy(file_name, dataPackage.pac.data);
      file_name[dataPackage.pac.length] = '\0';
      cout << "Received file name:" << file_name << endl;
      cout << "Send confirmation message." << endl;
      signPackage.pac.lable = FILE_NAME;
      sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &RemoteAddr, nAddrLen);
      ++testNum;
      break;
    }
    if (/*testNum == MAX_TEST_NUM || */ dataPackage.pac.lable == SEND_ERROR) {
      cout << "Transmission error." << endl;
      signPackage.pac.lable = SEND_ERROR;
      rep3(sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &RemoteAddr, nAddrLen));
      return -1;
    }
    // Sleep(200);
  }
  //打开文件
  FILE* fp;
  if (!(fp = fopen(file_name, "wb"))) {
    cout << "File: " << file_name << " Can't Open" << endl;
    signPackage.pac.lable = SEND_ERROR;
    rep3(sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
      (sockaddr*) &RemoteAddr, nAddrLen));
    return -1;
  }

  //接收文件
  int count = 0;
  int serialNum = 1;
  testNum = 0;
  start = clock();
  for (;;) {
    memset(dataPackage.str, 0, sizeof(dataPackage.str));
    memset(signPackage.str, 0, sizeof(signPackage.str));
    ret = recvfrom(ClientSocket, dataPackage.str, sizeof(dataPackage.str), 0,
      (sockaddr*) &RemoteAddr, &nAddrLen);
    if (/*testNum > MAX_TEST_NUM || */ dataPackage.pac.lable == SEND_ERROR) {
      signPackage.pac.lable = SEND_ERROR;
      rep3(sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &RemoteAddr, nAddrLen));
      cout << "Transmission error." << endl;
      //cout << testNum;

      return -1;
    }
    if (dataPackage.pac.lable == SEND_DATA &&
      dataPackage.pac.serialNumber == serialNum && checkSum(dataPackage)) {
      DataPackage writeDataPackage = dataPackage;
      ret = fwrite(writeDataPackage.pac.data, sizeof(char),
        writeDataPackage.pac.length, fp);  //写文件
      serialNum = (serialNum == MAX_SERIAL_NUM) ?
        1 :
        (serialNum + 1);                        //更改期望收到的序列号
      if (ret < writeDataPackage.pac.length) {  //写入失败
        signPackage.pac.lable = SEND_ERROR;
        rep3(sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
          (sockaddr*) &RemoteAddr, nAddrLen));
        cout << file_name << "Write failed." << endl;
        return -1;
      }
    } else if (dataPackage.pac.lable == SEND_FINISH &&
      dataPackage.pac.serialNumber == serialNum &&
      checkSum(dataPackage)) {  //传输文件完成，返回确认消息
      fclose(fp);
      fp = NULL;
      signPackage.pac.lable = SEND_FINISH;
      rep3(sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &RemoteAddr, nAddrLen));
      cout << "File received successfully." << endl;
      // while (recvfrom(ClientSocket, dataPackage.str, sizeof(dataPackage), 0,
      //          (sockaddr*) &RemoteAddr, &nAddrLen) != SOCKET_ERROR)
      //   ;
      break;
    } else if (clock() - start > MAX_DELAY_TIME ||
      dataPackage.pac.serialNumber > serialNum ||
      serialNum - dataPackage.pac.serialNumber >
        MAX_WINDOW) {  //超时或失序发ack
      signPackage.pac.serialNumber = serialNum;
      signPackage.pac.lable = SEND_DATA;
      sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0,
        (sockaddr*) &RemoteAddr, nAddrLen);
      ++testNum;
      start = clock();
    }
    // cout << "serialNum:" << serialNum << endl;
  }
  return 0;
}

signed main(int argc, char* argv[]) {
  SOCKET ServerSocket;     //服务器套接字
  SOCKET ClientSocket;     //客服端套接字
  sockaddr_in ServerAddr;  //服务器地址
  sockaddr_in ClientAddr;  //客户端地址
  int iResult;             //返回的结果

  memset(&ServerAddr, 0, sizeof(ServerAddr));

  //初始化SOCKET DLL
  WSADATA WsaData;  //WSADATA变量
  iResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
  if (iResult != 0) {
    cout << "WSAStartup failed with error: " << iResult << endl;

    return -1;
  }

  //创建SOCKET
  ServerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (ServerSocket == INVALID_SOCKET) {
    cout << "Socket failed with error:" << WSAGetLastError() << endl;

    WSACleanup();
    return -1;
  }

  //服务器套接字地址
  ServerAddr.sin_family = AF_INET;
  ServerAddr.sin_port = htons(PORT);
  ServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;

  //绑定socket和服务器地址
  iResult = bind(ServerSocket, (sockaddr*) &ServerAddr, sizeof(ServerAddr));
  if (iResult == SOCKET_ERROR) {
    cout << "Bind Failed With Error:" << WSAGetLastError() << endl;

    closesocket(ServerSocket);  //关闭套接字
    WSACleanup();               //释放套接字资源;
    return -1;
  }

  //创建连接
  cout << "Server is started!\nWaiting For Connecting...\n" << endl;

  sockaddr_in RemoteAddr;
  int nAddrLen = sizeof(RemoteAddr);

  DataPackage dataPackage;
  SignPackage signPackage;

  for (;;) {
    recvfrom(ServerSocket, signPackage.str, sizeof(signPackage.str), 0,
      (sockaddr*) &RemoteAddr, &nAddrLen);
    if (signPackage.pac.lable == FIRST_HAND_SHAKE) {
      cout << "receive first handshake." << endl;
      HANDLE hThread =
        CreateThread(NULL, 0, ClientThread, &RemoteAddr, 0, NULL);
      CloseHandle(hThread);
    }
  }

  closesocket(ServerSocket);
  WSACleanup();
}