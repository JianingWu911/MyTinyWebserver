# include<semaphore.h>
# include<iostream>
# include<string.h>
# include<pthread.h>


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