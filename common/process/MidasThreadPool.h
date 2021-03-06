#ifndef MIDAS_MIDAS_THREAD_POOL_H
#define MIDAS_MIDAS_THREAD_POOL_H

#include <utils/log/Log.h>
#include <atomic>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/**
 * thread pool to run user's functor with signature:
 * ret func(int id, other_params)
 * where id is the index of the thread that runs the functor
 */

namespace midas {

namespace detail {

template <typename T>
class Queue {
public:
    bool push(T const &value) {
        std::unique_lock<std::mutex> lock(this->mutex);
        this->q.push(value);
        return true;
    }
    // deletes the retrieved element, do not use for non integral types
    bool pop(T &v) {
        std::unique_lock<std::mutex> lock(this->mutex);
        if (this->q.empty()) return false;
        v = this->q.front();
        this->q.pop();
        return true;
    }
    bool empty() {
        std::unique_lock<std::mutex> lock(this->mutex);
        return this->q.empty();
    }

private:
    std::queue<T> q;
    std::mutex mutex;
};
}

class ThreadPool {
public:
    ThreadPool(int nThreads = std::thread::hardware_concurrency()) {
        MIDAS_LOG_INFO("start thread pool with " << nThreads << " threads");
        this->init();
        this->resize(nThreads);
    }

    /**
     * the destructor waits for all the functions in the queue to be finished
     */
    ~ThreadPool() { this->stop(true); }

    /**
     * @return the number of running threads in the pool
     */
    int size() { return static_cast<int>(this->threads.size()); }

    // number of idle threads
    int n_idle() { return this->nWaiting; }

    std::thread &get_thread(int i) { return *this->threads[i]; }

    /**
     * change the number of threads in the pool
     * should be called from one thread, otherwise be careful to not interleave, also with this->stop()
     * @param nThreads must be >= 0
     */
    void resize(int nThreads) {
        if (!this->isStop && !this->isDone) {
            int oldNThreads = static_cast<int>(this->threads.size());
            if (oldNThreads <= nThreads) {  // if the number of threads is increased
                this->threads.resize(nThreads);
                this->flags.resize(nThreads);

                for (int i = oldNThreads; i < nThreads; ++i) {
                    this->flags[i] = std::make_shared<std::atomic<bool>>(false);
                    this->set_thread(i);
                }
            } else {  // the number of threads is decreased
                for (int i = oldNThreads - 1; i >= nThreads; --i) {
                    *this->flags[i] = true;  // this thread will finish
                    this->threads[i]->detach();
                }
                {
                    // stop the detached threads that were waiting
                    std::unique_lock<std::mutex> lock(this->mutex);
                    this->cv.notify_all();
                }
                this->threads.resize(nThreads);  // safe to delete because the threads are detached
                // safe to delete because the threads have copies of shared_ptr of the flags, not originals
                this->flags.resize(nThreads);
            }
        }
    }

    void clear_queue() {
        std::function<void(int id)> *_f;
        while (this->q.pop(_f)) delete _f;
    }

    /**
     * pops a functional wrapper to the original function
     * at return, delete the function even if an exception occurred
     */
    std::function<void(int)> pop() {
        std::function<void(int id)> *_f = nullptr;
        this->q.pop(_f);
        std::unique_ptr<std::function<void(int id)>> func(_f);
        std::function<void(int)> f;
        if (_f) f = *_f;
        return f;
    }

    /**
     * wait for all computing threads to finish and stop all threads
     * may be called asynchronously to not pause the calling thread while waiting
     * @param isWait true all the functions in the queue will run, false the queue is cleared without running
     */
    void stop(bool isWait = false) {
        if (!isWait) {
            if (this->isStop) return;
            this->isStop = true;
            for (int i = 0, n = this->size(); i < n; ++i) {
                *this->flags[i] = true;  // command the threads to stop
            }
            this->clear_queue();
        } else {
            if (this->isDone || this->isStop) return;
            this->isDone = true;  // give the waiting threads a command to finish
        }

        {
            std::unique_lock<std::mutex> lock(this->mutex);
            this->cv.notify_all();  // stop all waiting threads
        }

        for (int i = 0; i < static_cast<int>(this->threads.size()); ++i) {
            // wait for the computing threads to finish
            if (this->threads[i]->joinable()) this->threads[i]->join();
        }

        // if there were no threads in the pool but some functors in the queue, delete them here
        this->clear_queue();
        this->threads.clear();
        this->flags.clear();
    }

    template <typename F, typename... Rest>
    auto push(F &&f, Rest &&... rest) -> std::future<decltype(f(0, rest...))> {
        auto pck = std::make_shared<std::packaged_task<decltype(f(0, rest...))(int)>>(
            std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Rest>(rest)...));
        auto _f = new std::function<void(int id)>([pck](int id) { (*pck)(id); });
        this->q.push(_f);
        std::unique_lock<std::mutex> lock(this->mutex);
        this->cv.notify_one();
        return pck->get_future();
    }

    /**
     * un the user's function that excepts argument int - id of the running thread
     * @return std::future, where the user can get the result and rethrow the exceptions
     */
    template <typename F>
    auto push(F &&f) -> std::future<decltype(f(0))> {
        auto pck = std::make_shared<std::packaged_task<decltype(f(0))(int)>>(std::forward<F>(f));
        auto _f = new std::function<void(int id)>([pck](int id) { (*pck)(id); });
        this->q.push(_f);
        std::unique_lock<std::mutex> lock(this->mutex);
        this->cv.notify_one();
        return pck->get_future();
    }

private:
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;

    void set_thread(int i) {
        std::shared_ptr<std::atomic<bool>> flag(this->flags[i]);
        auto f = [this, i, flag]() {
            std::atomic<bool> &_flag = *flag;
            std::function<void(int id)> *_f;
            bool isPop = this->q.pop(_f);
            while (true) {
                while (isPop) {  // if there is anything in the queue
                    std::unique_ptr<std::function<void(int id)>> func(_f);
                    (*_f)(i);

                    // the thread is wanted to stop, return even if the queue is not empty yet
                    if (_flag)
                        return;
                    else
                        isPop = this->q.pop(_f);
                }

                // the queue is empty here, wait for the next command
                std::unique_lock<std::mutex> lock(this->mutex);
                ++this->nWaiting;
                this->cv.wait(lock, [this, &_f, &isPop, &_flag]() {
                    isPop = this->q.pop(_f);
                    return isPop || this->isDone || _flag;
                });

                --this->nWaiting;
                if (!isPop) return;  // if the queue is empty and this->isDone == true or *flag then return
            }
        };
        this->threads[i].reset(new std::thread(f));  // compiler may not support std::make_unique()
    }

    void init() {
        this->nWaiting = 0;
        this->isStop = false;
        this->isDone = false;
    }

    std::vector<std::unique_ptr<std::thread>> threads;
    std::vector<std::shared_ptr<std::atomic<bool>>> flags;
    detail::Queue<std::function<void(int id)> *> q;
    std::atomic<bool> isDone;
    std::atomic<bool> isStop;
    std::atomic<int> nWaiting;  // how many threads are waiting

    std::mutex mutex;
    std::condition_variable cv;
};
}

#endif
