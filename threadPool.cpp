#include "threadPool.hpp"
/*
    日志处理函数:
    接受字符串参数，打印日志。
*/

void log(const string& msg) {
    static std::mutex logMtx;
    std::lock_guard<std::mutex> lg(logMtx);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    cout << msg << endl;
}

void log(const string& msg, int x) {
    static std::mutex logMtx;
    std::lock_guard<std::mutex> lg(logMtx);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    cout << msg;
}


/*
    线程池构造函数:
    将创建的线程加入到vector里
*/

ThreadPool::ThreadPool(size_t TNum) {
    log("(init)初始化线程池,使用 " + std::to_string(TNum) + " 个线程");
    for(size_t i = 0; i < TNum; ++i) {
        workers.emplace_back(&ThreadPool::worker, this);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
}

/*
    线程池析构函数:
    将所有线程进行回收
*/

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lg(mtx);// 上锁，通知所有线程即将关闭线程池
        stop = true;
    }
    cv.notify_all();// 唤醒所有线程

    for(auto& t : workers) {
        if(t.joinable()) t.join();
    }
    log("(finish)线程池已关闭");
}

/*
    线程池工作函数:
    while循环:等待任务，期间处于休眠状态，除非线程池关闭
    锁:访问队列时加锁，执行任务时解锁
    条件变量:没有任务时休眠，有任务时加锁
    任务:如果在任务队列中检测到有任务，被唤醒之后，从队列中取出，执行任务
*/

void ThreadPool::worker() {
    log("(worker)工作线程");
    while(true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> ul(mtx);
            cv.wait(ul, [this]() {return stop || !jobs.empty(); });

            if(stop && jobs.empty()) {
                log("(worker)工作线程已销毁");
                return;
            }
            job = std::move(jobs.front());
            jobs.pop();
        }
        job();
    }
}

/*
    线程池任务提交函数:
    模板:声明，接受任意任务，参数，回调
    返回值:future异步处理任务结果，最后执行回调
    returnType:任务自动获取到对应任务的返回值，用于回调
    job:任务包装，加入到队列中
    上锁访问队列:加入
*/

template<class F, class CB>
auto ThreadPool::submit(F&& f, CB&& cb) -> std::future<decltype(f())> {
    using returnType = decltype(f());// 获取返回值类型

    auto job = std::make_shared<std::packaged_task<returnType()>> (
        std::forward<F>(f)
    );// 任务包装器:在让多线程共享的前提下，将任务完美包装到job里
    std::future<returnType> res = job->get_future();// 获取结果

    {// 上锁访问队列
        std::lock_guard<std::mutex> lg(mtx);
        jobs.emplace([job, cb]() {
            (*job)();
            cb();
        });
    }
    cv.notify_one();// 唤醒线程，准备工作
    log("(submit)任务已加入队列");
    return res;
}