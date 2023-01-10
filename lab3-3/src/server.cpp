#include "defs.hpp"

#define MAX_FILE_SIZE 100000000

char fileBuffer[MAX_FILE_SIZE];
static u_int base_stage = 0;
double MAX_TIME = CLOCKS_PER_SEC;
double MAX_WAIT_TIME = MAX_TIME / 4;
#define PORT 1234
static int windowSize = 16;

bool Connect(SOCKET& socket, SOCKADDR_IN& addr) {
  int nAddrLen = sizeof(addr);
  char* buffer = new char[sizeof(PackageHead)];
  recvfrom(socket, buffer, sizeof(PackageHead), 0, (sockaddr*) &addr, &nAddrLen);

  PrintPackge((DataPackage*) buffer);

  if ((((PackageHead*) buffer)->flag & SYN) &&
    (CheckSum((u_short*) buffer, sizeof(PackageHead)) == 0))
    cout << "[SYN_RECV]finish first handshake." << endl;
  else return false;

  base_stage = ((PackageHead*) buffer)->seq;

  PackageHead h;
  h.flag |= ACK;
  h.flag |= SYN;
  h.windows = windowSize;
  h.checkSum = CheckSum((u_short*) &h, sizeof(PackageHead));
  memcpy(buffer, &h, sizeof(PackageHead));
  if (sendto(socket, buffer, sizeof(PackageHead), 0, (sockaddr*) &addr, nAddrLen) == -1)
    return 0;

  PrintPackge((DataPackage*) buffer);
  cout << "[SYN_ACK_SEND]finish second handshake." << endl;

  u_long imode = 1;
  clock_t start, end;
  ioctlsocket(socket, FIONBIO, &imode);  //非阻塞

  start = clock();
  while (recvfrom(socket, buffer, sizeof(h), 0, (sockaddr*) &addr, &nAddrLen) <= 0) {
    end = clock();
    if ((end - start) >= MAX_TIME) {
      sendto(socket, buffer, sizeof(h), 0, (sockaddr*) &addr,
        nAddrLen); 
      cout << "[RETRANS]The second handshake has been retransmitted." << endl;
      start = clock();
    }
  }

  PrintPackge((DataPackage*) buffer);

  if ((((PackageHead*) buffer)->flag & ACK) &&
    (CheckSum((u_short*) buffer, sizeof(PackageHead)) == 0)) {
    cout << "[ACK_RECV]finish third handshake." << endl;
  } else return false;

  imode = 0;
  ioctlsocket(socket, FIONBIO, &imode);  //阻塞
  cout << "[CONNECTED]successfully connected, ready to receive file..." << endl;
  return 1;
}

DataPackage creatPackage(u_int ack) {
  DataPackage pkg;
  pkg.head.ack = ack;
  pkg.head.flag |= ACK;
  pkg.head.checkSum = CheckSum((u_short*) &pkg, sizeof(DataPackage));

  return pkg;
}

u_long recvHandler(char *fileBuffer, SOCKET &socket, SOCKADDR_IN &addr) {
    u_long fileLen = 0;
    int addrLen = sizeof(addr);
    u_int expectedSeq = base_stage;
    int dataLen;

    char *pkt_buffer = new char[sizeof(DataPackage)];
    DataPackage recvPkt, sendPkt = creatPackage(base_stage - 1);
    clock_t start;
    bool clockStart = false;

    while (true) {
        memset(pkt_buffer, 0, sizeof(DataPackage));
        recvfrom(socket, pkt_buffer, sizeof(DataPackage), 0, (SOCKADDR *) &addr, &addrLen);
        memcpy(&recvPkt, pkt_buffer, sizeof(DataPackage));

        if (recvPkt.head.flag & END && CheckSum((u_short *) &recvPkt, sizeof(PackageHead)) == 0) {
            cout << "finish transfering." << endl;
            PackageHead endPacket;
            endPacket.flag |= ACK;
            endPacket.checkSum = CheckSum((u_short *) &endPacket, sizeof(PackageHead));
            memcpy(pkt_buffer, &endPacket, sizeof(PackageHead));
            sendto(socket, pkt_buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, addrLen);
            return fileLen;
        }

        if (recvPkt.head.seq == expectedSeq && CheckSum((u_short *) &recvPkt, sizeof(DataPackage)) == 0) {
            //correctly receive the expected seq
            dataLen = recvPkt.head.bufferSize;
            memcpy(fileBuffer + fileLen, recvPkt.data, dataLen);
            fileLen += dataLen;

            //give back ack=seq
            sendPkt = creatPackage(expectedSeq);
            expectedSeq = (expectedSeq + 1) % MAX_SEQ;

            memcpy(pkt_buffer, &sendPkt, sizeof(DataPackage));
            sendto(socket, pkt_buffer, sizeof(DataPackage), 0, (SOCKADDR *) &addr, addrLen);
            continue;
        }

        cout << "[SEQ]wait head:" << expectedSeq << endl;
        cout << "[SEQ]recv head:" << recvPkt.head.seq << endl;
        memcpy(pkt_buffer, &sendPkt, sizeof(DataPackage));
        sendto(socket, pkt_buffer, sizeof(DataPackage), 0, (SOCKADDR *) &addr, addrLen);
    }
}

bool disConnect(SOCKET &socket, SOCKADDR_IN &addr) {
    int addrLen = sizeof(addr);
    char *buffer = new char[sizeof(PackageHead)];

    recvfrom(socket, buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, &addrLen);

    PrintPackge((DataPackage *) buffer);

    if ((((PackageHead *) buffer)->flag & FIN) && (CheckSum((u_short *) buffer, sizeof(PackageHead) == 0))) {
        cout << "[FIN_RECV]finish first hand waving." << endl;
    } else {
        return false;
    }

    PackageHead closeHead;
    closeHead.flag = 0;
    closeHead.flag |= ACK;
    closeHead.checkSum = CheckSum((u_short *) &closeHead, sizeof(PackageHead));
    memcpy(buffer, &closeHead, sizeof(PackageHead));
    sendto(socket, buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, addrLen);

    PrintPackge((DataPackage *) buffer);
    cout << "[ACK_SEND]finish second hand waving." << endl;

    closeHead.flag = 0;
    closeHead.flag |= FIN;
    closeHead.checkSum = CheckSum((u_short *) &closeHead, sizeof(PackageHead));
    memcpy(buffer, &closeHead, sizeof(PackageHead));
    sendto(socket, buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, addrLen);

    PrintPackge((DataPackage *) buffer);
    cout << "[FIN_SEND]finish third hand waving." << endl;

    u_long imode = 1;
    ioctlsocket(socket, FIONBIO, &imode);
    clock_t start = clock();
    while (recvfrom(socket, buffer, sizeof(PackageHead), 0, (sockaddr *) &addr, &addrLen) <= 0) {
        if (clock() - start >= MAX_TIME) {
            memcpy(buffer, &closeHead, sizeof(PackageHead));
            sendto(socket, buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, addrLen);
            start = clock();
        }
    }

    PrintPackge((DataPackage *) buffer);
    if ((((PackageHead *) buffer)->flag & ACK) && (CheckSum((u_short *) buffer, sizeof(PackageHead) == 0))) {
        cout << "[ACK_RECV]finish four-way wavehand." << endl;
    } else {
        return false;
    }
    closesocket(socket);
    return true;
}

int main() {
  sockaddr_in ServerAddr;  //服务器地址
  sockaddr_in ClientAddr;  

  WSADATA WsaData;  //WSADATA变量
  int iResult;     
  iResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
  if (iResult != 0) {
    cout << "WSAStartup failed with error: " << iResult << endl;
    return -1;
  }

  SOCKET ServerSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (ServerSocket == INVALID_SOCKET) {
    cout << "Socket failed with error:" << WSAGetLastError() << endl;

    WSACleanup();
    return -1;
  }

  ServerAddr.sin_family = AF_INET;
  ServerAddr.sin_port = htons(PORT);
  ServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;
  //绑定socket和服务器地址
  iResult = bind(ServerSocket, (sockaddr*) &ServerAddr, sizeof(ServerAddr));
  if (iResult == SOCKET_ERROR) {
    cout << "Bind Failed With Error:" << WSAGetLastError() << endl;

    closesocket(ServerSocket); 
    WSACleanup();     
    return -1;
  }

  if (!Connect(ServerSocket, ClientAddr)) {
    cout << "[ERROR]connecting error." << endl;
    return 0;
  }

  u_long fileLen = recvHandler(fileBuffer, ServerSocket, ClientAddr);

  if (!disConnect(ServerSocket, ClientAddr)) {
    cout << "[ERROR]disconnecting error." << endl;
    return 0;
  }

  //写入复制文件
  string filename = "recv.jpg";
  ofstream outfile(filename, ios::binary);
  if (!outfile.is_open()) {
    cout << "[ERROR]opening file error." << endl;
    return 0;
  }
  cout << "File Length: " << fileLen << endl;
  outfile.write(fileBuffer, fileLen);
  outfile.close();

  cout << "[FINISHED]writing successfully." << endl;
  return 1;
}