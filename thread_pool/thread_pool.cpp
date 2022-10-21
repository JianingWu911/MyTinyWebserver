#include "./thread_pool.h"

template<typename T>
Thread_pool<T>::Thread_pool(connection_pool* connPool, 
                            int pthread_num = 8, 
                            int max_request = 10000) : 
                            m_connPool(connPool), 
                            m_pthread_num(pthread_num),
                            max_size(max_request) {
    // 数量不能为负
    if (m_pthread_num < 0 || max_size < 0) 
        throw std::exceotion();
    m_pthread = new pthread_t[pthread_num]; // new创建动态数组是用[];
    // new分配失败返回0， 终止
    if (m_pthread == nullptr) 
        throw std::exception();
    for (int i = 0; i < pthread_num; ++i ) {
        int ret = pthread_create(m_pthread + i, NULL, worker, this); // 第一个参数是指针，数组中第i个元素的指针
        if (ret != 0) {
            printf("error number: %d\n", ret);
            printf("error information: %s\n", strerror(errno));
            throw std::exception();
        }
        ret = pthread_detach(m_pthread[i]);
        if (ret != 0) {
            printf("error number: %d\n", ret);
            printf("error information: %s\n", strerror(errno));
            throw std::exception();
        }
    }
}

template<typename T>
bool Thread_pool<T>::append(T* request){
    m_queuelocker.lock();
    if (m_workqueue.size() > max_size) {
        m_queuelocker.unlock();
        return false;
    }
    /*******************为什么要先设置状态*******************/
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    // 信号量用post发布
    m_queuestat.post();
    return true;
}
/**************************没写append_p版本*******************************************/

template<typename T>
void* Thread_pool<T>::worker(void* arg) { 
    // 传入的参数就是无类型的指针，需要进行转换
    Thread_pool<T>* temp = static_cast<Thread_pool<T>*>(arg); // 由于pthread_create()传入的参数void* arg需要进行类型转换，转换为目标类型
    temp->run();  
    return temp; // 函数返回值是void* 需要返回值，这个地方返回转换完成的指针
}
template<typename T>
void Thread_pool<T>::run() {
    while (!m_stop) {
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queue.unlock();
            continue;
        }
        T* request = m_workerqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (request == nullptr) continue;
/************************简略,修改*********************************/
        request->mysql = m_connPool->GetConnection();
        request->process();
        m_connPool->ReleaseConnection(request->mysql);
    }
}
