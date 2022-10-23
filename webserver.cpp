#include "./webserver.h"


WebServer::WebServer() {
    // 一个服务器对象处理多个连接，先用最大文件描述符个数建立连接数组
    users = new Http_conn[MAX_FD];

    // 服务器存储文件的位置
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    /* 定时器 客户数据*****************/
    users_timer = new client_data[MAX_FD]; // 每个连接都有一个定时器
}

WebServer::~WebServer() {
    delete[] users;
    delete[] users_timer;
    /*************关闭一些东西，但是不知道这些东西哪里打开的*/
}

void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    // 端口号码，通过端口 + ip确定唯一套接字
    m_port = port;
    /*************** 用户名是哪传来的*/
    m_user = user; 
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::trig_mode() {
    /***************为什么分两个来控制触发模式*/
    if (m_TRIGMode == 0) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    else if (m_TRIGMode == 1) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    else if (m_TRIGMode == 2) {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    else if (m_TRIGMode == 3) {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

void WebServer::log_write(){
    if (0 == m_close_log) { // 0代表打开日志
        /*************不知道两种日志有什么区别*/
        if (1 == m_log_write ){

        }
        else {

        }
    }
}

void WebServer::sql_pool() {
    // 数据库单例模式
    m_connPool = connection_pool::GetInstance();
    m_connPool->init("localhost", m_user, 
    m_passWord, m_databaseName, 3306, m_sql_num, m_close_log); // mysql用3306端口
    // 用单例模式的数据库对数据连接进行初始化
    users->initmysql_result(m_connPool);
}

void WebServer::thread_pool() {
    // new出来一个线程池
    // 线程池默认值个数为8，可以通过运行程序传入的参数进行修改
    m_pool = new Thread_pool<Http_conn>(m_actormodel, m_connPool, m_thread_num); 

}

void WebServer::eventListen() {
    // 先对监听套接字进行设置

    // 创建 套接字
    m_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (0 == m_OPT_LINGER) { // 为0设置延迟
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    else if (1 == m_OPT_LINGER) { // 为1关闭延迟
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    }
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    // 设置套接字的地址族，端口号，IPV4地址
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY); // 任意ip地址接收到数据都进行处理
    address.sin_port = htons(m_port);

    int ret = 0;
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)); // time_wait阶段仍然可以连接
    ret = bind(m_listenfd, &address, sizeof(address));;
    ret = listen(m_listenfd, 5);

    /*设置utils************************/
    // utils 用来封装 事件时间排序的链表，
    // epoll 设置
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5); // size 没有实际意义
    /*将epoll包装到utils内了吗？*********************/
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    Http_conn::m_epollfd = m_epollfd; // 整个类只有一个epoll，所以是静态的，通过类名直接访问

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    // 将留在另一端的m_pipefd设置为非阻塞
    utils.setnonblocking(m_pipefd[1]); 
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);
    utils.addsig(SIGPIPE, SIG_IGN); // SIG_IGN 代表该信号忽略
    utils.addsig(SIGALRM, utils.sig_handler, false); // false 代表对于mask设置restart位
    utils.addsig(SIGTERM, utils.sig_handler, false);
    
    alarm(TIMESLOT); // TIMESLOT秒后发送SIGALRM信号
    
    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;


}


void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode,
                        m_close_log, m_user, m_passWord, m_databaseName);
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer; // 创建定时器
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL); // 当前时间
    timer->expire = cur + 3 * TIMESLOT; // 设置到期时间
    users_timer[connfd].timer = timer; // 将数组的指针指向整个timer
    utils.m_timer_lst.add_timer(timer); // 加到链表
}

void WebServer::adjust_timer(util_timer *timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3*TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    
}

void WebServer::deal_timer(util_timer* timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    utils.m_timer_lst.del_timer(timer);
}

/************是否可以少传入一个参数呢*/
// void WebServer::deal_timer(util_timer* timer) {
//     timer->cb_func(&timer->user_data);
//     utils.m_timer_lst.del_timer(timer);
// }

bool WebServer::dealclinetdata() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    //LT 的监听触发模式，只是accept一次
    if (0 == m_LISTENTrigmode) {
        int connfd = accept(m_listenfd, (sockaddr*)&client_address, &client_addrlength);
        if (Http_conn::m_user_count >= MAX_FD){ // timer 内部调用init， init 内部会对count++；
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    else {
        while (1) {
            int connfd = accept(m_listenfd, (sockaddr*)&client_address, &client_addrlength);
            if (connfd < 0) {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                return false;
            }
            if (Http_conn::m_user_count >= MAX_FD){ 
                LOG_ERROR("%s", "Internal server busy");
                return false;
            }
            timer(connfd, client_address);
        }   
    }
    // 只有当LT模式才可能返回true；
    // ET模式最终只有因为accept出错或者文件描述符超过才能通过返回false跳出循环
    return true;
}

/*为什么这两个参数? : 传入的是引用，通过信号来对这两个值进行设置*/
bool WebServer::dealwithsignal(bool& timeout, bool& stop_server) {
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (0 >= ret) return false;
    for (int i = 0; i < ret; ++i) {
        switch (signals[i]) {
            case SIGALRM :
                timeout = true;
                break;
            case SIGTERM : 
                stop_server = true;
                break;
        }
    }
    return false; 
}

void WebServer::dealwithread(int sockfd) {
    util_timer *timer = users_timer[sockfd].timer; // WebServer 构造函数中创建的数组
    // reactor
    if (1 == m_actormodel) {
        if (timer != nullptr) {
            // 更新时间
            adjust_timer(timer);
        }
        // 原来的 append是有两个参数的，第二个参数是stat, o是读，1是写
        // 所谓的reactor，加入到工作队列，等待的线程去处理
        m_pool->append(users + sockfd, 0); 
        // 死循环等待处理
        while (1) {
            if (1 == users[sockfd].improv) { // 为1，代表读取完成
                /*************读取失败，对方可能已经关闭，这个时候可以将删除了*/
                if (1 == users[sockfd].timer_flag) { 
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0; // 删除完毕，就不要再删除了
                }
                users[sockfd].improv = 0; // 已经处理完成，就设置为0
                break;
            }
        }
    }
    else {
        /************所谓的proactor，没有直接加入队列交给线程处理，而是主线程先读取*/
        if (users[sockfd].read_once()) {
            // 这个append_p，就是直接处理
            m_pool->append_p(users + sockfd);
             if (timer != nullptr) {
                // 更新时间
                adjust_timer(timer);
            }
        }
        else {
            // 读取失败，该链接可能已经断开
            deal_timer(timer, sockfd);
        }
    }
}

void WebServer::dealwithwrite(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;
    // reactor
    if (1 == m_actormodel) {
        if (timer != nullptr) {
            // 更新时间
            adjust_timer(timer);
        }
        // 原来的 append是有两个参数的，第二个参数是stat, o是读，1是写
        // 所谓的reactor，加入到工作队列，等待的线程去处理
        m_pool->append(users + sockfd, 1); 
        // 死循环等待处理
        while (1) {
            if (1 == users[sockfd].improv) { // 为1，代表读取完成
                /*************读取失败，对方可能已经关闭，这个时候可以将删除了*/
                if (1 == users[sockfd].timer_flag) { 
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0; // 删除完毕，就不要再删除了
                }
                users[sockfd].improv = 0; // 已经处理完成，就设置为0
                break;
            }
        }
    }
    else {
        /************所谓的proactor，没有直接加入队列交给线程处理，而是主线程先读取*/
        if (users[sockfd].write()) {
            // 这个append_p，就是直接处理
             if (timer != nullptr) {
                // 更新时间
                adjust_timer(timer);
            }
        }
        else {
            // 读取失败，该链接可能已经断开
            deal_timer(timer, sockfd);
        }
    }    
}

void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;
    while (!stop_server) {
        // 将事件取出放到events数组中，-1表示永远阻塞
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        for (int i = 0; i < number; ++i ) {
            int sockfd = events[i].data.fd;
            if (sockfd == m_listenfd) {
                bool flag = dealclinetdata();
                // 处理失败，不进行后续的操作
                if (false == flag)
                    continue;                
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
              else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }     
        if (timeout) {
            //时间到了就处理
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        } 
    }
}
