#include "defs.hpp"

#define PORT 1235

static const u_long windowSize = 8 * MSS;
static int nAddrLen;
static u_int base = 0;
static u_int nextSeqNum = base;

sockaddr_in addrSrv;

double MAX_TIME = CLOCKS_PER_SEC / 4;
static u_long cwnd = MSS;
static u_long ssthresh = 6 * MSS;
static int dupACKCount = 0;
static u_long window;
static u_long filelen;
static mutex mutexLock;

static clock_t start;
static bool stopTimer = true;

//RENO
static int RENO_STAGE = START_UP;

// new RENO
static bool fastResend = false;
static bool timeOutResend = false;

//sender缓冲�?
static u_long lastSendByte = 0, lastAckByte = 0;
static DataPackage sendPkts[50] {};

bool Connect(SOCKET& socket, SOCKADDR_IN& addr) {
  PackageHead h;
  h.flag |= SYN;
  h.seq = base;
  h.checkSum = CheckSum((u_short*) &h, sizeof(h));

  char* buffer = new char[sizeof(h)];
  memcpy(buffer, &h, sizeof(h));
  sendto(socket, buffer, sizeof(h), 0, (sockaddr*) &addr, nAddrLen);
  PrintPackge((DataPackage*) &h);
  cout << "[SYN_SEND]finish first handshake." << endl;

  clock_t start = clock();
  while (recvfrom(socket, buffer, sizeof(h), 0, (sockaddr*) &addr, &nAddrLen) <= 0) {
    if ((clock() - start) >= MAX_TIME) {
      memcpy(buffer, &h, sizeof(h));
      sendto(socket, buffer, sizeof(buffer), 0, (sockaddr*) &addr, nAddrLen);
      start = clock();
    }
  }

  memcpy(&h, buffer, sizeof(h));
  PrintPackge((DataPackage*) &h);
  if ((h.flag & ACK) && (h.flag & SYN) &&
    (CheckSum((u_short*) &h, sizeof(h)) == 0)) {
    cout << "[ACK_RECV]finish second handshake." << endl;
  } else return false;

  h.flag = 0;
  h.flag |= ACK;
  h.checkSum = 0;
  h.checkSum = (CheckSum((u_short*) &h, sizeof(h)));
  memcpy(buffer, &h, sizeof(h));
  sendto(socket, buffer, sizeof(h), 0, (sockaddr*) &addr, nAddrLen);
  PrintPackge((DataPackage*) &h);

  //wait 2 MAXTIME
  start = clock();
  while (true) {
    if (clock() - start > 2 * MAX_TIME) break;
    if (recvfrom(socket, buffer, sizeof(PackageHead), 0, (sockaddr*) &addr,
          &nAddrLen) <= 0)
      continue;  //丢失ACK
    memcpy(buffer, &h, sizeof(h));
    sendto(socket, buffer, sizeof(h), 0, (sockaddr*) &addr, nAddrLen);
    start = clock();
  }
  cout << "[ACK_SEND]finish third handshake." << endl;
  cout << "[WAIT]connected, wait to send package..." << endl;
  return true;
}

DataPackage creatPackage(u_int seq, char* data, int len) {
  DataPackage pkg;
  pkg.head.seq = seq;
  pkg.head.bufferSize = len;
  memcpy(pkg.data, data, len);
  pkg.head.checkSum = CheckSum((u_short*) &pkg, sizeof(DataPackage));

  return pkg;
}

u_int waitingNum(u_int nextSeq) {
  if (nextSeq >= base) return nextSeq - base;
  return nextSeq + MAX_SEQ - base;
}

DWORD WINAPI recvHandler(LPVOID param) {
    SOCKET *clientSock = (SOCKET *) param;
    char recvBuffer[sizeof(DataPackage)];
    DataPackage recvPacket;

    while (true) {
        if (recvfrom(*clientSock, recvBuffer, sizeof(DataPackage), 0, (SOCKADDR *) &addrSrv, &nAddrLen) > 0) {
            memcpy(&recvPacket, recvBuffer, sizeof(DataPackage));
            mutexLock.lock();
            if (CheckSum((u_short *) &recvPacket, sizeof(DataPackage)) == 0 && recvPacket.head.flag & ACK) {
                if (base < (recvPacket.head.ack + 1)) {
                    //收到新ACK
                    int d = recvPacket.head.ack + 1 - base;
                    for (int i = 0; i < d; i++) {
                        lastAckByte += sendPkts[i].head.bufferSize;
                    }
                    for (int i = 0; i < (int) waitingNum(nextSeqNum) - d; i++) {
                        sendPkts[i] = sendPkts[i + d];
                    }
                    string stageName;
                    switch (RENO_STAGE) {
                        case START_UP:
                            cwnd += d * MSS;
                            dupACKCount = 0;
                            if (cwnd >= ssthresh)
                                RENO_STAGE = AVOID;
                            break;
                        case AVOID:
                            cwnd += d * MSS * (MSS / cwnd);
                            dupACKCount = 0;
                            break;
                        case RECOVERY:
                            cwnd = ssthresh;
                            dupACKCount = 0;
                            RENO_STAGE = AVOID;
                            break;
                    }
                    window = min(cwnd, windowSize);
                    base = (recvPacket.head.ack + 1) % MAX_SEQ;
                    stageName = getRENOStageName(RENO_STAGE);
#ifdef OUTPUT_LOG
                    cout << "[" << stageName << "]cwnd:" << cwnd << "\twindow:" << window << "\tssthresh:" << ssthresh
                         << endl;
                    cout << "[lastACKByte:" << lastAckByte << "\tlastSendByte:" << lastSendByte
                         << "\tlastWritenByte:"
                         << lastAckByte + window << "]" << endl;
#endif
                } else {
                    //冗余 ACK
                    dupACKCount++;
                    if (RENO_STAGE == START_UP || RENO_STAGE == AVOID) {
                        if (dupACKCount == 3) {
                            ssthresh = cwnd / 2 + 3 * MSS;
                            cwnd = ssthresh;
                            RENO_STAGE = RECOVERY;
                            //retransmit missing segment
                            fastResend = true;
#ifdef OUTPUT_LOG
                            cout << "ACK repeat 3 times! begin fast recovery." << endl;
#endif
                        }
                    } else {
                        cwnd += MSS;
                    }

                    string stageName = getRENOStageName(RENO_STAGE);
#ifdef OUTPUT_LOG
                    cout << "[" << stageName << "]cwnd:" << cwnd << "\twindow:" << window << "\tssthresh:" << ssthresh
                         << endl;
                    cout << "[lastACKByte:" << lastAckByte << "\tlastSendByte:" << lastSendByte
                         << "\tlastWritenByte:"
                         << lastAckByte + window << "]" << endl;
#endif
                }

                if (base == nextSeqNum)
                    stopTimer = true;
                else {
                    start = clock();
                    stopTimer = false;
                }
            }
            mutexLock.unlock();
            if (lastAckByte == filelen)
                return 0;
        }
    }
}

DWORD WINAPI timeoutHandler(LPVOID param) {
    while (true) {
        if (lastAckByte == filelen)
            return 0;
        if (!stopTimer) {
            if (clock() - start > MAX_TIME) {
                timeOutResend = true;
#ifdef OUTPUT_LOG
                cout << "[time out!]Begin Resend" << endl;
#endif
            }
        }
    }
}

void sendHandler(u_long len, char *fileBuffer, SOCKET &socket, SOCKADDR_IN &addr) {

    int Datalen;

    char *data_buffer = new char[sizeof(DataPackage)], *pkt_buffer = new char[sizeof(DataPackage)];
    nextSeqNum = base;
    cout << "filelen: " << len << "Bytes" << endl;

    int sumPackets = 0, lossPackets = 0;
    auto nBeginTime = chrono::system_clock::now();
    auto nEndTime = nBeginTime;
    HANDLE recvhandler = CreateThread(nullptr, 0, recvHandler, LPVOID(&socket), 0, nullptr);
    HANDLE timeouthandler = CreateThread(nullptr, 0, timeoutHandler, nullptr, 0, nullptr);
    string stageName;
    while (true) {
        if (lastAckByte == len) {
            nEndTime = chrono::system_clock::now();
            auto duration = chrono::duration_cast<chrono::microseconds>(nEndTime - nBeginTime);
            double lossRate = double(lossPackets) / sumPackets;
            printf("System use %lf s, and the rate of loss packet is %lf\n",
                   double(duration.count()) * chrono::microseconds::period::num /
                   chrono::microseconds::period::den, lossRate);

            CloseHandle(recvhandler);
            CloseHandle(timeouthandler);
            PackageHead endPacket;
            endPacket.flag |= END;
            endPacket.checkSum = CheckSum((u_short *) &endPacket, sizeof(PackageHead));
            memcpy(pkt_buffer, &endPacket, sizeof(PackageHead));
            sendto(socket, pkt_buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, nAddrLen);

            while (recvfrom(socket, pkt_buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, &nAddrLen) <= 0) {
                if (clock() - start >= MAX_TIME) {
                    start = clock();
                    goto resend;
                }
            }

            if (((PackageHead *) (pkt_buffer))->flag & ACK &&
                CheckSum((u_short *) pkt_buffer, sizeof(PackageHead)) == 0) {
                cout << "finish file transfering." << endl;
                return;
            }

            resend:
            continue;
        }

        if (fastResend || timeOutResend)
            goto mysend;

        mutexLock.lock();
        window = min(cwnd, windowSize);
        if ((lastSendByte < lastAckByte + window) && (lastSendByte < len)) {
            sumPackets++;

            Datalen = min(lastAckByte + window - lastSendByte, MSS);
            Datalen = min(Datalen, len - lastSendByte);
            memcpy(data_buffer, fileBuffer + lastSendByte, Datalen);

            sendPkts[nextSeqNum - base] = creatPackage(nextSeqNum, data_buffer, Datalen);
            memcpy(pkt_buffer, &sendPkts[nextSeqNum - base], sizeof(DataPackage));

            sendto(socket, pkt_buffer, sizeof(DataPackage), 0, (SOCKADDR *) &addr, nAddrLen);

            if (base == nextSeqNum) {
                start = clock();
                stopTimer = false;
            }
            nextSeqNum = (nextSeqNum + 1) % MAX_SEQ;
            lastSendByte += Datalen;
        }
        mutexLock.unlock();
        continue;

        mysend:
        mutexLock.lock();
        for (int i = 0; i < nextSeqNum - base; i++) {
            memcpy(pkt_buffer, &sendPkts[i], sizeof(DataPackage));
            sendto(socket, pkt_buffer, sizeof(DataPackage), 0, (SOCKADDR *) &addr, nAddrLen);
        }
        if (timeOutResend) {
            ssthresh = cwnd / 2;
            cwnd = MSS;
            dupACKCount = 0;
            RENO_STAGE = START_UP;
        }
        timeOutResend = fastResend = false;
        mutexLock.unlock();
        start = clock();
        stopTimer = false;
    }
}

bool disConnect(SOCKET &socket, SOCKADDR_IN &addr) {

    int addrLen = sizeof(addr);
    char *buffer = new char[sizeof(PackageHead)];
    PackageHead closeHead;
    closeHead.flag |= FIN;
    closeHead.checkSum = CheckSum((u_short *) &closeHead, sizeof(PackageHead));
    memcpy(buffer, &closeHead, sizeof(PackageHead));

    PrintPackge((DataPackage *) &closeHead);
    if (sendto(socket, buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, addrLen) != SOCKET_ERROR)
        cout << "[FIN_SEND]finish first hand waving." << endl;
    else
        return false;

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
        cout << "[ACK_RECV]finish second hand waving." << endl;
    } else {
        return false;
    }

    u_long imode = 0;
    ioctlsocket(socket, FIONBIO, &imode);//阻�??
    recvfrom(socket, buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, &addrLen);
    memcpy(&closeHead, buffer, sizeof(PackageHead));
    PrintPackge((DataPackage *) buffer);
    if ((((PackageHead *) buffer)->flag & FIN) && (CheckSum((u_short *) buffer, sizeof(PackageHead) == 0))) {
        cout << "[FIN_RECV]finish third hand waving." << endl;
    } else {
        return false;
    }

    imode = 1;
    ioctlsocket(socket, FIONBIO, &imode);

    closeHead.flag = 0;
    closeHead.flag |= ACK;
    closeHead.checkSum = CheckSum((u_short *) &closeHead, sizeof(PackageHead));

    memcpy(buffer, &closeHead, sizeof(PackageHead));
    sendto(socket, buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, addrLen);
    PrintPackge((DataPackage *) &closeHead);
    start = clock();
    while (clock() - start <= 2 * MAX_TIME) {
        if (recvfrom(socket, buffer, sizeof(PackageHead), 0, (SOCKADDR *) &addr, &addrLen) <= 0)
            continue;
        //ACK lost
        memcpy(buffer, &closeHead, sizeof(PackageHead));
        sendto(socket, buffer, sizeof(PackageHead), 0, (sockaddr *) &addr, addrLen);
        start = clock();
    }

    cout << "[ACK_SEND]finish four-way wavehand." << endl;
    closesocket(socket);
    return true;
}

int main() {

  WSADATA WsaData;  //WSADATA变量
  int iResult; 
  iResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
  if (iResult != 0) {
    cout << "WSAStartup failed with error: " << iResult << endl;
    return -1;
  }

  SOCKET ClientSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (ClientSocket == INVALID_SOCKET) {
    cout << "Socket failed with error:" << WSAGetLastError() << endl;

    WSACleanup();
    return -1;
  }

  u_long imode = 1;
  ioctlsocket(ClientSocket, FIONBIO, &imode);  //非阻塞

  string ServerADDR;
  cout << "please input server ip:" << endl;
  cin >> ServerADDR;

  addrSrv.sin_family = AF_INET;
  addrSrv.sin_port = htons(PORT);
  addrSrv.sin_addr.S_un.S_addr = inet_addr(ServerADDR.c_str());

  nAddrLen = sizeof(ServerADDR);

  //三次握手
  if (!Connect(ClientSocket, addrSrv)) {
    cout << "[ERROR]connecting error." << endl;
    return 0;
  }

  cout << "please input filename:" << endl;
  string filename;
  cin >> filename;

  ifstream infile(filename, ifstream::binary);

  while (!infile.is_open()) {
    cout << "[ERROR]cannot open the file." << endl;
    cout << "please input filename:" << endl;
    cin >> filename;
  }

  infile.seekg(0, infile.end);
  filelen = infile.tellg();
  infile.seekg(0, infile.beg);
  cout << filelen << endl;

  char* filebuffer = new char[filelen];
  infile.read(filebuffer, filelen);
  infile.close();

  cout << "begin to transfer..." << endl;

  //传输数据
  sendHandler(filelen, filebuffer, ClientSocket, addrSrv);

  //四次挥手
  if (!disConnect(ClientSocket, addrSrv)) {
    cout << "[ERROR]disconnecting error." << endl;
    return 0;
  }

  cout << "[FINISHED]file transfered successfully." << endl;
  return 1;
}