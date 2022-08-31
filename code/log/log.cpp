/*
 * @Author       : mark
 * @Date         : 2020-06-16
 * @copyleft Apache 2.0
 */ 
#include "log.h"

using namespace std;

Log::Log() {
    lineCount_ = 0;
    isAsync_ = false;
    writeThread_ = nullptr;
    deque_ = nullptr;
    toDay_ = 0;
    fp_ = nullptr;
}

Log::~Log() {
    if(writeThread_ && writeThread_->joinable()) {
        while(!deque_->empty()) {
            deque_->flush();
        };
        deque_->Close();
        writeThread_->join();
    }
    if(fp_) {
        lock_guard<mutex> locker(mtx_);
        flush();
        fclose(fp_);
    }
}

int Log::GetLevel() {
    lock_guard<mutex> locker(mtx_);
    return level_;
}

void Log::SetLevel(int level) {
    lock_guard<mutex> locker(mtx_);
    level_ = level;
}

// init 方法
void Log::init(int level = 1, const char* path, const char* suffix,
    int maxQueueSize) {
    isOpen_ = true;
    level_ = level;
    // 如果使用阻塞队列就要设置同步格式
    if(maxQueueSize > 0) {
        isAsync_ = true;
        if(!deque_) {
            // 分别创建队列和单线程
            unique_ptr<BlockDeque<std::string>> newDeque(new BlockDeque<std::string>); // BlockDeque 是线程安全的 Deque
            deque_ = move(newDeque); // 讲一个左值转化为右值
            
            std::unique_ptr<std::thread> NewThread(new thread(FlushLogThread)); // 创建一个线程
            writeThread_ = move(NewThread);
        }
    } else {
        isAsync_ = false;
    }

    lineCount_ = 0;

    // 定义 log 中的时间类
    time_t timer = time(nullptr);
    struct tm *sysTime = localtime(&timer); // 初始化一个本地时间的数据结构
    struct tm t = *sysTime;
    path_ = path;
    suffix_ = suffix;
    char fileName[LOG_NAME_LEN] = {0};
    // 设置日志标题
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
            path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_); // 格式化输出字符串到指定的缓冲区
    toDay_ = t.tm_mday; // 得到当天的日期

    {
        lock_guard<mutex> locker(mtx_); // 使用 RAII 的方法创建 lock ，可以避免因为异常没有解锁就退出
        buff_.RetrieveAll(); // 初始化 buffer ，将指针指向数组开始位置
        if(fp_) { 
            flush();
            fclose(fp_); 
        }
        // 创建 log 文件
        fp_ = fopen(fileName, "a");
        if(fp_ == nullptr) {
            mkdir(path_, 0777);
            fp_ = fopen(fileName, "a");
        } 
        assert(fp_ != nullptr);
    }
}

void Log::write(int level, const char *format, ...) { 
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);// 将当前的时间传给一个 timeval 的结构体变量
    time_t tSec = now.tv_sec; // 得到秒
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    va_list vaList; // 定义可变列表

    // 如果创建文件时间不是在当天或者文件行数到达最大行数
    if (toDay_ != t.tm_mday || (lineCount_ && (lineCount_  %  MAX_LINES == 0)))
    {
        unique_lock<mutex> locker(mtx_);
        locker.unlock();
        
        char newFile[LOG_NAME_LEN];
        char tail[36] = {0};
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        if (toDay_ != t.tm_mday)
        {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday;
            lineCount_ = 0;
        }
        else {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_  / MAX_LINES), suffix_);
        }
        
        locker.lock();
        flush();
        fclose(fp_);
        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }

    {
        unique_lock<mutex> locker(mtx_);
        lineCount_++;
        int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                    t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
                    
        buff_.HasWritten(n); // 移动写指针
        AppendLogLevelTitle_(level);

        va_start(vaList, format);
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
        va_end(vaList);

        buff_.HasWritten(m);
        buff_.Append("\n\0", 2);

        if(isAsync_ && deque_ && !deque_->full()) {
            deque_->push_back(buff_.RetrieveAllToStr());
        } else {
            fputs(buff_.Peek(), fp_);
        }
        buff_.RetrieveAll(); // 将 buffer
    }
}

void Log::AppendLogLevelTitle_(int level) {
    switch(level) {
    case 0:
        buff_.Append("[debug]: ", 9); // 构造一个 len 大小的空间将数据追加进去
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

void Log::flush() {
    if(isAsync_) { 
        deque_->flush(); // 这里的刷新方法只是通知消费者线程
    }
    fflush(fp_); // 将缓冲区的数据刷新到 fp_
}

void Log::AsyncWrite_() { // 将阻塞队列的数据写入到文件
    string str = "";
    while(deque_->pop(str)) {
        lock_guard<mutex> locker(mtx_);
        fputs(str.c_str(), fp_); // c_str：得到 str 的首地址。fputs：将一段数据以流的形式放入到另一个文件中
    }
}

// 得到 log 的实例
Log* Log::Instance() {
    static Log inst;
    return &inst;
}

void Log::FlushLogThread() {
    Log::Instance()->AsyncWrite_();
}