#ifndef THREAD_POOL
#define THREAD_POOL

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <list>
#include <exception>
#include "../locker/locker.h"


template<typename T>
class Thread_pool {
public: 
    Thread_pool(connection_pool* connPool, int pthread_num = 8, int max_request = 10000);
    ~Thread_pool(); 
    bool append(T* request, int state);
    bool append_p(T* request);

private:
    // 设置为私有函数
    static void* worker(void*); // 类内静态函数用来作为pthread_create的参数，调用run函数；
    void run();

private:
    pthread_t* m_pthread; // 线程池数组
    int m_pthread_num; // 线程池内线程数
    locker m_queuelocker; // 请求队列的锁
    sem m_queuestat; // 请求队列的信号量
    std::list<T*> m_workerqueue; // 请求工作队列
    connection_pool *m_connPool; // 数据库链接池
    int max_size; // 最大请求数量
    bool m_stop; // 服务器是否停止
    int m_actor_model; // 1 reactor模型，0是proactor（默认）
};

#endif















