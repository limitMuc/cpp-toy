#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <list>
#include <string.h>
#include <string>

using namespace std;

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8000
#define EPOLL_SIZE 1024
#define SERVER_MESSAGE "ClientID #%d: \"%s\""
#define EPOLL_EVENT_NUM 2
#define EXIT "exit"
#define RECORDS_NUM 100						    // 聊天记录的条数限制

list<int> clients_list;                                             // 存放客户端socket
list<string> chat_records;					    // 存放聊天记录

void panic(const char* msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// 将文件描述符添加到内核事件表中
void addfd_to_epoll(int fd, int epollfd) {
    struct epoll_event ev;
    // 将文件描述符添加到内核事件表中
    ev.data.fd = fd;
    // 注册 EPOLLIN事件(可读意味着有客户端通过这个socket文件描述符向服务端请求连接了，即有客户端写入了东西，那么服务端就可以读取到东西了),设置为ET 工作方式
    ev.events = EPOLLIN | EPOLLET;
    // 注册目标文件描述符到epfd中，同时关联内部event到文件描述符上
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    // 获取文件的flags，即open函数的第二个参数
    int flags = fcntl(fd, F_GETFD, 0);
    // 将文件描述符设置非阻塞方式
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 广播
int broadcast(int clientfd) {
    // BUFSIZ为系统默认的缓冲区大小（大小为8192）
    char buf[BUFSIZ];
    char message[BUFSIZ];
    // 清空初始化,置字节字符串的前n个字节为零且包括‘\0’，之所以需要清空是因为在多线程或多进程环境中，为防止某个进程将其初始化的值影响到其他线程或进程对它的操作的结果
    bzero(buf, BUFSIZ);
    bzero(message, BUFSIZ);
    // 从TCP连接的另一端接收数据, 返回接收到的字节数
    int bytes = recv(clientfd, buf, BUFSIZ, 0);
    if (bytes < 0) {
        panic("recv failed");
    }

    if(bytes == 0) {                    // 另一端关闭了连接
        close(clientfd);
        clients_list.remove(clientfd);
    } else {
	// 当聊天记录达到阈值	
	if (chat_records.size() == RECORDS_NUM) {
		chat_records.pop_front();
	}
	chat_records.push_back(buf);
        // 对接受到的客户端消息进行拼接
        sprintf(message, SERVER_MESSAGE, clientfd, buf);
        for(list<int>::iterator it = clients_list.begin(); it != clients_list.end(); it++) {
            // 将消息广播给其余客户端
            if (*it != clientfd) {
                // 将应用程序请求发送的数据拷贝到发送缓存中发送并得到确认后再返回
                if (send(*it, message, BUFSIZ, 0) < 0) {
                    panic("send failed");
                }
            }
        }
    }

    return bytes;
}


