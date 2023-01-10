#include <iostream>
#include <cstdio>  
#include <winsock2.h> 
#include <windows.h>
#include <time.h>
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma warning(disable:4996)
using namespace std;
#pragma comment(lib, "ws2_32.lib")

#define FIRST_HAND_SHAKE 1//第一次握手
#define SECOND_HAND_SHAKE 2//第二次握手
#define THIRD_HAND_SHAKE 3//第三次握手
#define FILE_NAME 4//文件名传输
#define SEND_DATA 5//文件传输
#define SEND_ERROR 6//传输出错
#define SEND_FINISH 7//传输结束
#define MAX_TEST_NUM 50//最大重发次数
#define MAX_DELAY_TIME 200//最大超时时间
#define PORT 1234
#define MAX_BUFFER 1024
#define MAX_NAME 1024

//用于三次握手以及ACK的数据包
struct SignPackage {
	union {
		struct {
			char lable;
			int serialNumber;
		} pac;
		char str[5];
	};
};

//发送文件数据的UDP包
struct DataPackage {
	union {
		struct {
			u_short lable;//标志位，储存部分上面的宏定义
			u_short serialNumber;//序列号
			u_short length;//数据段长度
			char data[MAX_BUFFER];//数据
			u_short checkSum;//校验和
		} pac;
		char str[4 * sizeof(u_short) + MAX_BUFFER];//用于sendto和recvfrom函数
		struct {
			u_short checkContent[3 + MAX_BUFFER / (sizeof(u_short))];//用于校验
			u_short checkSum;
		} useToCheck;
	};
};

#define CHECK_COUNT (sizeof(DataPackage) / 2 - 1)//用于校验和检验

//检测校验和函数
bool checkSum(DataPackage package) {
	register u_long sum = 0;
	for (int i = 0; i < CHECK_COUNT; i++) {
		sum += package.useToCheck.checkContent[i];
		if (sum & 0XFFFF0000) {
			sum &= 0XFFFF;
			sum += 1;
		}
	}
	return (package.useToCheck.checkSum == (u_short)~(sum & 0XFFFF));
}

DWORD WINAPI ClientThread(LPVOID lpParameter) {
	//服务端UDP套接字
	SOCKET ClientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	sockaddr_in RemoteAddr;
	RemoteAddr = *((sockaddr_in*)lpParameter);

	int nAddrLen = sizeof(RemoteAddr);

	//服务端端口绑定
	u_short newport = 1234;
	sockaddr_in ServerAddr;
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(newport);
	ServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;

	//尝试绑定新的端口
	while (bind(ClientSocket, (sockaddr*)& ServerAddr, sizeof(ServerAddr)) == SOCKET_ERROR) {
		newport ++;
		ServerAddr.sin_port = htons(newport);
	}

	DataPackage dataPackage;
	SignPackage signPackage;

	char SendBuffer[MAX_BUFFER] = {0};
	char RecvBuffer[MAX_BUFFER];
	int ret;
	clock_t start, end;
	timeval tv = {5, 0};
	setsockopt(ClientSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)& tv, sizeof(tv));

	//第二次握手
	signPackage.pac.lable = SECOND_HAND_SHAKE;
	sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
	int testNum = 1;
	cout << "The second handshake has been sent." << endl;

	//第三次握手
	start = clock();

	while (true) {
		end = clock();
		ret = recvfrom(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, &nAddrLen);
		if (signPackage.pac.lable == THIRD_HAND_SHAKE)
			break;
		if (testNum > MAX_TEST_NUM) {
			cout << "Transmission error, press any key to exit." << endl;
			signPackage.pac.lable = SEND_ERROR;
			sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
			sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
			sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
			system("pause");
			return -1;
		}
		if ((end - start) > 20) {
			signPackage.pac.lable = SECOND_HAND_SHAKE;
			signPackage.pac.serialNumber = 0;
			sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);//第二次握手重发
			cout << "The second handshake has been retransmitted." << endl;
			testNum++;
			start = clock();
		}
	}

	cout << "receive third handshake." << endl;

	//连接建立成功
	cout << "Client Connect Success!\n" << "Client:   Ip:" << inet_ntoa(RemoteAddr.sin_addr) << " ; Port:" << ntohs(RemoteAddr.sin_port) << endl;

	//接收数据
	while (true)
	{
		char file_name[MAX_NAME];
		int ret = 0;
		int testNum = 0;
		memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
		//接收文件名
		while (true) {
			ret = recvfrom(ClientSocket, dataPackage.str, sizeof(dataPackage.str), 0, (sockaddr*)& RemoteAddr, &nAddrLen);
			if (ret != SOCKET_ERROR && dataPackage.pac.lable == FILE_NAME && checkSum(dataPackage)) {
				strcpy(file_name, dataPackage.pac.data);
				file_name[dataPackage.pac.length] = '\0';
				cout << "Received file name:" << file_name << endl;
				cout << "Send confirmation message.\n";
				signPackage.pac.lable = FILE_NAME;
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				testNum ++;
				break;
			}
			if (testNum == 100 || dataPackage.pac.lable == SEND_ERROR) {
				cout << "Transmission error, press any key to exit." << endl;
				signPackage.pac.lable = SEND_ERROR;
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				system("pause");
				return -1;
			}
			Sleep(200);
		}
		//打开文件
		FILE* fp;
		if (!(fp = fopen(file_name, "wb")))
		{
			cout << "File: " << file_name << " Can't Open" << endl;
			signPackage.pac.lable = SEND_ERROR;
			sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
			sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
			sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
			return -1;
		}
		memset(RecvBuffer, 0, MAX_BUFFER);
		//接收文件
		int ret1;
		int ret2;
		memset(dataPackage.str, 0, sizeof(dataPackage.str));
		testNum = 0;
		signPackage.pac.serialNumber = 0;
		signPackage.pac.lable = SEND_DATA;
		while (true)
		{
			ret1 = recvfrom(ClientSocket, dataPackage.str, sizeof(dataPackage.str), 0, (sockaddr*)& RemoteAddr, &nAddrLen);
			if (testNum == 100 || dataPackage.pac.lable == SEND_ERROR) {//传送出错
				cout << "Transmission error, press any key to exit." << endl;
				signPackage.pac.lable = SEND_ERROR;
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				system("pause");
				return -1;
			}
			if (ret1 != SOCKET_ERROR && dataPackage.pac.lable == SEND_FINISH) {//传送完毕
				signPackage.pac.lable = SEND_FINISH;
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				cout << "Receive: " << file_name << " Successful!" << endl;
				cout << "File received successfully.\n";
				break;
			}
			if (ret1 == SOCKET_ERROR || dataPackage.pac.serialNumber != signPackage.pac.serialNumber || !checkSum(dataPackage)) {//传输出问题
				ret2 = sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				testNum++;
				Sleep(200);
				continue;
			}
			while (dataPackage.pac.lable == SEND_DATA) {
				ret2 = sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
				ret = fwrite(dataPackage.pac.data, sizeof(char), dataPackage.pac.length, fp);//写数据
				signPackage.pac.serialNumber = signPackage.pac.serialNumber ? 0 : 1;
				if (ret < dataPackage.pac.length) {
					cout << file_name << "Write failed, press any key to exit." << endl;
					signPackage.pac.lable = SEND_ERROR;
					sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
					sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
					sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
					system("pause");
					return -1;
				}
				if (ret2 != SOCKET_ERROR) {
					break;
				}
				if (testNum ++ == 100) {
					cout << "Transmission error, press any key to exit." << endl;
					signPackage.pac.lable = SEND_ERROR;
					sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
					sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
					sendto(ClientSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, nAddrLen);
					system("pause");
					return -1;
				}
			}
			memset(dataPackage.str, 0, sizeof(dataPackage.str));
		}
	}
	return 0;
}

int main(int argc, char* argv[])
{

	SOCKET ServerSocket;//服务器套接字
	SOCKET ClientSocket;//客服端套接字
	sockaddr_in ServerAddr;//服务器地址
	sockaddr_in ClientAddr;//客户端地址
	int iResult;//返回的结果

	memset(&ServerAddr, 0, sizeof(ServerAddr));

	//初始化SOCKET DLL
	WSADATA WsaData;//WSADATA变量
	iResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
	if (iResult != 0)
	{
		cout << "WSAStartup failed with error: " << iResult << endl;
		system("pause");
		return -1;
	}

	//创建SOCKET
	ServerSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (ServerSocket == INVALID_SOCKET)
	{
		cout << "Socket failed with error:" << WSAGetLastError() << endl;
		system("pause");
		WSACleanup();
		return -1;
	}

	//服务器套接字地址
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_port = htons(PORT);
	ServerAddr.sin_addr.S_un.S_addr = INADDR_ANY;

	//绑定socket和服务器地址
	iResult = bind(ServerSocket, (sockaddr*)& ServerAddr, sizeof(ServerAddr));
	if (iResult == SOCKET_ERROR)
	{
		cout << "Bind Failed With Error:" << WSAGetLastError() << endl;
		system("pause");
		closesocket(ServerSocket);//关闭套接字  
		WSACleanup();//释放套接字资源; 
		return -1;
	}

	//创建连接
	cout << "Server is started!\nWaiting For Connecting...\n" << endl;

	sockaddr_in RemoteAddr;
	int nAddrLen = sizeof(RemoteAddr);

	DataPackage dataPackage;
	SignPackage signPackage;

	while (true)
	{
		recvfrom(ServerSocket, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& RemoteAddr, &nAddrLen);
		if (signPackage.pac.lable == FIRST_HAND_SHAKE)
		{
			cout << "receive first handshake." << endl;

			HANDLE hThread = CreateThread(NULL, 0, ClientThread, &RemoteAddr, 0, NULL);
			CloseHandle(hThread);
		}
	}

	closesocket(ServerSocket);
	WSACleanup();
	system("pause");
	return 0;
}