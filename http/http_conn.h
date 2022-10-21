#ifndef HTTP_CONN
#define HTTP_CONN
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <cstddef>
#include <unistd.h>
#include <iostream>
#include <fcntl.h>
#include <string.h>
#include <map>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include "../log/log.h"


class Http_conn {
public:
    //设置读取文件的名称m_real_file大小
    static const int FILENAME_LEN=200;
    //设置读缓冲区m_read_buf大小
    static const int READ_BUFFER_SIZE=2048;
    //设置写缓冲区m_write_buf大小
    static const int WRITE_BUFFER_SIZE=1024;
    //报文的请求方法，本项目只用到GET和POST
    enum METHOD{GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATH};
    //主状态机的状态
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};
    //报文解析的结果
    enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION};
    //从状态机的状态
    enum LINE_STATUS{LINE_OK=0,LINE_BAD,LINE_OPEN};

public:
    Http_conn();
    ~Http_conn();

public:
    /*初始化套接字***********************/
    void init(int sockfd, 
                const sockaddr_in & addr, 
                char* root, 
                int TRIGMode,
                int close_log, 
                const std::string& user, 
                const std::string password, 
                const std::string sqlname);
    void close_conn();
    void process();
    bool read_once();
    // process_write没有写成的写事件，再次触发调用write
    bool write(); 
    inline sockaddr_in* get_address() {
        return  &m_address;
    }
    void initmysql_result(connection_pool *connPool);


private:
    // 对一些值进行置零，没有其他实际作用
    void init();
    // 原本定义为非成员函数，这里面定义成私有的成员函数
    void removefd(int epollfd, int fd);
    int setnonblocking(int fd); // 返回值为旧的状态标志
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);
    /*注意modfd的作用*/
    void modfd(int epollfd, int fd, int ev, int TRIGMode);
    char* get_line();
    HTTP_CODE process_read();
    LINE_STATUS parse_line();
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    bool add_response(const char* format,...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
    bool process_write(HTTP_CODE ret); // 传入参数为process_read的返回值
    void unmap();
public:
    /*为什么要public？*********************/
    static int m_epollfd; // 整个类只有一个m_epollfd
    static int m_user_count;
    MYSQL* mysql; // 为了外面可以 直接访问
    int timer_flag;
    int improv;
    int m_state;

private:
    int m_sockfd; // 每个请求对象有自己的sockfd，与m_epollfd对比
    int m_read_idx; // 标志已经读到的位置，m_read_idx前面的字符都已经读取完毕
    char m_read_buf[READ_BUFFER_SIZE]; // 读缓冲区数组
    sockaddr_in m_address; // 每个连接有自己的ip地址
    int m_TRIGMode; // 每个连接都有自己的触发模式
    char* doc_root; // 文件存放位置
    int m_close_log;
    char sql_user[100]; // 若后面用new分配，此处可以只用指针
    char sql_password[100];
    char sql_name[100];
    int m_check_state;
    int m_start_line;
    int m_checked_idx;
    char* m_url;
    char* m_version;
    METHOD m_method;
    int m_linger; //长连接： 若为Keep alive 则将m_linger设置为1
    int cgi;
    int m_content_length;
    char* m_host;
    char* m_string;
    char m_real_file[FILENAME_LEN];
    struct stat m_file_stat;
    char *m_file_address;
    int m_write_idx;
    char* m_write_buf;  
    struct iovec m_iv[2];
    int m_iv_count;
    int bytes_to_send;
    int bytes_have_send;
};

#endif 