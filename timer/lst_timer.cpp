#include <lst_timer.h>
#include "../http/http_conn.h"


sort_timer_lst::sort_timer_lst() : head(nullptr), tail(nullptr) {};

// 标准的删除所有节点
sort_timer_lst::~sort_timer_lst() {
    while (head != nullptr) {
        util_timer* temp = head;
        head = head->next;
        delete temp;
    }
}
// 将原本的两个add_timer()函数和为一个函数
void sort_timer_lst::add_timer(util_timer *timer){
    util_timer* dummyHead = new util_timer();
    dummyHead->next = head;
    util_timer* temp = dummyHead;
    while (temp->next != nullptr) {
        if (temp->next->expire > timer->expire) {
            timer->next = temp->next;
            temp->next = timer;
            temp->next->prev = temp;
            timer->next->prev = timer; 
            
            break;           
        }
        temp = temp->next;
    }
    // 一直比较到最后
    if (temp->next == nullptr) {
        temp->next = timer;
        timer->next = nullptr;
        timer->prev = temp;
        /* ******************这块平白无无故搞出一个tail，有点奇怪，前面都没有对head进行处理*/
        tail = timer;
    }
    //特殊处理，如果插入到头结点，更改head
    if (temp == dummyHead) {
        head = timer;
    }
    // 删除前处理好头结点
    dummyHead->next->prev = nullptr;
    delete dummyHead;
}

// 将delete也用虚拟节点进行压缩
void sort_timer_lst::del_timer(util_timer *timer){
    util_timer* dummyHead = new util_timer();
    dummyHead->next = head;
    util_timer* temp = dummyHead;
    while (temp->next != nullptr) {
        if (temp->next == timer) {
            temp->next = temp->next->next;
            if (temp->next != nullptr) temp->next->prev = temp;
            else tail = temp;
            if (temp == dummyHead) head = temp->next;
            break;           
        }
        temp = temp->next;
    }
    // 删除前处理好头结点
    dummyHead->next->prev = nullptr;
    delete dummyHead;
}

void sort_timer_lst::tick(){
    if (head == nullptr) return;
    time_t cur_time = time(NULL);
    util_timer *temp = head;
    while (head != nullptr) {
        if (head->expire > cur_time) break;
        head->cb_func(head->user_data);
        temp = head;
        head = head->next;
        if (head != nullptr) head->prev = nullptr;
        else tail = head;
        delete temp;
    }
}

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

// 设置阻塞的套路
int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event event;
    event.data.fd = fd;
    if (TRIGMode == 1) {
        event.events = EPOLLET | EPOLLIN | EPOLLRDHUP; // EPOLLRDHUP 监听对方关闭tcp连接 
    }
    else event.events = EPOLLIN | EPOLLRDHUP;
    // 保证一个socket不能同时被两个线程处理，开启 EPOLLONESHOT
    // 当某个线程处理完成，应当立即重置该选项
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::sig_handler(int sig) {
    int save_errno = errno;
    int msg = sig;
    // 信号只是一个int，且只是低一位字节就可以存放，所以用char来存储就好。
    send(u_pipefd[1], (char*) &msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    sa.sa_handler = handler;
    if (restart) {
        /**********restart 什么作用？*/
        sa.sa_flags |= SA_RESTART; 
    }
    // 将sa_mask设置为全部信号集，这样在处理信号时候，其他信号都会被阻塞，而不是被抛弃
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

//定时处理任务，重新定时以不断触发SIGALRM信号
void Utils::timer_handler()
{
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

// 删除epoll监听事件，关闭连接，当然可以写成其他的功能
void cb_func(client_data *user_data) {
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd,NULL);
    close(user_data->sockfd);
    Http_conn::m_user_count--;
}