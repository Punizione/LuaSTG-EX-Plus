#pragma once

#include <iostream>

#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <queue>
#include <vector>
#include <string>

//ȡ��windows.h�Դ���winsock1.1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif //WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "Ws2_32.lib")

//SB ΢��
#ifdef ERROR
#undef ERROR
#endif // ERROR

//����̨logϸ��
enum class LOGLEVEL
{
	DEBUG   = 1,
	INFO    = 2,
	WARN    = 3,
	ERROR   = 4,
	DISABLE = 5,
};

//ȫ��Log�ȼ�
static const LOGLEVEL G_LOGLEVEL = LOGLEVEL::DEBUG;

bool IFDEBUG() {
	return (G_LOGLEVEL <= LOGLEVEL::DEBUG);
}
bool IFINFO() {
	return (G_LOGLEVEL <= LOGLEVEL::INFO);
}
bool IFWARN() {
	return (G_LOGLEVEL <= LOGLEVEL::WARN);
}
bool IFERROR() {
	return (G_LOGLEVEL <= LOGLEVEL::ERROR);
}

//======================================
//EX+��������

//��д��
struct EX_RW_LOCK {
	volatile long mode;
	EX_RW_LOCK() {
		mode = 1;//0 write 1 none 2+ read
	}
	void ReadStart() {
		volatile long current_mode = mode;//Create A SnapShot of current mode
		while (current_mode == 0 || //In read mode
			InterlockedCompareExchange(&mode, current_mode + 1, current_mode) != current_mode) { //mode has changed by another thread
			Sleep(0);//Sleep a little time
			current_mode = mode;//Update snapshot
		}
	}
	void ReadEnd() {
#ifdef _DEBUG
		assert(InterlockedDecrement(&mode) != 0);
#else
		InterlockedDecrement(&mode);
#endif // _DEBUG
	}
	void WriteStart() {
		while (InterlockedCompareExchange(&mode, 0, 1) != 1)
		{
			Sleep(0);//Sleep a little time
		}
	}
	void WriteEnd() {
#ifdef _DEBUG
		assert(InterlockedIncrement(&mode) == 1);
#else
		InterlockedIncrement(&mode);
#endif // _DEBUG	
	}
};

//����C++17��д�Ķ�д��
struct ETC_RW_LOCK {
	mutable std::shared_mutex mutex_object;
	void ReadStart() {
		mutex_object.lock_shared();
	}
	void ReadEnd() {
		mutex_object.unlock_shared();
	}
	void WriteStart() {
		mutex_object.lock();
	}
	void WriteEnd() {
		mutex_object.unlock();
	}
};

//�����ַ���������
//�ɻػ�ʽ�������ݣ������ﻺ����β�������Զ���ת��������ͷ
//�����棬�����ȡ����ʱ���ݳ��ȹ����������ظ�������
class EXSTRINGBUFFER :public EX_RW_LOCK {
	volatile int buffer_head;
	volatile int buffer_tail;
	char *buffer;
	int size;
public:
	EXSTRINGBUFFER(int count) {
		size = count;
		buffer = new char[size + 1];
		buffer_head = 0;
		buffer_tail = 0;
	}
	~EXSTRINGBUFFER() {
		delete buffer;
	}
	int Push(const char *p) {
		while (*p) {
			buffer[buffer_head++] = *(p++);
			buffer_head = buffer_head % size;
			if (buffer_head == buffer_tail) {
				return 0;
			}
		}
	}
	int Get(char *out_data, int max_count)
	{
		int i = 0;
		WriteStart();
		if (buffer) {
			while (buffer[buffer_tail] && i < max_count && buffer_tail != buffer_head)
			{
				out_data[i] = buffer[buffer_tail];
				buffer_tail = (buffer_tail + 1) % size;
				i++;
			}
			if (i && !buffer[buffer_tail]) {
				buffer_tail = (buffer_tail + 1) % size;
			}
		}
		out_data[i] = 0;
		WriteEnd();
		return i;
	}
};

//��̬�ַ�����������ʹ�����µĶ�д��
//ʹ��string��queue��д
class StringBuffer :public ETC_RW_LOCK {
private:
	std::queue<std::string> queue_object;
public:
	StringBuffer() {
		Clear();
	}
	~StringBuffer() {
		Clear();
	}
	//�򻺳���β����һ���ַ���
	void Push(std::string str) {
		std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
		queue_object.push(str);
	}
	//��ȡ���ȷ�����ַ�������ȡ��Ӷ�����pop��
	std::string Get()
	{
		std::string str_out;
		{
			std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
			str_out = std::move(queue_object.front());//�ƶ���������ʡ��Դ�����ܷ�ֹ�������x
			queue_object.pop();
		}
		return str_out;
	}
	//����ַ���������
	void Clear() {
		std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
		while (!queue_object.empty()) {
			queue_object.pop();
		}
	}
	//�ǿյ���
	bool Empty() {
		std::shared_lock<std::shared_mutex> lock(mutex_object);//���������Զ����ٶ���
		return queue_object.empty();
	}
};

//======================================
//EX+��������

//���������
constexpr unsigned int MAX_TCP_CLIENT = 16;

//TCP״̬
enum class TCPStatus {
	none    = 0,//��ʼ״̬
	free    = 1,//�����õ�ַ���׽���
	install = 2,//�Ѿ�������
	connect = 3,//�շ�����״̬
	close   = 4,//�ر�
	invalid = 5,//������Ҫǿ�йر��׽��ֺ��շ��߳�
};

//WSA״̬
static bool G_INIT_WSA = false;
static std::shared_mutex G_WSA_RW_LOCK;

//װ��WSA
void InitWSA() {
	std::unique_lock<std::shared_mutex> lock(G_WSA_RW_LOCK);
	if (G_INIT_WSA) {
		return;
	}
	WSADATA wsaData;
	int iResult;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if ((iResult != 0) && IFERROR()) {
		std::cout << "WSAStartup failed: " << iResult << std::endl;
		return;
	}
	else if (IFINFO()) {
		std::cout << "WSAStartup successfully." << std::endl;
	}
	G_INIT_WSA = true;
}

//�ر�WSA
void DelWSA() {
	std::unique_lock<std::shared_mutex> lock(G_WSA_RW_LOCK);
	if (!G_INIT_WSA) {
		return;
	}
	WSACleanup();
	G_INIT_WSA = false;
}

//��ȡWSAװ��״̬
bool IsWSAvalid() {
	std::shared_lock<std::shared_mutex> lock(G_WSA_RW_LOCK);
	return G_INIT_WSA;
}

//TCP���
class TCPObject :public ETC_RW_LOCK {
public:
	TCPStatus m_status;
private:
	SOCKET socket_object;
	SOCKADDR_IN m_clientaddr;
	StringBuffer send_buffer_object;
	StringBuffer receive_buffer_object;
	std::thread send_thread_object;
	std::thread receive_thread_object;
	bool stop_send_signal;
	bool stop_recv_signal;
public:
	TCPObject() {
		InitWSA();//ÿһ����������һ��WSA����
		socket_object = INVALID_SOCKET;
		send_buffer_object.Clear();
		receive_buffer_object.Clear();
		stop_send_signal = false;
		stop_recv_signal = false;
		m_status = TCPStatus::none;
	};
	~TCPObject() {
		Stop();
	}
	
	//����

	//ֱ�ӽ����е��׽��ֶ����룬����install״̬
	void Create(SOCKET s, SOCKADDR_IN s_in) {
		Stop();
		{
			std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
			socket_object = s;
			m_clientaddr = s_in;
			m_status = TCPStatus::install;
		}
		if (IFDEBUG()) {
			std::cout << "TCP free state." << std::endl;
		}
	};
	
	//���ӵ�ĳ������λ�ã�����install״̬��ʧ���򷵻�free״̬
	bool Connect(std::string host, int port) {
		using std::cout;
		using std::endl;
		
		Stop();
		std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
		bool finish = true;
		
		//create a tcp socket
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		socket_object = socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);
		if ((socket_object == INVALID_SOCKET) && IFERROR())
		{
			cout << "TCP create socket fail: " << WSAGetLastError() << endl;
			finish = false;
		}
		else if (IFINFO()) {
			cout << "TCP create socket successfully." << endl;
		}

		//connect to server
		m_clientaddr.sin_family = AF_INET;
		m_clientaddr.sin_port = htons(port);
		m_clientaddr.sin_addr.S_un.S_addr = inet_addr(host.data());
		if (IFINFO()) {
			cout << "TCP try to connect." << endl;
		}
		if ((connect(socket_object, (SOCKADDR *)&m_clientaddr, sizeof(m_clientaddr)) == SOCKET_ERROR) && IFERROR())
		{
			cout << "TCP connect fail: " << WSAGetLastError() << endl;
			finish = false;
		}
		if ((socket_object == INVALID_SOCKET) && IFERROR())
		{
			cout << "TCP connect fail: " << WSAGetLastError() << endl;
			finish = false;
		}

		//final
		if (finish) {
			m_status = TCPStatus::install;
			if (IFINFO()) {
				std::cout << "TCP connected successfully." << std::endl;
			}
		}
		else {
			m_status = TCPStatus::free;
			if (IFINFO()) {
				std::cout << "TCP connect fail." << std::endl;
			}
		}
		return finish;
	}
	
	//ͨ����Ϣ���л�������ʵ�����ݵķ���
	void Send(std::string data) {
		std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
		if (IFDEBUG()) {
			std::cout << "TCP want to send: " << data << std::endl;
		}
		send_buffer_object.Push(data);
	}

	//ͨ����Ϣ���л�������ʵ�����ݵĽ���
	bool Receive(std::string &out_str) {
		std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
		bool result = !receive_buffer_object.Empty();
		if (result) {
			out_str = receive_buffer_object.Get();
			if (IFDEBUG()) {
				std::cout << "TCP want to get: " << out_str << std::endl;
			}
		}
		else {
			if (IFDEBUG()) {
				std::cout << "TCP receive queue is empty" << std::endl;
			}
		}
		return result;
	};
	
	//���̲߳���

	//�����շ��̣߳���install״̬תΪconnect״̬
	//����install״̬������
	void Start() {
		std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
		if (m_status != TCPStatus::install) {
			if (IFERROR()) {
				std::cout << "TCP status reqire install." << std::endl;
			}
			return;
		}
		stop_send_signal = false;
		stop_recv_signal = false;
		m_status = TCPStatus::connect;
		send_buffer_object.Clear();
		receive_buffer_object.Clear();
		//��ʼ�շ��߳�
		StartSendThread();
		StartReceiveThread();
	}

	//invalid״̬ʱ�����رյ�stop״̬
	//connectʱ�����رյ�stop״̬
	//freeʱ���socket
	void Stop() {
		std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
		send_buffer_object.Clear();
		receive_buffer_object.Clear();
		if (m_status != TCPStatus::close) {
			m_status = TCPStatus::close;
			stop_send_signal = true;
			stop_recv_signal = true;
			if (socket_object != INVALID_SOCKET) {
				
				closesocket(socket_object);
				socket_object = INVALID_SOCKET;
			}
		}
		/*
		std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
		if (socket_object) {
			closesocket(socket_object);
		}
		socket_object = INVALID_SOCKET;
		m_status = TCPStatus::free;
		*/
	};
	
	//�����̣߳�ֻ�ܱ�����connect״̬�������ת��invalid״̬
	void SendThread() {
		using std::cout;
		using std::endl;
		using namespace std::chrono_literals;
		
		if (IFDEBUG()) {
			cout << "TCP send thread start." << endl;
		}

		//��δ����ֹͣ�źţ�������Ϣѭ��
		while (true) {
			//��鷢������
			{
				std::shared_lock<std::shared_mutex> lock(mutex_object);//���������Զ����ٶ���
				if (stop_send_signal || (m_status != TCPStatus::connect)) {
					if (IFDEBUG()) {
						cout << "TCP send thread stop." << endl;
					}
					break;
				}
			}
			//����
			if (!send_buffer_object.Empty()) {
				//����״̬�������ݿ��Է���
				std::string data = send_buffer_object.Get();
				int count;
				{
					std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
					count = send(socket_object, data.data(), data.length(), 0);
				}
				if (count == SOCKET_ERROR) {
					//�����ر�
					{
						std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
						if (IFERROR()) {
							cout << "TCP send failed: " << WSAGetLastError() << endl;
						}
						m_status = TCPStatus::invalid;
					}
					Stop();
					break;
				}
				else if (IFDEBUG()) {
					cout << "TCP send: " << count << "->" << data << endl;
				}
			}
			else {
				//�����ݿɷ��ͣ�����
				std::this_thread::sleep_for(1ms);
			}
		}
	}
	void StartSendThread() {
		std::thread td(this->ReceiveThread);
		std::swap(td, send_thread_object);
	}
	
	//�����̣߳�ֻ�ܱ�����connect״̬�������תΪinvalid״̬
	void ReceiveThread() {
		using std::cout;
		using std::endl;
		
		if (IFDEBUG()) {
			cout << "TCP recieve thread start." << endl;
		}

		const unsigned int BUFFER_LENGTH = 1024;
		char *temp_buffer = new char[BUFFER_LENGTH];
		//���ݽ���ѭ��
		while (true)
		{
			//����������
			{
				std::shared_lock<std::shared_mutex> lock(mutex_object);//���������Զ����ٶ���
				if (stop_recv_signal || (m_status != TCPStatus::connect)) {
					if (IFDEBUG()) {
						cout << "TCP recieve thread stop." << endl;
					}
					break;
				}
			}
			memset(temp_buffer, 0, BUFFER_LENGTH);
			int count;
			{
				std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
				count = recv(socket_object, temp_buffer, BUFFER_LENGTH, 0);
			}
			if (count <= 0)
			{
				//�����ر�
				{
					std::unique_lock<std::shared_mutex> lock(mutex_object);//��ռ���������Զ����ٶ���
					if (IFERROR()) {
						cout << "TCP receive failed: " << WSAGetLastError() << endl;
					}
					m_status = TCPStatus::invalid;
				}
				Stop();
				break;
			}
			else {
				//�յ���Ϣ���浽�ַ�����������
				if (IFDEBUG()) {
					cout << "TCP receive: " << count << "<-" << temp_buffer << endl;
				}
				std::string data = temp_buffer;
				receive_buffer_object.Push(data);
			}
		}
		delete[] temp_buffer;
	}
	void StartReceiveThread() {
		std::thread td(this->ReceiveThread);
		std::swap(td, receive_thread_object);
	}
};

//EX+����������
class TCPGameServer {
private:
	SOCKET serversocket_object;
	std::vector<TCPObject> client_objects;
public:
	TCPGameServer();
	~TCPGameServer();

	//���ڱ��ص�IP��ַ������ֻ�ÿ��Ƕ˿ڼ���
	bool Start(int port);

	//ֹͣ������
	void Stop();

	static void static_server_thread(void *p);
	void server_thread();

	static void static_server_boardcast_thread(void *p);
	void server_boardcast_thread();

	void Send(const char *data, int count);

};
