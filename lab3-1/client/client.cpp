#include <stdio.h>
#include <iostream>
#include <winsock2.h>
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
#define PORT 1234
#define MAX_BUFFER 1024

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
			u_short checkSum;//校验和
		} useToCheck;
	};
};

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

#define CHECK_COUNT (sizeof(DataPackage) / 2 - 1)//用于校验和检验

//计算校验和函数
void checkSum(DataPackage* package) {
	register u_long sum = 0;
	for (int i = 0; i < CHECK_COUNT; i++) {
		sum += package->useToCheck.checkContent[i];
		if (sum & 0XFFFF0000)
		{
			sum &= 0XFFFF;
			sum++;
		}
	}
	package->useToCheck.checkSum = (u_short)~(sum & 0XFFFF);
}

int main(int argc, char* argv[])
{
	SOCKET sclient;//客户端套接字
	int iResult;//返回的结果

	//初始化SOCKET DLL
	WSADATA WsaData;
	iResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
	if (iResult != 0)
	{
		cout << "Init Windows Socket Failed:" << iResult << endl;
		system("pause");
		return -1;
	}

	//创建SOCKET
	sclient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sclient == INVALID_SOCKET)
	{
		cout << "Create Socket Failed:" << WSAGetLastError() << endl;
		system("pause");
		WSACleanup();
		return  -1;
	}

	char IP[15];
	cout << "Please enter the IP address:" << endl;
	cin >> IP;
	getchar();

	sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(PORT);
	sin.sin_addr.S_un.S_addr = inet_addr(IP);
	int len = sizeof(sin);

	DataPackage dataPackage;
	SignPackage signPackage;

	char SendBuffer[MAX_BUFFER] = { 0 };
	char RecvBuffer[MAX_BUFFER];
	int ret;

	clock_t start, end;
	//设置超时时间（非阻塞）
	timeval tv = { 10, 0 };
	setsockopt(sclient, SOL_SOCKET, SO_RCVTIMEO, (char*)& tv, sizeof(tv));

	//第一次握手
	signPackage.pac.lable = FIRST_HAND_SHAKE;
	signPackage.pac.serialNumber = 0;
	sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
	int testNum = 1;
	cout << "The first handshake has been sent." << endl;

	start = clock();
	//第二次握手
	while (true) {
		end = clock();
		recvfrom(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, &len);
		if (signPackage.pac.lable == SECOND_HAND_SHAKE) {
			cout << "The second handshake has been received." << endl;
			break;
		}
		if (testNum > MAX_TEST_NUM) {
			cout << "Transmission error, press any key to exit." << endl;
			signPackage.pac.lable = SEND_ERROR;
			sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
			sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
			sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
			system("pause");
			return -1;
		}
		if ((end - start) > 20) {
			signPackage.pac.lable = FIRST_HAND_SHAKE;
			signPackage.pac.serialNumber = 0;
			sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
			cout << "The first handshake has been retransmitted." << endl;
			testNum ++;
			start = clock();
		}
	}

	//第三次握手
	signPackage.pac.lable = THIRD_HAND_SHAKE;
	sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
	sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
	sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
	cout << "The third handshake has been sent." << endl;

	cout << "Client OK!" << endl;

	while (true)
	{
		cout << "---File Transfer---" << endl;

		FILE* fp;
		char file_name[MAX_PATH];

		cout << "Please Input The Filename:" << endl;
		cin >> file_name;
		//传送文件名
		if (!(fp = fopen(file_name, "rb")))
		{
			cout << "File " << file_name << " Can't Open" << endl;
			continue;
		}

		memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
		dataPackage.pac.lable = FILE_NAME;
		dataPackage.pac.length = strlen(file_name);
		strcpy(dataPackage.pac.data, file_name);
		checkSum(&dataPackage);
		int testNum = 0;//记录失败次数
	
		sendto(sclient, dataPackage.str, sizeof(dataPackage.str), 0, (sockaddr*)& sin, len);
		testNum++;
		cout << "File name has been sent!" << endl;
		start = clock();

	
		while (true) {
			end = clock();
			ret = recvfrom(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, &len);
			if (testNum == 100 || signPackage.pac.lable == SEND_ERROR) {
				cout << "Transmission error, press any key to exit." << endl;
				signPackage.pac.lable = SEND_ERROR;
				sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
				sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
				sendto(sclient, signPackage.str, sizeof(signPackage.str), 0, (sockaddr*)& sin, len);
				system("pause");
				return -1;
			}
			if (ret != SOCKET_ERROR && signPackage.pac.lable == FILE_NAME) {
				cout << "server has confirmed to receive the file name." << endl;
				break;
			}
			if ((end - start) > 200) {
				sendto(sclient, dataPackage.str, sizeof(dataPackage.str), 0, (sockaddr*)& sin, len);
				testNum++;
				cout << "File name has been sent!" << endl;
				start = clock();
			}
		}


		//传送文件
		int length;
		testNum = 0;
		memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
		int serialNumber = 1;
		dataPackage.pac.serialNumber = serialNumber;
		while ((length = fread(dataPackage.pac.data, sizeof(char), sizeof(dataPackage.pac.data), fp)) > 0)
		{
			testNum = 0;
			dataPackage.pac.length = length;
			dataPackage.pac.lable = SEND_DATA;
			serialNumber = serialNumber ? 0 : 1;
			dataPackage.pac.serialNumber = serialNumber;
			checkSum(&dataPackage);

			ret = sendto(sclient, dataPackage.str, sizeof(dataPackage), 0, (sockaddr*)& sin, len);
			testNum++;
			start = clock();

			while (true) {
				end = clock();
				ret = recvfrom(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, &len);
				if (testNum == 100 || signPackage.pac.lable == SEND_ERROR) {
					cout << "Transmission error, press any key to exit." << endl;
					signPackage.pac.lable = SEND_ERROR;
					sendto(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, len);
					sendto(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, len);
					sendto(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, len);
					system("pause");
					return -1;
				}
				if (ret != SOCKET_ERROR && signPackage.pac.lable == SEND_DATA && signPackage.pac.serialNumber == dataPackage.pac.serialNumber) {
					break;
				}
				if ((end - start) > 200) {
					ret = sendto(sclient, dataPackage.str, sizeof(dataPackage), 0, (sockaddr*)& sin, len);
					testNum++;
					start = clock();
				}
			}
			memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
		}

		testNum = 0;
		memset(dataPackage.str, 0x00, sizeof(dataPackage.str));
		dataPackage.pac.lable = SEND_FINISH;
		checkSum(&dataPackage);
	
		ret = sendto(sclient, dataPackage.str, sizeof(dataPackage), 0, (sockaddr*)& sin, len);
		testNum++;
		cout << "File has been sent!" << endl;
		start = clock();
	
		while (true) {
			end = clock();
			ret = recvfrom(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, &len);
			if (testNum == 100 || signPackage.pac.lable == SEND_ERROR) {
				cout << "Transmission error, press any key to exit." << endl;
				signPackage.pac.lable = SEND_ERROR;
				sendto(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, len);
				sendto(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, len);
				sendto(sclient, signPackage.str, sizeof(signPackage), 0, (sockaddr*)& sin, len);
				system("pause");
				return -1;
			}
			if (ret != SOCKET_ERROR && signPackage.pac.lable == SEND_FINISH) {
				cout << "server has confirmed to receive the file." << endl;
				break;
			}
			if ((end - start) > 200) {
				ret = sendto(sclient, dataPackage.str, sizeof(dataPackage), 0, (sockaddr*)& sin, len);
				testNum++;
				cout << "File has been sent!" << endl;
				start = clock();
			}
		}
		fclose(fp);
	}

	closesocket(sclient);
	system("pause");
	WSACleanup();
	return 0;
}