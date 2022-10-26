# include<semaphore.h>
# include<iostream>
# include<string.h>
# include<pthread.h>
#include <exception>



class sem {
public:
    sem() {
        int ret = sem_init(&m_sem, 0, 0); // 第三个参数是信号量的初始值，即从多少个信号开始统计，一般来说肯定是0；
        if (0 != ret) {
            std::cout << "error number" << ret << std::endl;
            std::cout << "error information" << strerror(errno) << std::endl;
            throw std::exception();
        }    
    }
    ~sem() {
        int ret = sem_destroy(&m_sem);
        if (0 != ret) {
            std::cout << "error number" << ret << std::endl;
            std::cout << "error information" << strerror(errno) << std::endl;
            throw std::exception();
        }
    }
    bool wait() {
        int ret = sem_wait(&m_sem);
        if (0 != ret) {
            std::cout << "error number" << ret << std::endl;
            std::cout << "error information" << strerror(errno) << std::endl;
            throw std::exception();
            return false;
        }
        return true;
    }
    bool post() {
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

class locker {
public:
    locker() {
        int ret = pthread_mutex_init(&m_mutex, NULL);
        if (0 != ret) {
            std::cout << "error number" << ret << std::endl;
            std::cout << "error information" << strerror(errno) << std::endl;
            throw std::exception();
        }
    }
    ~locker() {
        int ret = pthread_mutex_destroy(&m_mutex);
        if (0 != ret) {
            std::cout << "error number" << ret << std::endl;
            std::cout << "error information" << strerror(errno) << std::endl;
            throw std::exception();
        }
    }
    int lock() {
        return pthread_mutex_lock(&m_mutex) != 0;
    }
    int unlock() {
        return pthread_mutex_unlock(&m_mutex) != 0; 
    }
private:
    pthread_mutex_t m_mutex;
};

class Cond {
private:
    pthread_cond_t m_cond;

public:
    Cond() {
        if (pthread_cond_init(&m_cond, NULL)) { // 成功返回0
            throw std::exception();
        }
    }
    ~Cond() {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t* mutex) {
        /********************************* 为什么这个上锁不用了*/
        // pthread_mutex_lock(mutex);
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }
    /****************************timewait()没有写*/
    bool signal () {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }
};

