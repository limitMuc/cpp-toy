#include "common.hpp"

int main() {
	// 1、创建socket
    int clientfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	// 创建失败会返回-1
    if (clientfd < 0){
        panic("creat socket failed");
    }
	
	// IPv4套接口地址结构,需要将主机字节序转换位网络字节序
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = PF_INET;                        // 选择协议族
    serverAddr.sin_port = htons(SERVER_PORT);               // 将port转换为网络字节序
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);      // 将ip转换为网络字节序

	// 2、创建管道
    int pipefd[2];											// 其中pipefd[0]是读端被子进程使用，pipefd[1]是写端被父进程使用，由于管道是利用环形队列实现的，数据从写端流入管道，从读端流出，这样就实现了进程间通信
    if (pipe(pipefd) < 0) { 
		panic("create pipe failed"); 
	}

	// 3、连接服务端
    if (connect(clientfd, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
        panic("connect failed");
    }

	// 4、创建epoll句柄
    int epfd = epoll_create(EPOLL_SIZE);
    if (epfd < 0) {
        panic("create epfd failed");
    }
	
	// 指定epoll事件监听的文件描述符的个数
    struct epoll_event events[EPOLL_EVENT_NUM];
	//将socket和管道读端描述符都添加到内核事件表中
    addfd_to_epoll(clientfd, epfd);
    addfd_to_epoll(pipefd[0], epfd);

	// 表示客户端是否正常工作
    bool isClientwork = true;

    // 聊天信息缓冲区
    char message[BUFSIZ];

	// 5、Fork一个子进程
	// 这时候就相当于有两个几乎完全相同的进程在同时运行这行代码之后的代码，产生了分叉，因为这时候有两个进程，那么就需要根据不同进程返回不同的进程id，
	// 返回两次，对于父进程来说，fork返回新创建子进程的进程ID；对于子进程来说，fork返回0，因为子进程并没有子进程，所以就可以理解为是子进程的子进程的进程ID是0
    int pid = fork();
	
	// 6、读写管道	
	// 由于有两个进程都在运行同样的逻辑，所以，就可以根据fork()返回的不同的pid来判断究竟是父进程还是子进程
	if (pid < 0) {
        panic("fork failed");
    } 
	// 对于父进程来说，fork返回新创建子进程的进程ID,所以如果大于0就说明当前运行的进程是父进程
	else if (pid > 0) {
		// 父进程负责向管道中写入数据，所以父进程需要关闭管道读端
		close(pipefd[0]);
		cout << "please input 'exit' to exit the chat room" << endl;
		cout << "--------------------------===---------------------------" << endl;
		
		// 需要保证客户端还在正常运行 
		while (isClientwork) {
			// 清空初始化,置字节字符串的前n个字节为零且包括‘\0’，之所以需要清空是因为在多线程或多进程环境中，为防止某个进程将其初始化的值影响到其他线程或进程对它的操作的结果
			bzero(&message, BUFSIZ);
			cout << "\n\n" ;	
			// 从stdin流读入键盘输入的信息
			fgets(message, BUFSIZ, stdin);	

			// 客户端输入"exit"退出
            if (strncasecmp(message, EXIT, strlen(EXIT)) == 0) {			// 比较前N个字符，并忽略大小写差异
                isClientwork = false;
            } else {														// 父进程将输入的信息写入管道中
				if (write(pipefd[1], message, strlen(message) - 1) < 0) {	// 将message里的信息写入到管道写端文件描述符里
                    panic("father progress write failed");
                }
			}		
		}	
	}
	// 对于子进程来说，fork返回0,所以如果等于0就说明当前运行的进程是子进程
	else if (pid == 0) {
		// 子进程负责将管道中的数据读出,所以子进程需要关闭管道写端
		close(pipefd[1]);

		// 需要保证客户端还在正常运行 
		while (isClientwork) {
			// 等待事件的发生,返回就绪事件的数目。第二个参数指当检测到事件就会将所有就绪的事件从内核事件表中复制到其中；最后一个参数指定epoll的超时时间当为-1时，epoll_wait调用将永远阻塞，直到某个事件发生
        	int epoll_events_count = epoll_wait(epfd, events, EPOLL_EVENT_NUM, -1);
        	if (epoll_events_count < 0) {
            	panic("epoll_wait failed");
        	}

			//处理已经就绪的事件
        	for (int i = 0; i < epoll_events_count; i++) {
				// 清空初始化,置字节字符串的前n个字节为零且包括‘\0’，之所以需要清空是因为在多线程或多进程环境中，为防止某个进程将其初始化的值影响到其他线程或进程对它的操作的结果
				bzero(&message, BUFSIZ);

				// 当监听到的事件为服务端socket文件描述符，说明是服务端发来的消息
                if (events[i].data.fd == clientfd) {
					// 从TCP连接的另一端接收数据, 返回接收到的字节数
    				int bytes = recv(clientfd, message, BUFSIZ, 0);
					// 如果返回的字节数等于零，说明服务端关闭了
                    if (bytes == 0) {
                        cout << "server closed connection" << endl;
                        isClientwork = false;
						goto CLOSE;
                    } else if (bytes > 0) {
						// 将服务端发来的消息打印到终端
						cout << message << endl;
					} 
				}
				// 当监听到的事件为管道读端文件描述符，说明是父进程写入数据到管道中，即客户端用户在终端输入了信息，想要发送消息到服务端了
				else {
					// 子进程从管道中读取数据，将管道读端文件描述符的数据取出放入message里
                    int bytes = read(events[i].data.fd, message, BUFSIZ);
				    	
					if (bytes < 0) {
						panic("child progress read failed");
					} else if (bytes == 0) {
						isClientwork = false;
					}					
					else {
						// 将消息发送给服务端
						send(clientfd, message, BUFSIZ, 0);
					}
				} 
			}
		}
	}	

CLOSE:
	// 7、关闭文件描述符
	if (pid) {
        // 关闭写端文件描述符
        close(pipefd[1]);
		// 关闭socket文件描述符
        close(clientfd);
    } else {
        //关闭读端文件描述符
        close(pipefd[0]);
    }

	// 关闭epoll
    close(epfd);

	return 0;
}
