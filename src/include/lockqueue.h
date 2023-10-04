#pragma once
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

template<typename T>
class LockQueue
{
public:
    void Push(const T &data){
        std::lock_guard<std::mutex> lock(lkmutex);
        lkqueue.push(data);
        condvariable.notify_one();
    }
    T Pop(){
        std::unique_lock<std::mutex> lock(lkmutex);
        while(lkqueue.empty()){
            condvariable.wait(lock);
        }

        T data = lkqueue.front();
        lkqueue.pop();
        return data;
    } 
    
private:
    std::queue<T> lkqueue;
    std::mutex lkmutex;
    std::condition_variable condvariable;
};