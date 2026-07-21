#ifndef THREADPOOL
#define THREADPOOL

#include <iostream>
#include <vector>
#include <condition_variable>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <chrono>
#include <future>
#include <iomanip>

using std::string, std::cout, std::endl;

void log(const string& msg);
void log(const string& msg, int x);

class ThreadPool {

/*
    公共接口:构造函数和析构函数，
    自动初始化线程池，
    并且在工作完成之后结束线程池。
    定义任务提交函数。
    使用模板，接受任意可调用对象，任意对象的参数，回调函数。
    使用auto处理不确定的返回值，
    异步执行任务，自动执行回调函数，
    最后返回future对象，用来同步等待任务结果。
*/

public:
    ThreadPool(size_t TNum);
    ~ThreadPool();

    template<class F, class CB>
    auto submit(F&& f, CB&& cb) -> std::future<decltype(f())>;
      
/*
    隐藏接口:
    定义工作线程以及存放线程的数组，任务队列
    锁，条件变量，控制线程之间调度的条件
*/

private:
    void worker();// 工作线程
    std::vector<std::thread> workers;// 线程
    std::queue<std::function<void()>> jobs;// 任务队列

    std::mutex mtx;// 锁
    std::condition_variable cv;// 条件变量
    bool stop = false;// 控制条件
};

#endif