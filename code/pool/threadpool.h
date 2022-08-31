/*
 * @Descripttion: 
 * @version: 
 * @Author: xuboluo
 * @Date: 2020-10-26 16:45:09
 * @LastEditors: xuboluo
 * @LastEditTime: 2022-08-26 11:55:21
 * 线程池类
 */


#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>
class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount = 8): pool_(std::make_shared<Pool>()) { // 定义一个 pool 类
            assert(threadCount > 0);
            for(size_t i = 0; i < threadCount; i++) {
                // 使用匿名函数的方式定义 thread 匿名函数，匿名函数的作用是从 queue 中获取一个 task 
                std::thread([pool = pool_] {
                    std::unique_lock<std::mutex> locker(pool->mtx); // unique 类型的锁
                    while(true) {
                        if(!pool->tasks.empty()) { // 任务队列不为空
                            auto task = std::move(pool->tasks.front()); // 将左值强制转化为右值引用
                            pool->tasks.pop();
                            locker.unlock();
                            task();
                            locker.lock();
                        } 
                        else if(pool->isClosed) break; // 判断线程池是否关闭
                        else pool->cond.wait(locker); // 空的时候进行阻塞
                    }
                }).detach(); // 线程分离
            }
    }

    ThreadPool() = default;

    ThreadPool(ThreadPool&&) = default;
    
    ~ThreadPool() {
        if(static_cast<bool>(pool_)) {
            {
                std::lock_guard<std::mutex> locker(pool_->mtx);
                pool_->isClosed = true;
            }
            pool_->cond.notify_all();
        }
    }

    template<class F>
    void AddTask(F&& task) {
        {
            std::lock_guard<std::mutex> locker(pool_->mtx);
            pool_->tasks.emplace(std::forward<F>(task)); // 传入不同的处理函数
        }
        pool_->cond.notify_one();
    }

private:
    struct Pool {
        std::mutex mtx; 
        std::condition_variable cond;
        bool isClosed;
        std::queue<std::function<void()>> tasks; // 线程执行的方法是个 que 
    };
    std::shared_ptr<Pool> pool_; // pool 的对象
};


#endif //THREADPOOL_H