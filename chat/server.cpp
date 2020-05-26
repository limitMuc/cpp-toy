#include "common.hpp"

int main()
{
    // 1、创建socket
    int listener = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    // 创建失败会返回-1
    if (listener < 0){
        panic("creat socket failed");
    }

    // 设置套接字的属性使它能够在计算机重启的时候可以再次使用套接字的端口和IP
    int sock_reuse = 1;
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &sock_reuse, sizeof(sock_reuse)) < 0) {
        panic("reuse socket failed");
    }

    // IPv4套接口地址结构,需要将主机字节序转换位网络字节序
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = PF_INET;                        // 选择协议族
    serverAddr.sin_port = htons(SERVER_PORT);               // 将port转换为网络字节序
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);      // 将ip转换为网络字节序

    // 2、绑定socket
    if (bind(listener, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {          // 由于原始结构sockaddr已经废弃了，为了兼容需要将sockaddr_in进行强转
        panic("bind failed");
    }    
    
    // 3、监听
    if (listen(listener, SOMAXCONN) < 0) {                  // SOMAXCONN定义了系统中每一个端口最大的监听队列的长度,这是个全局的参数,默认值为1024
        panic("listen failed");
    }
    cout << "listening: " << SERVER_IP << " : " << SERVER_PORT << endl;

    // 4、创建epoll句柄
    int epfd = epoll_create(EPOLL_SIZE);
    if (epfd < 0) {
        panic("create epfd failed");
    }

    // 指定epoll事件监听的文件描述符的个数
    struct epoll_event events[EPOLL_SIZE];
    // 将socket文件描述符添加到内核事件表中
    addfd_to_epoll(listener, epfd);

    // 5、接收请求信息
    while(1) {
        // 等待事件的发生,返回就绪事件的数目。第二个参数指当检测到事件就会将所有就绪的事件从内核事件表中复制到其中；最后一个参数指定epoll的超时时间当为-1时，epoll_wait调用将永远阻塞，直到某个事件发生
        int epoll_events_count = epoll_wait(epfd, events, EPOLL_SIZE, -1);
        if (epoll_events_count < 0) {
            panic("epoll_wait failed");
        }

        //处理已经就绪的事件
        for (int i = 0; i < epoll_events_count; i++) {
            // 从内核事件表的拷贝数组中获取其中的socket文件描述符
            int sockfd = events[i].data.fd;
            
            // 如果就绪事件的socket文件描述符与服务端的socket文件描述符相等，说明是新客户端的请求，因为每一个客户端的socket文件描述符都会被重新命名放入内核事件表里
            if (sockfd == listener) {
                // 客户端的IPv4套接口地址结构
                struct sockaddr_in client_address;
                // 提供缓冲区addr的长度以避免缓冲区溢出问题
                socklen_t client_addr_length = sizeof(struct sockaddr_in);
                // 接受连接，成功返回一个新的socket文件描述符，由于事件已经发生了，所以此处并不会阻塞
                int clientfd = accept(listener, (struct sockaddr *) &client_address, &client_addr_length);
                cout << "client connection from: " << inet_ntoa(client_address.sin_addr) << " : " << ntohs(client_address.sin_port) << endl;
                // 将socket文件描述符添加到内核事件表中
                addfd_to_epoll(clientfd, epfd);
                // 将客户端加入到客户端socket链表中
                clients_list.push_back(clientfd);
            }
            // 6、处理信息，进行广播
            else {         
                int bytes = broadcast(sockfd);
                if (bytes < 0) {
                    panic("broadcast failed");
                }            
            }
        }    
    }
    
    // 7、关闭socket
    close(listener);
    
    // 8、关闭epoll
    close(epfd);

    return 0;
}

